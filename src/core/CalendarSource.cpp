#include "callie/CalendarSource.h"

#include <QPointer>

namespace callie {

void CalendarSource::copyThenDelete(CalendarSource &from, CalendarSource &to, const Event &event,
                                    const QString &calendarId, Created done)
{
    if (!event.attendees.isEmpty()) {
        done(tr("An event with guests can only move to a calendar in its own Google account."));
        return;
    }
    QString error;
    const QByteArray data = from.exportEvents(event.calendarId, event.uid, &error);
    if (data.isEmpty()) {
        done(error);
        return;
    }
    // The original goes only once the copy is in, so a failure loses nothing.
    const QPointer<CalendarSource> source(&from);
    to.importEvents(calendarId, data, [source, event, done](int, const QString &error) {
        if (!error.isEmpty()) {
            done(error);
            return;
        }
        if (!source) {
            done(tr("That calendar is not connected any more."));
            return;
        }
        source->deleteEvent(event, true, done);
    });
}

} // namespace callie
