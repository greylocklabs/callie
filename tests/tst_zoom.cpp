#include "FakeHttpServer.h"
#include "FakeTokenStore.h"
#include "callie/Settings.h"
#include "callie/Zoom.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace callie;
using namespace Qt::StringLiterals;

class TestZoom : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void configComesFromTheEnvironment();
    void needsAConnectedAccount();
    void connectingSignsInAsAPublicClient();
    void aSignInLeftInTheBrowserCanBeCanceled();
    void meetingsComeFromARefreshedSignIn();
    void anAllDayMeetingHasNoTime();
    void aTokenTurnedDownIsRefreshedOnce();
    void disconnectingForgetsTheSignIn();
    void eachAccountKeepsItsOwnSignIn();
    void aCalendarsAccountPicksItsZoomAccount();
    void findsZoomLinks();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<FakeTokenStore> m_tokens;
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<Zoom> m_zoom;

    /// A meeting made through m_zoom, as {join link, error}.
    std::pair<QUrl, QString> meeting(const QDateTime &start, bool allDay = false,
                                     const QString &account = u"sam@example.com"_s)
    {
        std::optional<std::pair<QUrl, QString>> result;
        m_zoom->createMeeting(
            account, u"Review"_s, start, start.addSecs(45 * 60), allDay,
            [&result](const QUrl &join, const QString &error) { result = {join, error}; });
        if (!QTest::qWaitFor([&result] { return result.has_value(); }, 5000))
            return {{}, u"no answer"_s};
        return *result;
    }

    QList<FakeHttpServer::Request> requestsTo(const QByteArray &path) const
    {
        QList<FakeHttpServer::Request> found;
        for (const auto &request : m_server->requests) {
            if (request.target.startsWith(path))
                found << request;
        }
        return found;
    }
};

void TestZoom::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_settings = std::make_unique<Settings>(m_dir->filePath(u"settings.ini"_s));
    m_tokens = std::make_unique<FakeTokenStore>();
    m_server = std::make_unique<FakeHttpServer>();
    m_zoom = std::make_unique<Zoom>(ZoomClientConfig{u"zoom-app"_s}, *m_tokens, *m_settings);
    m_zoom->setEndpoints(m_server->url(u"/v2/"_s), m_server->url(u"/oauth/authorize"_s),
                         m_server->url(u"/oauth/token"_s));
    m_server->handler = [](const FakeHttpServer::Request &request) {
        if (request.target.startsWith("/oauth/token"))
            return FakeHttpServer::Response(
                200, R"({"access_token":"at","refresh_token":"rt2","expires_in":3600})");
        return FakeHttpServer::Response(
            201, R"({"id":1,"join_url":"https://us02web.zoom.us/j/123?pwd=x"})");
    };
}

void TestZoom::configComesFromTheEnvironment()
{
    QProcessEnvironment environment;
    QCOMPARE(ZoomClientConfig::resolve({u"built"_s}, environment).clientId, u"built"_s);
    environment.insert(u"CALLIE_ZOOM_CLIENT_ID"_s, u"mine"_s);
    QCOMPARE(ZoomClientConfig::resolve({u"built"_s}, environment).clientId, u"mine"_s);
    QVERIFY(!Zoom({}, *m_tokens, *m_settings).available());
    QVERIFY(m_zoom->available());
}

void TestZoom::needsAConnectedAccount()
{
    const auto [join, error] = meeting(QDateTime::currentDateTimeUtc());
    QVERIFY(join.isEmpty());
    QVERIFY(error.contains(u"Connect Zoom"_s));
    QVERIFY(m_server->requests.isEmpty());
}

