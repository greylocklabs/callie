#pragma once

#include "callie/Event.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <memory>

class QLockFile;

namespace greylock {
class NotificationServer;
}

namespace callie {

class ReminderScheduler;
class Settings;
class Times;

/// Turns due reminders into desktop notifications and answers their buttons: open the event,
/// join its call, or snooze. One process notifies at a time, whichever of the app, the terminal
/// calendar or `callie reminders` holds the lock, so no reminder comes twice.
class ReminderNotifier : public QObject
{
    Q_OBJECT

public:
    struct Text
    {
        QString title;
        QString body;
    };

    explicit ReminderNotifier(QObject *parent = nullptr);
    ~ReminderNotifier() override;

    /// Starts notifying once this process holds the lock, trying again each minute until it
    /// does. `openLabel` names what a click on a notification does.
    void setup(ReminderScheduler *scheduler, greylock::NotificationServer *server,
               Settings *settings, const QString &openLabel);
    /// Whether this process holds the lock, so its reminders are the ones shown.
    [[nodiscard]] bool isNotifying() const;
    /// The lock's file; the user's runtime folder holds it unless a test gives another.
    void setLockPath(const QString &path);

    /// What a notification about `event` says at `now`.
    [[nodiscard]] static Text describe(const Event &event, const QDateTime &now,
                                       const Times &times);

    static constexpr int kSnoozeMinutes = 5;

Q_SIGNALS:
    /// A click on a reminder, to show `event`.
    void openRequested(const callie::Event &event);
    void joinRequested(const QUrl &url);
    /// A reminder was shown, for anything that shows it its own way too.
    void reminded(const callie::Event &event);

private:
    void claim();
    void applySettings();
    void notify(const Event &event);
    void onAction(uint id, const QString &key);

    QPointer<ReminderScheduler> m_scheduler;
    QPointer<greylock::NotificationServer> m_server;
    QPointer<Settings> m_settings;
    QString m_openLabel;
    QString m_lockPath;
    std::unique_ptr<QLockFile> m_lock;
    QTimer m_retry;
    QHash<uint, Event> m_shown;
};

} // namespace callie
