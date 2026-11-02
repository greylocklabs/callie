#include "callie/NextCall.h"

#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

const QTimeZone kBerlin("Europe/Berlin");

QDateTime at(int hour, int minute = 0)
{
    return QDateTime(QDate(2026, 11, 2), QTime(hour, minute), kBerlin);
}

Event call(const QString &summary, const QDateTime &start, int minutes = 30)
{
    Event event;
    event.uid = summary.toLower();
    event.summary = summary;
    event.calendarId = u"work"_s;
    event.start = start;
    event.end = start.addSecs(minutes * 60);
    event.conferenceUrl = QUrl(u"https://meet.google.com/abc-defg-hij"_s);
    return event;
}

QString summaryOf(const std::optional<Event> &event)
{
    return event ? event->summary : QString();
}

} // namespace

class TestNextCall : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void offeredFromAFewMinutesBeforeUntilItEnds()
    {
        const QList<Event> events{call(u"Standup"_s, at(10))};
        QCOMPARE(summaryOf(nextCall(events, at(9, 54), {}, {})), QString());
        QCOMPARE(summaryOf(nextCall(events, at(9, 55), {}, {})), u"Standup"_s);
        QCOMPARE(summaryOf(nextCall(events, at(10, 29), {}, {})), u"Standup"_s);
        QCOMPARE(summaryOf(nextCall(events, at(10, 30), {}, {})), QString());
    }

    void leavesOutWhatCannotOrNeedNotBeJoined()
    {
        Event noLink = call(u"Lunch"_s, at(10));
        noLink.conferenceUrl = QUrl();
        Event allDay = call(u"Offsite"_s, at(0), 24 * 60);
        allDay.allDay = true;
        Event declined = call(u"Sync"_s, at(10));
        declined.declined = true;
        Event hidden = call(u"Hidden"_s, at(10));
        hidden.calendarId = u"other"_s;
        Event notWeb = call(u"Phone"_s, at(10));
        notWeb.conferenceUrl = QUrl(u"tel:+15551234"_s);
        const QList<Event> events{noLink, allDay, declined, hidden, notWeb};
        QCOMPARE(summaryOf(nextCall(events, at(10, 5), {u"other"_s}, {})), QString());
    }

    void aLinkInTheDescriptionCounts()
    {
        Event event = call(u"Review"_s, at(10));
        event.conferenceUrl = QUrl();
        event.description = u"Join: https://us02web.zoom.us/j/123456"_s;
        QCOMPARE(summaryOf(nextCall({event}, at(10), {}, {})), u"Review"_s);
    }

    void aDismissedCallMakesWayForTheNext()
    {
        const Event first = call(u"Standup"_s, at(10));
        const Event second = call(u"Planning"_s, at(10, 30));
        QCOMPARE(summaryOf(nextCall({first, second}, at(10, 10), {}, {callKey(first)})), QString());
        QCOMPARE(summaryOf(nextCall({first, second}, at(10, 27), {}, {callKey(first)})),
                 u"Planning"_s);
    }

    void overlappingCallsOfferTheOneAboutToStart()
    {
        const QList<Event> events{call(u"Standup"_s, at(10)), call(u"Planning"_s, at(10, 30))};
        QCOMPARE(summaryOf(nextCall(events, at(10, 20), {}, {})), u"Standup"_s);
        QCOMPARE(summaryOf(nextCall(events, at(10, 26), {}, {})), u"Planning"_s);
    }

    void namesTheService()
    {
        QCOMPARE(callService(QUrl(u"https://us02web.zoom.us/j/1"_s)), u"zoom"_s);
        QCOMPARE(callService(QUrl(u"https://meet.google.com/x"_s)), u"meet"_s);
        QCOMPARE(callService(QUrl(u"https://teams.microsoft.com/l/x"_s)), u"teams"_s);
        QCOMPARE(callService(QUrl(u"https://example.org/call"_s)), u"web"_s);
        QCOMPARE(callService(QUrl(u"tel:+1555"_s)), QString());
    }
};

QTEST_GUILESS_MAIN(TestNextCall)
#include "tst_nextcall.moc"
