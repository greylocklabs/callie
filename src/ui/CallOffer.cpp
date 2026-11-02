#include "CallOffer.h"

#include "Clock.h"
#include "EventModelForeign.h"
#include "Reminders.h"
#include "callie/NextCall.h"
#include "callie/Settings.h"

#include <QDesktopServices>

using namespace Qt::StringLiterals;

namespace callie {

CallOffer::CallOffer(QObject *parent)
    : QObject(parent), m_now([] { return Clock::instance()->now(); })
{
    setSettings(SettingsForeign::create(nullptr, nullptr));
    connect(Clock::instance(), &Clock::nowChanged, this, &CallOffer::check);
}

void CallOffer::setSource(CalendarSource *source)
{
    if (m_source == source)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (m_source)
        connect(m_source, &CalendarSource::changed, this, &CallOffer::check);
    Q_EMIT sourceChanged();
    check();
}

void CallOffer::setSettings(Settings *settings)
{
    if (m_settings)
        disconnect(m_settings, nullptr, this, nullptr);
    m_settings = settings;
    if (m_settings) {
        connect(m_settings, &Settings::callCardChanged, this, &CallOffer::check);
        connect(m_settings, &Settings::hiddenCalendarsChanged, this, &CallOffer::check);
        connect(m_settings, &Settings::timeZoneChanged, this, &CallOffer::check);
        connect(m_settings, &Settings::timeFormatChanged, this, &CallOffer::check);
    }
    check();
}

void CallOffer::setNow(Now now)
{
    m_now = std::move(now);
    check();
}

QString CallOffer::title() const
{
    if (!m_call)
        return {};
    return m_call->summary.isEmpty() ? tr("(No title)") : m_call->summary;
}

QString CallOffer::when() const
{
    return m_when;
}

QUrl CallOffer::joinUrl() const
{
    return m_call ? m_call->joinUrl() : QUrl();
}

QString CallOffer::service() const
{
    return m_call ? callService(m_call->joinUrl()) : QString();
}

void CallOffer::join()
{
    if (m_call && canJoin(m_call->joinUrl()))
        QDesktopServices::openUrl(m_call->joinUrl());
}

void CallOffer::dismiss()
{
    if (!m_call)
        return;
    m_dismissed.insert(callKey(*m_call));
    offer(std::nullopt);
    check();
}

void CallOffer::check()
{
    if (!m_source || !m_settings || !m_settings->callCard()) {
        offer(std::nullopt);
        return;
    }
    // One read at a time; a check asked for meanwhile follows it.
    if (m_reading) {
        m_checkAgain = true;
        return;
    }
    const QDateTime now = m_now();
    const QDateTime soon = now.addSecs(std::chrono::seconds(kCallLead).count() + 60);
    QFuture<SourceSnapshot> read = m_source->load(now, soon, m_settings->timeZone());
    const auto review = [this, now](const QList<Event> &events) {
        offer(nextCall(events, now, m_settings->hiddenCalendars(), m_dismissed));
    };
    if (read.isFinished()) {
        review(read.resultCount() > 0 ? read.result().events : QList<Event>());
        return;
    }
    m_reading = true;
    read.then(this, [this, review](const SourceSnapshot &snapshot) {
        m_reading = false;
        if (m_settings && m_settings->callCard())
            review(snapshot.events);
        if (std::exchange(m_checkAgain, false))
            check();
    });
}

void CallOffer::offer(std::optional<Event> call)
{
    // The first line of what a reminder says, without the place.
    const QString when =
        call && m_settings
            ? Reminders::describe(*call, m_now(), *m_settings->times()).body.section(u'\n', 0, 0)
            : QString();
    const bool same = call.has_value() == m_call.has_value() &&
                      (!call || callKey(*call) == callKey(*m_call)) && when == m_when;
    m_call = std::move(call);
    m_when = when;
    if (!same)
        Q_EMIT changed();
}

} // namespace callie
