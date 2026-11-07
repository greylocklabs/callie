#include "callie/Zoom.h"

#include "callie/Logging.h"
#include "callie/NextCall.h"
#include "callie/Settings.h"
#include "callie/TokenStore.h"
#include "callie/ZoomAuth.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

// Margin for clock skew and for requests that start just before expiry.
constexpr int kExpiryMarginSeconds = 60;

// One Zoom account at a time, so its keyring entry has a fixed name.
const Account kZoomAccount{u"zoom"_s, u"meetings"_s};

} // namespace

Zoom::Zoom(const ZoomClientConfig &client, TokenStore &tokens, Settings &settings, QObject *parent)
    : QObject(parent), m_client(client), m_tokens(tokens), m_settings(settings),
      m_network(new QNetworkAccessManager(this)), m_api(u"https://api.zoom.us/v2/"_s)
{}

Zoom::~Zoom() = default;

void Zoom::setEndpoints(const QUrl &api, const QUrl &authorization, const QUrl &token)
{
    m_api = api;
    m_authorizationUrl = authorization;
    m_tokenUrl = token;
}

bool Zoom::isMeeting(const QUrl &url)
{
    return callService(url) == u"zoom";
}

bool Zoom::hasMeeting(const QString &text)
{
    const QStringList words = text.split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts);
    return std::ranges::any_of(words, [](const QString &word) { return isMeeting(QUrl(word)); });
}

QString Zoom::withoutMeetings(const QString &text)
{
    QStringList words = text.split(QRegularExpression(u"\\s+"_s), Qt::SkipEmptyParts);
    words.removeIf([](const QString &word) { return isMeeting(QUrl(word)); });
    return words.join(u' ');
}

QString Zoom::withMeeting(const QString &where, const QUrl &join)
{
    const QString kept = where.trimmed();
    return kept.isEmpty() ? join.toString() : kept + u' ' + join.toString();
}

void Zoom::connectAccount()
{
    if (!available() || m_auth)
        return;
    setError({});
    m_auth = new ZoomAuth(m_client, this);
    if (m_authorizationUrl.isValid())
        m_auth->setEndpoints(m_authorizationUrl, m_tokenUrl);
    connect(m_auth, &ZoomAuth::authorizeUrlReady, this, &Zoom::authorizeUrlReady);
    connect(m_auth, &ZoomAuth::granted, this, [this](const ZoomTokens &tokens) {
        m_auth->deleteLater();
        m_auth = nullptr;
        m_accessToken = tokens.accessToken;
        m_expiresAt = tokens.expiresAt;
        const QPointer<Zoom> self(this);
        m_tokens.write(kZoomAccount, tokens.refreshToken,
                       [self, accessToken = tokens.accessToken](const QString &error) {
                           if (!self)
                               return;
                           if (!error.isEmpty()) {
                               self->setError(tr("Could not keep the Zoom sign-in: %1").arg(error));
                               return;
                           }
                           self->readEmail(accessToken);
                       });
    });
    connect(m_auth, &ZoomAuth::failed, this, [this](const QString &message) {
        m_auth->deleteLater();
        m_auth = nullptr;
        setError(message);
    });
    Q_EMIT statusChanged();
    m_auth->authorize();
}

void Zoom::cancelConnect()
{
    if (!m_auth)
        return;
    // Deleted now, so a sign-in finished in the browser after this reaches nothing.
    delete m_auth;
    m_auth = nullptr;
    Q_EMIT statusChanged();
}

void Zoom::readEmail(const QString &accessToken)
{
    QNetworkRequest request(m_api.resolved(QUrl(u"users/me"_s)));
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        // Connected even without the address, which only names the account.
        const QString email = body[u"email"].toString();
        m_settings.setZoomAccount(email.isEmpty() ? u"Zoom"_s : email);
        // Connected to make meetings with, so the switch makes them from now on.
        m_settings.setVideoCallService(u"zoom"_s);
        Q_EMIT statusChanged();
    });
}

void Zoom::disconnectAccount()
{
    m_accessToken.clear();
    m_expiresAt = {};
    m_settings.setZoomAccount({});
    if (m_settings.videoCallService() == u"zoom")
        m_settings.setVideoCallService(u"calendar"_s);
    const QPointer<Zoom> self(this);
    m_tokens.remove(kZoomAccount, [self](const QString &error) {
        if (self && !error.isEmpty())
            qCWarning(lcAuth) << "could not remove the Zoom sign-in:" << error;
    });
    setError({});
}

