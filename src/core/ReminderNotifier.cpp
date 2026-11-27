#include "callie/ReminderNotifier.h"

#include "callie/NextCall.h"
#include "callie/ReminderScheduler.h"
#include "callie/Settings.h"
#include "callie/Times.h"
#include "greylock/Notifications.h"

#include <QDir>
#include <QLocale>
#include <QLockFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace callie {

ReminderNotifier::ReminderNotifier(QObject *parent) : QObject(parent)
{
    m_retry.setInterval(std::chrono::minutes(1));
    connect(&m_retry, &QTimer::timeout, this, &ReminderNotifier::claim);
}

ReminderNotifier::~ReminderNotifier() = default;

void ReminderNotifier::setLockPath(const QString &path)
{
    m_lockPath = path;
}

void ReminderNotifier::setup(ReminderScheduler *scheduler, greylock::NotificationServer *server,
                             Settings *settings, const QString &openLabel)
{
    if (m_scheduler)
        disconnect(m_scheduler, nullptr, this, nullptr);
    if (m_server)
        disconnect(m_server, nullptr, this, nullptr);
    if (m_settings)
        disconnect(m_settings, nullptr, this, nullptr);
    m_scheduler = scheduler;
    m_server = server;
    m_settings = settings;
    m_openLabel = openLabel;
    m_shown.clear();

    connect(m_scheduler, &ReminderScheduler::due, this,
            [this](const Event &event) { notify(event); });
    connect(m_server, &greylock::NotificationServer::actionInvoked, this,
            &ReminderNotifier::onAction);
    connect(m_server, &greylock::NotificationServer::closed, this,
            [this](uint id) { m_shown.remove(id); });
    connect(m_settings, &Settings::notifyChanged, this, &ReminderNotifier::applySettings);
    connect(m_settings, &Settings::reminderMinutesChanged, this, &ReminderNotifier::applySettings);
    connect(m_settings, &Settings::hiddenCalendarsChanged, this, &ReminderNotifier::applySettings);
    connect(m_settings, &Settings::timeZoneChanged, this, &ReminderNotifier::applySettings);
    claim();
}

bool ReminderNotifier::isNotifying() const
{
    return m_lock && m_lock->isLocked();
}

void ReminderNotifier::claim()
{
    if (!isNotifying()) {
        QString path = m_lockPath;
        if (path.isEmpty()) {
            const QString runtime =
                QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
            path = QDir(runtime.isEmpty() ? QDir::tempPath() : runtime)
                       .filePath(u"callie-reminders.lock"_s);
        }
        m_lock = std::make_unique<QLockFile>(path);
        // Held for as long as this process runs, however long that is; QLockFile still takes
        // over the lock of a holder that is gone, as after a crash.
        m_lock->setStaleLockTime(0);
        m_lock->tryLock(0);
        if (isNotifying() && m_retry.isActive())
            m_catchUp = false;
    }
    if (isNotifying())
        m_retry.stop();
    else
        m_retry.start();
    applySettings();
}

void ReminderNotifier::applySettings()
{
    if (!m_scheduler || !m_settings)
        return;
    m_scheduler->setHiddenCalendars(m_settings->hiddenCalendars());
    m_scheduler->setTimeZone(m_settings->timeZone());
    m_scheduler->setDefaultMinutes(m_settings->reminderMinutes());
    m_scheduler->setEnabled(m_settings->notify() && isNotifying(), m_catchUp);
    m_catchUp = true;
}

ReminderNotifier::Text ReminderNotifier::describe(const Event &event, const QDateTime &now,
                                                  const Times &times)
{
    const QString title = event.summary.isEmpty() ? tr("(No title)") : event.summary;
    const QDateTime start = event.start.toTimeZone(times.zone());
    const qint64 seconds = now.secsTo(event.start);

    QString when;
    if (event.allDay) {
        when = start.date() == now.toTimeZone(times.zone()).date()
                   ? tr("Today")
                   : QLocale().toString(start.date(), u"dddd, MMMM d"_s);
    } else {
        const QString span = tr("%1 to %2").arg(times.time(event.start), times.time(event.end));
        if (seconds <= 30) {
            when = tr("Now, %1").arg(span);
        } else if (seconds < 90 * 60) {
            const int minutes = int((seconds + 59) / 60);
            when = tr("In %1, %2").arg(tr("%n min", nullptr, minutes), span);
        } else if (start.date() == now.toTimeZone(times.zone()).date()) {
            when = span;
        } else {
            when = tr("%1, %2").arg(QLocale().toString(start.date(), u"dddd"_s), span);
        }
    }
    return {title, event.location.isEmpty() ? when : when + u'\n' + event.location};
}

void ReminderNotifier::notify(const Event &event)
{
    if (!m_server || !m_settings || !m_scheduler)
        return;
    const Text text = describe(event, m_scheduler->now(), *m_settings->times());
    // "default" is a click on the notification; daemons show it as that, not
    // as one more button.
    QStringList actions{u"default"_s, m_openLabel};
    if (canJoin(event.joinUrl()))
        actions << u"join"_s << tr("Join call");
    actions << u"snooze"_s << tr("Snooze %n min", nullptr, kSnoozeMinutes);
    // Daemons may read the body as markup, and its place comes from whoever
    // sent the invitation.
    m_server->show(text.title, text.body.toHtmlEscaped(), actions,
                   [guard = QPointer(this), event](uint id) {
                       if (guard && id != 0)
                           guard->m_shown.insert(id, event);
                   });
    Q_EMIT reminded(event);
}

void ReminderNotifier::onAction(uint id, const QString &key)
{
    const auto found = m_shown.constFind(id);
    if (found == m_shown.cend())
        return;
    const Event event = *found;
    if (key == u"join"_s && canJoin(event.joinUrl())) {
        Q_EMIT joinRequested(event.joinUrl());
    } else if (key == u"snooze"_s) {
        m_scheduler->snooze(event, kSnoozeMinutes);
    } else if (key == u"default"_s) {
        Q_EMIT openRequested(event);
    } else {
        return;
    }
    m_server->close(id);
    m_shown.remove(id);
}

} // namespace callie
