#include "CallOffer.h"

#include "callie/Settings.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

class ListSource : public CalendarSource
{
public:
    QList<Event> events;

    QString sourceId() const override { return u"list"_s; }
    QList<CalendarInfo> calendars() const override { return {}; }
    QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                               const QTimeZone &) const override
    {
        QList<Event> result;
        for (const Event &event : events) {
            if (event.start < to && event.end > from)
                result.append(event);
        }
        return result;
    }
    void refresh() override {}
};

const QTimeZone kBerlin("Europe/Berlin");

QDateTime at(int hour, int minute = 0)
{
    return QDateTime(QDate(2026, 11, 2), QTime(hour, minute), kBerlin);
}

Event call(const QString &summary, const QDateTime &start)
{
    Event event;
    event.uid = summary.toLower();
    event.summary = summary;
    event.start = start;
    event.end = start.addSecs(30 * 60);
    event.conferenceUrl = QUrl(u"https://us02web.zoom.us/j/123"_s);
    return event;
}

} // namespace

class TestCallOffer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_settings = std::make_unique<Settings>(m_dir->filePath(u"settings.ini"_s));
        m_settings->setTimeZoneId(u"Europe/Berlin"_s);
        m_settings->setTimeFormat(Settings::TimeFormat::TwentyFourHour);
        m_source.events = {call(u"Standup"_s, at(10)), call(u"Planning"_s, at(11))};
        m_now = at(9, 57);
        m_offer = std::make_unique<CallOffer>();
        m_offer->setSettings(m_settings.get());
        m_offer->setNow([this] { return m_now; });
        m_offer->setSource(&m_source);
    }

    void offersTheCallAboutToStart()
    {
        QVERIFY(m_offer->active());
        QCOMPARE(m_offer->title(), u"Standup"_s);
        QCOMPARE(m_offer->when(), u"In 3 min, 10:00 to 10:30"_s);
        QCOMPARE(m_offer->service(), u"zoom"_s);
        QCOMPARE(m_offer->joinUrl(), QUrl(u"https://us02web.zoom.us/j/123"_s));
    }

    void followsTheClock()
    {
        QSignalSpy changed(m_offer.get(), &CallOffer::changed);
        m_now = at(10, 31);
        m_offer->check();
        QVERIFY(!m_offer->active());
        m_now = at(10, 56);
        m_offer->check();
        QCOMPARE(m_offer->title(), u"Planning"_s);
        QCOMPARE(changed.size(), 2);
    }

    void dismissingHidesThisCallOnly()
    {
        m_offer->dismiss();
        QVERIFY(!m_offer->active());
        m_now = at(10, 10);
        m_offer->check();
        QVERIFY(!m_offer->active());
        m_now = at(10, 58);
        m_offer->check();
        QCOMPARE(m_offer->title(), u"Planning"_s);
    }

    void theSettingTurnsItOff()
    {
        m_settings->setCallCard(false);
        QVERIFY(!m_offer->active());
        m_settings->setCallCard(true);
        QVERIFY(m_offer->active());
    }

    void aNewEventIsSeenWhenTheSourceChanges()
    {
        m_now = at(12);
        m_offer->check();
        QVERIFY(!m_offer->active());
        m_source.events.append(call(u"Retro"_s, at(12, 3)));
        Q_EMIT m_source.changed();
        QCOMPARE(m_offer->title(), u"Retro"_s);
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    ListSource m_source;
    QDateTime m_now;
    std::unique_ptr<CallOffer> m_offer;
};

QTEST_MAIN(TestCallOffer)
#include "tst_calloffer.moc"