void TestZoom::connectingSignsInAsAPublicClient()
{
    m_server->handler = [](const FakeHttpServer::Request &request) {
        if (request.target.startsWith("/oauth/token"))
            return FakeHttpServer::Response(
                200, R"({"access_token":"at","refresh_token":"rt1","expires_in":3600,)"
                     R"("token_type":"bearer"})");
        return FakeHttpServer::Response(200, R"({"id":"u1","email":"sam@example.com"})");
    };
    // Plays the browser: Zoom sends it back to Callie's loopback address with a code.
    QNetworkAccessManager browser;
    QUrl asked;
    connect(m_zoom.get(), &Zoom::authorizeUrlReady, this, [&browser, &asked](const QUrl &url) {
        asked = url;
        const QUrlQuery query(url);
        QUrl back(query.queryItemValue(u"redirect_uri"_s, QUrl::FullyDecoded));
        back.setQuery(u"code=the-code&state="_s +
                      query.queryItemValue(u"state"_s, QUrl::FullyEncoded));
        QNetworkReply *reply = browser.get(QNetworkRequest(back));
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    });
    m_zoom->connectAccount();
    QVERIFY(m_zoom->connecting());
    QTRY_COMPARE_WITH_TIMEOUT(m_settings->zoomAccounts(), QStringList{u"sam@example.com"_s}, 5000);
    QVERIFY(!m_zoom->connecting());
    QCOMPARE(m_zoom->error(), QString());

    // PKCE, with Zoom sent back to the loopback address it matches on any port.
    const QUrlQuery query(asked);
    QCOMPARE(query.queryItemValue(u"client_id"_s), u"zoom-app"_s);
    QCOMPARE(query.queryItemValue(u"code_challenge_method"_s), u"S256"_s);
    QVERIFY(QUrl(query.queryItemValue(u"redirect_uri"_s, QUrl::FullyDecoded)).host() ==
            u"127.0.0.1"_s);
    const auto exchange = requestsTo("/oauth/token");
    QCOMPARE(exchange.size(), 1);
    QVERIFY(!exchange.first().headers.contains("authorization"));
    QVERIFY(!exchange.first().body.contains("client_secret"));
    QVERIFY(exchange.first().body.contains("code_verifier"));

    // Kept for later, and Zoom now makes the meetings.
    QCOMPARE(m_tokens->secrets.value(u"sam@example.com"_s), u"rt1"_s);
    QCOMPARE(m_settings->videoCallService(), u"zoom"_s);
    QCOMPARE(requestsTo("/v2/users/me").first().headers.value("authorization"),
             QByteArray("Bearer at"));
}

void TestZoom::aSignInLeftInTheBrowserCanBeCanceled()
{
    QUrl asked;
    connect(m_zoom.get(), &Zoom::authorizeUrlReady, this,
            [&asked](const QUrl &url) { asked = url; });
    m_zoom->connectAccount();
    QVERIFY(m_zoom->connecting());
    QTRY_VERIFY(asked.isValid());
    m_zoom->cancelConnect();
    QVERIFY(!m_zoom->connecting());
    QCOMPARE(m_zoom->error(), QString());

    // A sign-in finished in the browser afterwards finds nothing listening, and connects nothing.
    const QUrlQuery query(asked);
    QUrl back(query.queryItemValue(u"redirect_uri"_s, QUrl::FullyDecoded));
    back.setQuery(u"code=late&state="_s + query.queryItemValue(u"state"_s, QUrl::FullyEncoded));
    QNetworkAccessManager browser;
    QNetworkReply *reply = browser.get(QNetworkRequest(back));
    QTRY_VERIFY(reply->isFinished());
    reply->deleteLater();
    QVERIFY(requestsTo("/oauth/token").isEmpty());
    QVERIFY(m_settings->zoomAccounts().isEmpty());

    // And it can be started again.
    m_zoom->connectAccount();
    QVERIFY(m_zoom->connecting());
    m_zoom->cancelConnect();
}

