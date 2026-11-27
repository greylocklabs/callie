#pragma once

#include "callie/Event.h"
#include "callie/ReminderNotifier.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>

namespace greylock {
class NotificationServer;
}

namespace callie {

class ReminderScheduler;
class Settings;
class Times;

/// The app's reminders: due reminders as desktop notifications, whose click shows the event in
/// the window and whose Join opens its call. ReminderNotifier does the notifying, which the
/// terminal calendar and `callie reminders` share.
class Reminders : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using Text = ReminderNotifier::Text;

    static Reminders *instance();
    static Reminders *create(QQmlEngine *, QJSEngine *);

    /// Starts notifying. Until then nothing is shown, as in tests and screenshots.
    void setup(ReminderScheduler *scheduler, greylock::NotificationServer *server,
               Settings *settings);

    /// What a notification about `event` says at `now`.
    [[nodiscard]] static Text describe(const Event &event, const QDateTime &now, const Times &times)
    {
        return ReminderNotifier::describe(event, now, times);
    }

    static constexpr int kSnoozeMinutes = ReminderNotifier::kSnoozeMinutes;

Q_SIGNALS:
    /// Asks the window to show `day` (a local midnight) and the event there
    /// with this uid that starts at `start`.
    void openRequested(const QDateTime &day, const QString &uid, const QDateTime &start);

private:
    explicit Reminders(QObject *parent);

    ReminderNotifier m_notifier;
    QPointer<Settings> m_settings;
};

} // namespace callie