void Zoom::accessToken(TokenResult result)
{
    if (!m_accessToken.isEmpty() &&
        QDateTime::currentDateTimeUtc().addSecs(kExpiryMarginSeconds) < m_expiresAt) {
        QMetaObject::invokeMethod(
            this, [result = std::move(result), token = m_accessToken] { result(token, {}); },
            Qt::QueuedConnection);
        return;
    }
    m_waiting.append(std::move(result));
    // One refresh at a time, as each spends the refresh token it uses.
    if (m_waiting.size() > 1)
        return;
    const QPointer<Zoom> self(this);
    m_tokens.read(kZoomAccount, [self](const QString &refreshToken, const QString &error) {
        if (!self)
            return;
        if (!error.isEmpty() || refreshToken.isEmpty()) {
            self->finishToken({}, tr("Connect Zoom again in Settings."));
            return;
        }
        auto *auth = new ZoomAuth(self->m_client, self);
        if (self->m_tokenUrl.isValid())
            auth->setTokenUrl(self->m_tokenUrl);
        connect(auth, &ZoomAuth::granted, self, [self, auth](const ZoomTokens &tokens) {
            auth->deleteLater();
            self->m_accessToken = tokens.accessToken;
            self->m_expiresAt = tokens.expiresAt;
            // Stored before it is used, so a crash cannot leave only the spent one.
            self->m_tokens.write(kZoomAccount, tokens.refreshToken,
                                 [self, accessToken = tokens.accessToken](const QString &error) {
                                     if (!self)
                                         return;
                                     if (!error.isEmpty())
                                         qCWarning(lcAuth)
                                             << "could not store the new Zoom token:" << error;
                                     self->finishToken(accessToken, {});
                                 });
        });
        connect(auth, &ZoomAuth::failed, self, [self, auth](const QString &message) {
            auth->deleteLater();
            self->finishToken({}, message);
        });
        auth->refresh(refreshToken);
    });
}

void Zoom::finishToken(const QString &accessToken, const QString &error)
{
    const QList<TokenResult> waiting = std::exchange(m_waiting, {});
    for (const TokenResult &result : waiting)
        result(accessToken, error);
}

void Zoom::createMeeting(const QString &topic, const QDateTime &start, const QDateTime &end,
                         bool anyTime, MeetingResult done)
{
    if (m_settings.zoomAccount().isEmpty()) {
        done({}, tr("Connect Zoom in Settings to make Zoom meetings."));
        return;
    }
    // 2 is a meeting at a time; 3 has no fixed time, so one link serves every day.
    QJsonObject meeting{{u"topic"_s, topic.isEmpty() ? tr("Meeting") : topic},
                        {u"type"_s, anyTime ? 3 : 2}};
    if (!anyTime) {
        meeting.insert(u"start_time"_s, start.toUTC().toString(u"yyyy-MM-ddTHH:mm:ssZ"_s));
        meeting.insert(u"duration"_s, int(std::max<qint64>(1, start.secsTo(end) / 60)));
    }
    const QByteArray body = QJsonDocument(meeting).toJson(QJsonDocument::Compact);
    const QPointer<Zoom> self(this);
    accessToken([self, body, done](const QString &accessToken, const QString &error) {
        if (!self)
            return;
        if (!error.isEmpty()) {
            done({}, tr("Zoom could not make the meeting: %1").arg(error));
            return;
        }
        self->post(accessToken, body, false, done);
    });
}

void Zoom::post(const QString &token, const QByteArray &body, bool retried, MeetingResult done)
{
    QNetworkRequest request(m_api.resolved(QUrl(u"users/me/meetings"_s)));
    request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    QNetworkReply *reply = m_network->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, body, retried, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        // A token turned down early is refreshed once and tried again.
        if (status == 401 && !retried) {
            m_accessToken.clear();
            const QPointer<Zoom> self(this);
            accessToken([self, body, done](const QString &accessToken, const QString &error) {
                if (!self)
                    return;
                if (!error.isEmpty())
                    done({}, tr("Zoom could not make the meeting: %1").arg(error));
                else
                    self->post(accessToken, body, true, done);
            });
            return;
        }
        const QUrl join(answer[u"join_url"].toString());
        if (reply->error() == QNetworkReply::NoError && isMeeting(join)) {
            done(join, {});
            return;
        }
        const QString reason = answer[u"message"].toString(reply->errorString());
        done({}, tr("Zoom could not make the meeting: %1").arg(reason));
    });
}

void Zoom::setError(const QString &error)
{
    m_error = error;
    Q_EMIT statusChanged();
}

} // namespace callie
