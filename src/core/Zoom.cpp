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

// Each Zoom account's sign-in is kept in the keyring under its email.
Account keyFor(const QString &email)
{
    return {u"zoom"_s, email};
}

// The email a calendar's account goes by: the first address in it, as in "microsoft:sam@x.org"
// or CalDAV's "sam@fastmail.com (fastmail.com)".
QString emailOf(const QString &account)
{
    static const QRegularExpression address(u"[^\\s:/()<>]+@[^\\s:/()<>]+"_s);
    return address.match(account).captured();
}

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

QString Zoom::accountFor(const QString &calendarAccount) const
{
    const QStringList accounts = m_settings.zoomAccounts();
    const QString email = emailOf(calendarAccount);
    for (const QString &account : accounts) {
        if (!email.isEmpty() && account.compare(email, Qt::CaseInsensitive) == 0)
            return account;
    }
    return accounts.contains(m_settings.zoomDefault()) ? m_settings.zoomDefault()
                                                       : accounts.value(0);
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
        connected(tokens);
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

void Zoom::connected(const ZoomTokens &tokens)
{
    // Which account signed in decides where its sign-in is kept.
    QNetworkRequest request(m_api.resolved(QUrl(u"users/me"_s)));
    request.setRawHeader("Authorization", "Bearer " + tokens.accessToken.toUtf8());
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, tokens] {
        reply->deleteLater();
        const QString email =
            QJsonDocument::fromJson(reply->readAll()).object()[u"email"].toString();
        if (reply->error() != QNetworkReply::NoError || email.isEmpty()) {
            setError(tr("Zoom did not say which account signed in: %1").arg(reply->errorString()));
            return;
        }
        m_cache.insert(email.toLower(), {tokens.accessToken, tokens.expiresAt});
        const QPointer<Zoom> self(this);
        m_tokens.write(keyFor(email), tokens.refreshToken, [self, email](const QString &error) {
            if (!self)
                return;
            if (!error.isEmpty()) {
                self->setError(tr("Could not keep the Zoom sign-in: %1").arg(error));
                return;
            }
            self->m_settings.addZoomAccount(email);
            // Connected to make meetings with, so the switch makes them from now on.
            self->m_settings.setVideoCallService(u"zoom"_s);
            Q_EMIT self->statusChanged();
        });
    });
}

void Zoom::disconnectAccount(const QString &email)
{
    m_cache.remove(email.toLower());
    m_settings.removeZoomAccount(email);
    if (m_settings.zoomAccounts().isEmpty() && m_settings.videoCallService() == u"zoom")
        m_settings.setVideoCallService(u"calendar"_s);
    const QPointer<Zoom> self(this);
    m_tokens.remove(keyFor(email), [self](const QString &error) {
        if (self && !error.isEmpty())
            qCWarning(lcAuth) << "could not remove the Zoom sign-in:" << error;
    });
    setError({});
}

void Zoom::accessToken(const QString &email, TokenResult result)
{
    const QString key = email.toLower();
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.cend() &&
        QDateTime::currentDateTimeUtc().addSecs(kExpiryMarginSeconds) < cached->expiresAt) {
        QMetaObject::invokeMethod(
            this, [result = std::move(result), token = cached->accessToken] { result(token, {}); },
            Qt::QueuedConnection);
        return;
    }
    QList<TokenResult> &waiting = m_waiting[key];
    waiting.append(std::move(result));
    // One refresh at a time for an account, as each spends the refresh token it uses.
    if (waiting.size() > 1)
        return;
    const QPointer<Zoom> self(this);
    m_tokens.read(keyFor(email), [self, email, key](const QString &refreshToken,
                                                    const QString &error) {
        if (!self)
            return;
        if (!error.isEmpty() || refreshToken.isEmpty()) {
            self->finishToken(key, {}, tr("Connect %1 to Zoom again in Settings.").arg(email));
            return;
        }
        auto *auth = new ZoomAuth(self->m_client, self);
        if (self->m_tokenUrl.isValid())
            auth->setTokenUrl(self->m_tokenUrl);
        connect(auth, &ZoomAuth::granted, self, [self, auth, email, key](const ZoomTokens &tokens) {
            auth->deleteLater();
            self->m_cache.insert(key, {tokens.accessToken, tokens.expiresAt});
            // Stored before it is used, so a crash cannot leave only the spent one.
            self->m_tokens.write(
                keyFor(email), tokens.refreshToken,
                [self, key, accessToken = tokens.accessToken](const QString &error) {
                    if (!self)
                        return;
                    if (!error.isEmpty())
                        qCWarning(lcAuth) << "could not store the new Zoom token:" << error;
                    self->finishToken(key, accessToken, {});
                });
        });
        connect(auth, &ZoomAuth::failed, self, [self, auth, key](const QString &message) {
            auth->deleteLater();
            self->finishToken(key, {}, message);
        });
        auth->refresh(refreshToken);
    });
}

void Zoom::finishToken(const QString &key, const QString &accessToken, const QString &error)
{
    const QList<TokenResult> waiting = m_waiting.take(key);
    for (const TokenResult &result : waiting)
        result(accessToken, error);
}

void Zoom::createMeeting(const QString &account, const QString &topic, const QDateTime &start,
                         const QDateTime &end, bool anyTime, MeetingResult done)
{
    if (!m_settings.zoomAccounts().contains(account)) {
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
    accessToken(account,
                [self, account, body, done](const QString &accessToken, const QString &error) {
                    if (!self)
                        return;
                    if (!error.isEmpty()) {
                        done({}, tr("Zoom could not make the meeting: %1").arg(error));
                        return;
                    }
                    self->post(account, accessToken, body, false, done);
                });
}

void Zoom::post(const QString &account, const QString &token, const QByteArray &body, bool retried,
                MeetingResult done)
{
    QNetworkRequest request(m_api.resolved(QUrl(u"users/me/meetings"_s)));
    request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    QNetworkReply *reply = m_network->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, account, body, retried, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        // A token turned down early is refreshed once and tried again.
        if (status == 401 && !retried) {
            m_cache.remove(account.toLower());
            const QPointer<Zoom> self(this);
            accessToken(account, [self, account, body, done](const QString &accessToken,
                                                             const QString &error) {
                if (!self)
                    return;
                if (!error.isEmpty())
                    done({}, tr("Zoom could not make the meeting: %1").arg(error));
                else
                    self->post(account, accessToken, body, true, done);
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
