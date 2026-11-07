#include "callie/ZoomAuth.h"

#include "callie/Logging.h"
#include "callie/OAuthReplyHandler.h"
#include "callie/SignInPage.h"
#include "greylock/ThemeLoader.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QOAuth2AuthorizationCodeFlow>
#include <QTimer>
#include <QUrlQuery>

// As in MicrosoftAuth: Qt 6.9 renamed calls that Debian trixie's 6.8 lacks.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
#define CALLIE_QT_OAUTH_69
#endif

namespace callie {

ZoomAuth::ZoomAuth(const ZoomClientConfig &client, QObject *parent)
    : QObject(parent), m_client(client), m_flow(new QOAuth2AuthorizationCodeFlow(this)),
      m_network(new QNetworkAccessManager(this)), m_signInTimer(new QTimer(this))
{
    m_signInTimer->setSingleShot(true);
    m_signInTimer->setInterval(std::chrono::minutes(5));
    connect(m_signInTimer, &QTimer::timeout, this,
            [this] { fail(tr("timed out waiting for Zoom sign-in")); });

    setEndpoints(QUrl(QStringLiteral("https://zoom.us/oauth/authorize")),
                 QUrl(QStringLiteral("https://zoom.us/oauth/token")));
    // The scopes are the app's own, set where it is registered with Zoom.
    m_flow->setClientIdentifier(client.clientId);
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);

    connect(m_flow, &QAbstractOAuth::authorizeWithBrowser, this, &ZoomAuth::authorizeUrlReady);
    connect(m_flow, &QAbstractOAuth::granted, this, &ZoomAuth::onGranted);
    const auto onServerError = [this](const QString &error, const QString &description,
                                      const QUrl &) {
        const QString reason = m_handler ? m_handler->refusalFor(error) : QString();
        if (!reason.isEmpty())
            fail(tr("Zoom turned the sign-in down: %1").arg(reason));
        else
            fail(description.isEmpty() ? error : description);
    };
#ifdef CALLIE_QT_OAUTH_69
    connect(m_flow, &QAbstractOAuth2::serverReportedErrorOccurred, this, onServerError);
#else
    connect(m_flow, &QAbstractOAuth2::error, this, onServerError);
#endif
    connect(m_flow, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error) {
        const QString reason = m_handler ? m_handler->lastRefusal() : QString();
        fail(reason.isEmpty() ? tr("the request to Zoom failed")
                              : tr("Zoom turned the sign-in down: %1").arg(reason));
    });
}

ZoomAuth::~ZoomAuth() = default;

void ZoomAuth::setEndpoints(const QUrl &authorization, const QUrl &token)
{
    m_flow->setAuthorizationUrl(authorization);
    setTokenUrl(token);
}

void ZoomAuth::setTokenUrl(const QUrl &token)
{
    m_tokenUrl = token;
#ifdef CALLIE_QT_OAUTH_69
    m_flow->setTokenUrl(token);
#else
    m_flow->setAccessTokenUrl(token);
#endif
}

void ZoomAuth::authorize()
{
    if (!m_handler) {
        // Loopback only, so the code never reaches the network.
        m_handler = new OAuthReplyHandler(QHostAddress::LocalHost, 0, this);
#ifdef CALLIE_QT_OAUTH_69
        // Zoom matches loopback redirects on the IP literal, never "localhost".
        m_handler->setCallbackHost(QStringLiteral("127.0.0.1"));
#endif
        m_handler->setCallbackText(signInPage(greylock::ThemeLoader::defaultTheme()));
        m_flow->setReplyHandler(m_handler);
    }
    if (!m_handler->isListening() && !m_handler->listen(QHostAddress::LocalHost, 0)) {
        fail(tr("could not listen for the Zoom sign-in callback"));
        return;
    }
    qCInfo(lcAuth) << "waiting for Zoom sign-in at" << m_handler->callback();
    m_signInTimer->start();
    m_flow->grant();
}

void ZoomAuth::setSignInTimeout(std::chrono::milliseconds timeout)
{
    m_signInTimer->setInterval(timeout);
}

void ZoomAuth::refresh(const QString &refreshToken)
{
    // A public client names itself in the form, with no secret to send.
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    form.addQueryItem(QStringLiteral("client_id"), m_client.clientId);
    QNetworkRequest request(m_tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply *reply = m_network->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, refreshToken] {
        reply->deleteLater();
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        const QString accessToken = body[u"access_token"].toString();
        if (reply->error() == QNetworkReply::NoError && !accessToken.isEmpty()) {
            const int expiresIn = body[u"expires_in"].toInt();
            Q_EMIT granted(ZoomTokens{
                accessToken, body[u"refresh_token"].toString(refreshToken),
                expiresIn > 0 ? QDateTime::currentDateTimeUtc().addSecs(expiresIn) : QDateTime()});
            return;
        }
        const QString error = body[u"error"].toString();
        const QString description = body[u"reason"].toString(body[u"error_description"].toString());
        if (error == QLatin1String("invalid_grant"))
            fail(tr("Zoom no longer accepts the saved sign-in. Connect Zoom again."));
        else if (!error.isEmpty())
            fail(tr("Zoom refused to refresh the sign-in: %1")
                     .arg(description.isEmpty() ? error : description));
        else
            fail(tr("could not refresh the Zoom sign-in: %1").arg(reply->errorString()));
    });
}

void ZoomAuth::onGranted()
{
    m_signInTimer->stop();
    qCInfo(lcAuth) << "Zoom granted access";
    if (m_handler)
        m_handler->close();
    const ZoomTokens tokens{m_flow->token(), m_flow->refreshToken(), m_flow->expirationAt()};
    if (tokens.refreshToken.isEmpty()) {
        fail(tr("Zoom did not return a refresh token"));
        return;
    }
    Q_EMIT granted(tokens);
}

void ZoomAuth::fail(const QString &message)
{
    m_signInTimer->stop();
    qCInfo(lcAuth) << "Zoom sign-in failed:" << message;
    if (m_handler)
        m_handler->close();
    Q_EMIT failed(message);
}

} // namespace callie
