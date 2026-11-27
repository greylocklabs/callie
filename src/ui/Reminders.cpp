#include "Reminders.h"

#include "callie/ReminderScheduler.h"
#include "callie/Settings.h"
#include "callie/Times.h"

#include <QCoreApplication>
#include <QDesktopServices>

using namespace Qt::StringLiterals;

namespace callie {

Reminders::Reminders(QObject *parent) : QObject(parent) {}

Reminders *Reminders::instance()
{
    static auto *reminders = new Reminders(QCoreApplication::instance());
    return reminders;
}

Reminders *Reminders::create(QQmlEngine *, QJSEngine *)
{
    Reminders *reminders = instance();
    QJSEngine::setObjectOwnership(reminders, QJSEngine::CppOwnership);
    return reminders;
}

void Reminders::setup(ReminderScheduler *scheduler, greylock::NotificationServer *server,
                      Settings *settings)
{
    m_settings = settings;
    disconnect(&m_notifier, nullptr, this, nullptr);
    connect(&m_notifier, &ReminderNotifier::openRequested, this, [this](const Event &event) {
        if (m_settings)
            Q_EMIT openRequested(m_settings->times()->date(event.start), event.uid, event.start);
    });
    connect(&m_notifier, &ReminderNotifier::joinRequested, this,
            [](const QUrl &url) { QDesktopServices::openUrl(url); });
    m_notifier.setup(scheduler, server, settings, tr("Open in Callie"));
}

} // namespace callie