void TestZoom::meetingsComeFromARefreshedSignIn()
{
    m_settings->addZoomAccount(u"sam@example.com"_s);
    m_tokens->secrets.insert(u"sam@example.com"_s, u"rt1"_s);
    const QDateTime start(QDate(2026, 11, 13), QTime(14, 30), QTimeZone("America/New_York"));
    const auto [join, error] = meeting(start);
    QCOMPARE(error, QString());
    QCOMPARE(join, QUrl(u"https://us02web.zoom.us/j/123?pwd=x"_s));

    // A public client: the refresh names the app in the form and sends no secret.
    const auto refresh = requestsTo("/oauth/token");
    QCOMPARE(refresh.size(), 1);
    QVERIFY(!refresh.first().headers.contains("authorization"));
    const QUrlQuery form(QString::fromUtf8(refresh.first().body));
    QCOMPARE(form.queryItemValue(u"grant_type"_s), u"refresh_token"_s);
    QCOMPARE(form.queryItemValue(u"refresh_token"_s), u"rt1"_s);
    QCOMPARE(form.queryItemValue(u"client_id"_s), u"zoom-app"_s);
    // Zoom's new refresh token replaces the spent one.
    QTRY_COMPARE(m_tokens->secrets.value(u"sam@example.com"_s), u"rt2"_s);

    const auto made = requestsTo("/v2/users/me/meetings");
    QCOMPARE(made.size(), 1);
    QCOMPARE(made.first().method, QByteArray("POST"));
    QCOMPARE(made.first().headers.value("authorization"), QByteArray("Bearer at"));
    const QJsonObject body = QJsonDocument::fromJson(made.first().body).object();
    QCOMPARE(body[u"topic"].toString(), u"Review"_s);
    QCOMPARE(body[u"type"].toInt(), 2);
    QCOMPARE(body[u"start_time"].toString(), u"2026-11-13T19:30:00Z"_s);
    QCOMPARE(body[u"duration"].toInt(), 45);

    // The token is kept until it runs out.
    QCOMPARE(meeting(start).second, QString());
    QCOMPARE(requestsTo("/oauth/token").size(), 1);
}

void TestZoom::anAllDayMeetingHasNoTime()
{
    m_settings->addZoomAccount(u"sam@example.com"_s);
    m_tokens->secrets.insert(u"sam@example.com"_s, u"rt1"_s);
    QCOMPARE(meeting(QDateTime(QDate(2026, 11, 13), QTime(0, 0)), true).second, QString());
    const QJsonObject body =
        QJsonDocument::fromJson(requestsTo("/v2/users/me/meetings").first().body).object();
    QCOMPARE(body[u"type"].toInt(), 3);
    QVERIFY(!body.contains(u"start_time"));
}

