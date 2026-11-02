#include "callie/NextCall.h"

using namespace Qt::StringLiterals;

namespace callie {

QString callKey(const Event &event)
{
    return event.uid + u'@' + event.start.toUTC().toString(Qt::ISODate);
}

bool canJoin(const QUrl &url)
{
    return url.isValid() && (url.scheme() == u"https"_s || url.scheme() == u"http"_s);
}

QString callService(const QUrl &url)
{
    if (!canJoin(url))
        return {};
    const QString host = url.host().toLower();
    if (host == u"zoom.us"_s || host.endsWith(u".zoom.us"_s))
        return u"zoom"_s;
    if (host == u"meet.google.com"_s)
        return u"meet"_s;
    if (host == u"teams.microsoft.com"_s || host == u"teams.live.com"_s)
        return u"teams"_s;
    return u"web"_s;
}

std::optional<Event> nextCall(const QList<Event> &events, const QDateTime &now,
                              const QStringList &hiddenCalendars, const QSet<QString> &dismissed)
{
    const QDateTime soon = now.addSecs(std::chrono::seconds(kCallLead).count());
    std::optional<Event> best;
    for (const Event &event : events) {
        if (event.allDay || event.declined || event.start > soon || event.end <= now)
            continue;
        if (hiddenCalendars.contains(event.calendarId) || !canJoin(event.joinUrl()) ||
            dismissed.contains(callKey(event)))
            continue;
        if (!best || event.start > best->start)
            best = event;
    }
    return best;
}

} // namespace callie
