#pragma once

#include "ZoomClientConfig.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace callie {

class Settings;
class TokenStore;
class ZoomAuth;
struct ZoomTokens;

/// Zoom meetings for events, made as one of the Zoom accounts connected in Settings: the one
/// with the email of the event's calendar's account, or the default. Each refresh token stays in
/// the keyring and is replaced at every refresh, as Zoom gives a new one each time.
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

    /// Starts signing in to Zoom and emits authorizeUrlReady for the browser. Zoom's own page
    /// can switch accounts, so each sign-in can add another.
    Q_INVOKABLE void connectAccount();
    /// Stops a sign-in still waiting on the browser, as when it was closed without one.
    Q_INVOKABLE void cancelConnect();
    /// Forgets a connected account and its sign-in.
    Q_INVOKABLE void disconnectAccount(const QString &email);
    /// The Zoom account for an event in a calendar of `calendarAccount`: the one with its email,
    /// or the default. Empty when none is connected.
    Q_INVOKABLE QString accountFor(const QString &calendarAccount) const;

    /// Makes a meeting for an event, calling `done` once with its link or an error.
    /// With `anyTime`, as for an all-day or repeating event, the meeting has no fixed
    /// time, so its link works on every day of the event.
    void createMeeting(const QString &account, const QString &topic, const QDateTime &start,
                       const QDateTime &end, bool anyTime, MeetingResult done);

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

    struct Cached
    {
        QString accessToken;
        QDateTime expiresAt;
    };

    void connected(const ZoomTokens &tokens);
    void accessToken(const QString &email, TokenResult result);
    void finishToken(const QString &key, const QString &accessToken, const QString &error);
    void post(const QString &account, const QString &token, const QByteArray &body, bool retried,
              MeetingResult done);
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
    /// Access tokens and the requests waiting on a refresh, by lowercased email.
    QHash<QString, Cached> m_cache;
    QHash<QString, QList<TokenResult>> m_waiting;
};

} // namespace callie
