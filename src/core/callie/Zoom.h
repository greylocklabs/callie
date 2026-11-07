#pragma once

#include "ZoomClientConfig.h"

#include <QDateTime>
#include <QObject>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace callie {

class Settings;
class TokenStore;
class ZoomAuth;

/// Zoom meetings for events, made as the Zoom account connected in Settings.
/// The refresh token stays in the keyring and is replaced at every refresh,
/// as Zoom gives a new one each time.
class Zoom : public QObject
{
    Q_OBJECT
    /// A Zoom app is configured, so Zoom can be connected at all.
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool connecting READ connecting NOTIFY statusChanged)
    /// Why connecting failed; empty otherwise.
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)

public:
    using MeetingResult = std::function<void(const QUrl &joinUrl, const QString &error)>;

    Zoom(const ZoomClientConfig &client, TokenStore &tokens, Settings &settings,
         QObject *parent = nullptr);
    ~Zoom() override;

    /// Points the API and sign-in at a fake server in tests.
    void setEndpoints(const QUrl &api, const QUrl &authorization, const QUrl &token);

    [[nodiscard]] bool available() const { return m_client.isValid(); }
    [[nodiscard]] bool connecting() const { return m_auth != nullptr; }
    [[nodiscard]] QString error() const { return m_error; }

    /// Starts signing in to Zoom and emits authorizeUrlReady for the browser.
    Q_INVOKABLE void connectAccount();
    /// Forgets the connected account and its sign-in.
    Q_INVOKABLE void disconnectAccount();

    /// Makes a meeting for an event, calling `done` once with its link or an error.
    /// An all-day event gets a meeting with no fixed time.
    void createMeeting(const QString &topic, const QDateTime &start, const QDateTime &end,
                       bool allDay, MeetingResult done);

    /// Whether `url` joins a Zoom meeting.
    [[nodiscard]] static bool isMeeting(const QUrl &url);
    /// Whether `text`, such as an event's Where, holds a Zoom meeting link.
    [[nodiscard]] static bool hasMeeting(const QString &text);
    /// `text` without its Zoom meeting links.
    [[nodiscard]] static QString withoutMeetings(const QString &text);
    /// `where` with the meeting link `join` after what it already says.
    [[nodiscard]] static QString withMeeting(const QString &where, const QUrl &join);

Q_SIGNALS:
    void authorizeUrlReady(const QUrl &url);
    void statusChanged();

private:
    using TokenResult = std::function<void(const QString &accessToken, const QString &error)>;

    void accessToken(TokenResult result);
    void finishToken(const QString &accessToken, const QString &error);
    void post(const QString &token, const QByteArray &body, bool retried, MeetingResult done);
    void readEmail(const QString &accessToken);
    void setError(const QString &error);

    ZoomClientConfig m_client;
    TokenStore &m_tokens;
    Settings &m_settings;
    QNetworkAccessManager *m_network;
    QUrl m_api;
    QUrl m_authorizationUrl;
    QUrl m_tokenUrl;
    ZoomAuth *m_auth = nullptr;
    QString m_error;
    QString m_accessToken;
    QDateTime m_expiresAt;
    QList<TokenResult> m_waiting;
};

} // namespace callie