void TestZoom::aTokenTurnedDownIsRefreshedOnce()
{
    m_settings->addZoomAccount(u"sam@example.com"_s);
    m_tokens->secrets.insert(u"sam@example.com"_s, u"rt1"_s);
    int meetings = 0;
    m_server->handler = [&meetings](const FakeHttpServer::Request &request) {
        if (request.target.startsWith("/oauth/token"))
            return FakeHttpServer::Response(
                200, R"({"access_token":"at","refresh_token":"rt2","expires_in":3600})");
        // Turned down the first time, as a token revoked early would be.
        if (++meetings == 1)
            return FakeHttpServer::Response(401, R"({"code":124,"message":"Invalid token"})");
        return FakeHttpServer::Response(201, R"({"join_url":"https://zoom.us/j/9"})");
    };
    QCOMPARE(meeting(QDateTime::currentDateTimeUtc()).first, QUrl(u"https://zoom.us/j/9"_s));
    QCOMPARE(requestsTo("/oauth/token").size(), 2);

    // Turned down twice is an error, not a loop.
    meetings = -5;
    m_server->handler = [](const FakeHttpServer::Request &request) {
        if (request.target.startsWith("/oauth/token"))
            return FakeHttpServer::Response(
                200, R"({"access_token":"at","refresh_token":"rt3","expires_in":3600})");
        return FakeHttpServer::Response(401, R"({"code":124,"message":"Invalid token"})");
    };
    const auto [join, error] = meeting(QDateTime::currentDateTimeUtc());
    QVERIFY(join.isEmpty());
    QVERIFY(error.contains(u"Invalid token"_s));
}

void TestZoom::disconnectingForgetsTheSignIn()
{
    m_settings->addZoomAccount(u"sam@example.com"_s);
    m_settings->setVideoCallService(u"zoom"_s);
    m_tokens->secrets.insert(u"sam@example.com"_s, u"rt1"_s);
    m_zoom->disconnectAccount(u"sam@example.com"_s);
    QVERIFY(m_settings->zoomAccounts().isEmpty());
    QCOMPARE(m_settings->videoCallService(), u"calendar"_s);
    QTRY_VERIFY(!m_tokens->secrets.contains(u"sam@example.com"_s));
}

void TestZoom::eachAccountKeepsItsOwnSignIn()
{
    m_settings->addZoomAccount(u"sam@example.com"_s);
    m_settings->addZoomAccount(u"sam@work.example"_s);
    m_tokens->secrets.insert(u"sam@example.com"_s, u"home"_s);
    m_tokens->secrets.insert(u"sam@work.example"_s, u"work"_s);
    m_server->handler = [](const FakeHttpServer::Request &request) {
        // Each sign-in refreshes into a token of its own.
        if (request.target.startsWith("/oauth/token")) {
            const QString spent =
                QUrlQuery(QString::fromUtf8(request.body)).queryItemValue(u"refresh_token"_s);
            return FakeHttpServer::Response(200, (u"{\"access_token\":\"at-"_s + spent +
                                                  u"\",\"refresh_token\":\""_s + spent +
                                                  u"2\",\"expires_in\":3600}"_s)
                                                     .toUtf8());
        }
        return FakeHttpServer::Response(201, R"({"join_url":"https://zoom.us/j/1"})");
    };
    const QDateTime start = QDateTime::currentDateTimeUtc();
    QCOMPARE(meeting(start, false, u"sam@work.example"_s).second, QString());
    QCOMPARE(meeting(start, false, u"sam@example.com"_s).second, QString());
    const auto made = requestsTo("/v2/users/me/meetings");
    QCOMPARE(made.size(), 2);
    QCOMPARE(made.at(0).headers.value("authorization"), QByteArray("Bearer at-work"));
    QCOMPARE(made.at(1).headers.value("authorization"), QByteArray("Bearer at-home"));
    QTRY_COMPARE(m_tokens->secrets.value(u"sam@work.example"_s), u"work2"_s);
    QTRY_COMPARE(m_tokens->secrets.value(u"sam@example.com"_s), u"home2"_s);

    // One goes; the other stays, and becomes the default.
    m_zoom->disconnectAccount(u"sam@example.com"_s);
    QCOMPARE(m_settings->zoomAccounts(), QStringList{u"sam@work.example"_s});
    QCOMPARE(m_settings->zoomDefault(), u"sam@work.example"_s);
    QTRY_VERIFY(!m_tokens->secrets.contains(u"sam@example.com"_s));
    QVERIFY(m_tokens->secrets.contains(u"sam@work.example"_s));
    // An account no longer connected makes nothing.
    QVERIFY(!meeting(start, false, u"sam@example.com"_s).second.isEmpty());
}

void TestZoom::aCalendarsAccountPicksItsZoomAccount()
{
    QCOMPARE(m_zoom->accountFor(u"sam@example.com"_s), QString());
    m_settings->addZoomAccount(u"sam@example.com"_s);
    m_settings->addZoomAccount(u"Sam@Work.example"_s);
    // The first connected is the default, for calendars of other accounts.
    QCOMPARE(m_settings->zoomDefault(), u"sam@example.com"_s);
    QCOMPARE(m_zoom->accountFor(u"sam@work.example"_s), u"Sam@Work.example"_s);
    QCOMPARE(m_zoom->accountFor(u"microsoft:sam@work.example"_s), u"Sam@Work.example"_s);
    QCOMPARE(m_zoom->accountFor(u"caldav:https://dav.example/sam@work.example"_s),
             u"Sam@Work.example"_s);
    QCOMPARE(m_zoom->accountFor(u"other@example.com"_s), u"sam@example.com"_s);
    QCOMPARE(m_zoom->accountFor(QString()), u"sam@example.com"_s);
    m_settings->setZoomDefault(u"Sam@Work.example"_s);
    QCOMPARE(m_zoom->accountFor(u"other@example.com"_s), u"Sam@Work.example"_s);
    // Only a connected account can be the default.
    m_settings->setZoomDefault(u"nobody@example.com"_s);
    QCOMPARE(m_settings->zoomDefault(), u"Sam@Work.example"_s);
}

void TestZoom::findsZoomLinks()
{
    QVERIFY(Zoom::isMeeting(QUrl(u"https://us02web.zoom.us/j/123"_s)));
    QVERIFY(Zoom::isMeeting(QUrl(u"https://zoom.us/j/1"_s)));
    QVERIFY(!Zoom::isMeeting(QUrl(u"https://meet.google.com/abc"_s)));
    QVERIFY(!Zoom::isMeeting(QUrl(u"https://zoom.us.example.com/j/1"_s)));
}

QTEST_MAIN(TestZoom)
#include "tst_zoom.moc"
