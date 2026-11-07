#pragma once

#include "ZoomClientConfig.h"

#include <QDateTime>
#include <QObject>
#include <QUrl>

#include <chrono>

class QNetworkAccessManager;
class QOAuth2AuthorizationCodeFlow;
class QTimer;

namespace callie {

class OAuthReplyHandler;

struct ZoomTokens
{
    QString accessToken;
    /// Zoom gives a new one with every refresh, and the old one stops working.
    QString refreshToken;
    QDateTime expiresAt;
};

/// OAuth 2.0 with Zoom as a public client: the authorization code flow with
/// PKCE and a loopback redirect. Opening the browser is left to the caller.
class ZoomAuth : public QObject
{
    Q_OBJECT

public:
    explicit ZoomAuth(const ZoomClientConfig &client, QObject *parent = nullptr);
    ~ZoomAuth() override;

    /// Points the flow at a fake server in tests.
    void setEndpoints(const QUrl &authorization, const QUrl &token);
    void setTokenUrl(const QUrl &token);

    /// Starts the loopback listener and emits authorizeUrlReady.
    void authorize();
    /// Exchanges a refresh token for a new access token and refresh token.
    void refresh(const QString &refreshToken);
    void setSignInTimeout(std::chrono::milliseconds timeout);

Q_SIGNALS:
    void authorizeUrlReady(const QUrl &url);
    void granted(const callie::ZoomTokens &tokens);
    void failed(const QString &message);

private:
    void onGranted();
    void fail(const QString &message);

    ZoomClientConfig m_client;
    QOAuth2AuthorizationCodeFlow *m_flow;
    QNetworkAccessManager *m_network;
    QUrl m_tokenUrl;
    OAuthReplyHandler *m_handler = nullptr;
    QTimer *m_signInTimer;
};

} // namespace callie
