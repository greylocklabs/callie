#include "Tui.h"
#include "TuiState.h"
#include "greylock/cli/Palette.h"

#include "callie/QueuedSource.h"
#include "callie/SampleSource.h"
#include "callie/Settings.h"

#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace callie::cli;
using namespace Qt::StringLiterals;

namespace {

const QDateTime kNow(QDate(2026, 10, 7), QTime(13, 40), QTimeZone::UTC);

/// A few fixed events.
class FixedSource : public CalendarSource
{
public:
    explicit FixedSource(QList<Event> events) : m_events(std::move(events)) {}

    QString sourceId() const override { return u"fixed"_s; }
    QList<CalendarInfo> calendars() const override { return {}; }
    void refresh() override {}
    QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                               const QTimeZone &) const override
    {
        QList<Event> out;
        for (const Event &e : m_events) {
            if (e.end > from && e.start < to)
                out.append(e);
        }
        return out;
    }

private:
    QList<Event> m_events;
};

} // namespace

class TestTui : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates)); }
    void init();
    void startsOnTodayAtWhatIsNext();
    void keysMoveAroundAsInTheApp();
    void viewsCoverTheirDays();
    void hiddenCalendarsStayHidden();
    void daysAreTheSettingsZones();
    void syncStatusRedraws();
    void drawsInTheTheme();
    void detailsAnswerAndTakeBack();
    void decliningAHiddenOneClosesItsDetails();
    void deletingCanBeTakenBack();
    void newEventsAsCallieAddReadsThem();
    void searchAndInvitationsJumpToTheDay();
    void foundAsTheCliListsIt();
    void foundKeepsItsCalendar();
    void refusedChangesAreShown();
    void calendarsShowAndHide();
    void callsOpen();
    void onlyWebCallsOpen();
    void changesAnEventsFields();
    void movesAnEventKeepingItsLength();
    void copiesAndDuplicatesEvents();
    void onlyChangesWhatCanBeChanged();
    void anEventMovedOutOfViewClosesItsFields();
    void aRepeatingEventIsDeletedOnceOrWhole();
    void aRepeatingInvitationIsAnsweredOnceOrWhole();
    void aOneOffEventIsDeletedOnceConfirmed();
    void sharesTheAppsKeys();
    void remindersShowInTheFooter();
    void editsTheLineBeingTyped();
    void recallsEarlierLines();
    void searchesAsItIsTyped();
    void completesCalendarNames();
    void runsCommandsAfterAColon();
    void goesToADate();
    void countsRepeatMoves();
    void movesHalfAScreen();
    void setsTheAgendasLength();

private:
    [[nodiscard]] QStringList summaries() const;
    [[nodiscard]] QString selected() const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<SampleSource> m_source;
    std::unique_ptr<QueuedSource> m_queue;
    std::unique_ptr<TuiState> m_state;
    /// Selects the first event called `summary` in what is shown.
    bool select(const QString &summary);
    /// Types `text` in place of what the line being typed holds.
    void retype(const QString &text);
    /// When the event called `summary` starts on the day shown, or nothing.
    [[nodiscard]] std::optional<Event> shown(const QString &summary) const;
};

void TestTui::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_settings = std::make_unique<Settings>(m_dir->filePath(u"settings.ini"_s));
    m_settings->setTimeZoneId(u"UTC"_s);
    m_source = std::make_unique<SampleSource>();
    m_source->setNow([] { return kNow; });
    // As the TUI runs it: changes held a moment, so they can be taken back.
    m_queue = std::make_unique<QueuedSource>(*m_source, QString());
    m_queue->setNow([] { return kNow; });
    m_state = std::make_unique<TuiState>(*m_queue, *m_settings, [] { return kNow; });
}

QStringList TestTui::summaries() const
{
    QStringList result;
    for (const Event &e : m_state->events())
        result.append(e.summary);
    return result;
}

QString TestTui::selected() const
{
    return m_state->selected() < 0 ? QString() : m_state->events().at(m_state->selected()).summary;
}

void TestTui::startsOnTodayAtWhatIsNext()
{
    QCOMPARE(m_state->view(), TuiState::View::Day);
    QCOMPARE(m_state->day(), QDate(2026, 10, 7));
    QVERIFY(summaries().contains(u"Dentist"_s));
    // What already ended today is passed over.
    const Event next = m_state->events().at(m_state->selected());
    QVERIFY(next.end > kNow);
    for (int i = 0; i < m_state->selected(); ++i)
        QVERIFY(m_state->events().at(i).end <= kNow || m_state->events().at(i).allDay);
}

void TestTui::keysMoveAroundAsInTheApp()
{
    QVERIFY(m_state->events().size() > 2);
    m_state->press(u"g"_s);
    m_state->press(u"g"_s);
    QCOMPARE(m_state->selected(), 0);
    QVERIFY(m_state->press(u"j"_s));
    QCOMPARE(m_state->selected(), 1);
    QVERIFY(m_state->press(u"k"_s));
    QVERIFY(m_state->press(u"Up"_s));
    QCOMPARE(m_state->selected(), 0);
    m_state->press(u"G"_s);
    QCOMPARE(m_state->selected(), int(m_state->events().size()) - 1);
    m_state->press(u"g"_s);
    QCOMPARE(m_state->pending(), u"g"_s);
    m_state->press(u"g"_s);
    QCOMPARE(m_state->selected(), 0);

    m_state->press(u"l"_s);
    QCOMPARE(m_state->day(), QDate(2026, 10, 8));
    m_state->press(u"Left"_s);
    m_state->press(u"h"_s);
    QCOMPARE(m_state->day(), QDate(2026, 10, 6));
    m_state->press(u"t"_s);
    QCOMPARE(m_state->day(), QDate(2026, 10, 7));

    // Keys it does not know are left alone.
    QVERIFY(!m_state->press(u"z"_s));
    m_state->press(u"?"_s);
    QVERIFY(m_state->helpShown());
    QVERIFY(!m_state->press(u"j"_s));
    m_state->press(u"Escape"_s);
    QVERIFY(!m_state->helpShown());
    m_state->press(u"q"_s);
    QVERIFY(m_state->quitRequested());
}

void TestTui::viewsCoverTheirDays()
{
    // The leader, then a view's letter, as in the app's vi mode.
    m_state->press(m_settings->leaderKey());
    QCOMPARE(m_state->pending(), u"leader"_s);
    m_state->press(u"w"_s);
    QCOMPARE(m_state->view(), TuiState::View::Week);
    QCOMPARE(m_state->rangeDays(), 7);
    QCOMPARE(m_state->rangeStart().dayOfWeek(), m_settings->firstDayOfWeek());
    QVERIFY(m_state->rangeStart() <= QDate(2026, 10, 7));
    QVERIFY(summaries().count(u"Standup"_s) >= 3);
    m_state->press(u"l"_s);
    QCOMPARE(m_state->day(), QDate(2026, 10, 14));

    m_state->press(u"Tab"_s);
    QCOMPARE(m_state->view(), TuiState::View::Agenda);
    QCOMPARE(m_state->rangeDays(), 14);
    QCOMPARE(m_state->rangeStart(), m_state->day());
    // In order: by day, then all-day first, then by start.
    for (int i = 1; i < m_state->events().size(); ++i) {
        const Event &a = m_state->events().at(i - 1), &b = m_state->events().at(i);
        QVERIFY(m_state->dayOf(a) < m_state->dayOf(b) ||
                (m_state->dayOf(a) == m_state->dayOf(b) &&
                 (a.allDay > b.allDay || (a.allDay == b.allDay && a.start <= b.start))));
    }
}

void TestTui::hiddenCalendarsStayHidden()
{
    const QString calendar = m_state->events().first().calendarId;
    m_settings->setCalendarVisible(calendar, false);
    m_state->reload();
    for (const Event &e : m_state->events())
        QVERIFY(e.calendarId != calendar);
}

void TestTui::daysAreTheSettingsZones()
{
    // Los Angeles leaves daylight time early on November 1, so that day has
    // 25 hours. 07:30 UTC is 00:30 there, still the start of the day.
    m_settings->setTimeZoneId(u"America/Los_Angeles"_s);
    const QTimeZone la("America/Los_Angeles");
    const auto at = [&](int day, int hour, int minute) {
        return QDateTime(QDate(2026, 11, day), QTime(hour, minute), la);
    };
    const auto event = [](const QString &summary, const QDateTime &start, const QDateTime &end) {
        Event e;
        e.eventId = summary;
        e.summary = summary;
        e.start = start;
        e.end = end;
        return e;
    };
    FixedSource source({
        event(u"Late"_s, QDateTime(QDate(2026, 10, 31), QTime(23, 30), la), at(1, 0, 15)),
        event(u"Brunch"_s, at(1, 11, 0), at(1, 12, 0)),
        event(u"Night"_s, at(1, 23, 30), at(1, 23, 50)),
        event(u"Tomorrow"_s, at(2, 0, 30), at(2, 1, 0)),
    });
    const QDateTime now(QDate(2026, 11, 1), QTime(7, 30), QTimeZone::UTC);
    TuiState state(source, *m_settings, [now] { return now; });

    QCOMPARE(state.today(), QDate(2026, 11, 1));
    QStringList shown;
    for (const Event &e : state.events())
        shown.append(e.summary);
    QCOMPARE(shown, (QStringList{u"Late"_s, u"Brunch"_s, u"Night"_s}));
    QCOMPARE(state.dayOf(state.events().first()), QDate(2026, 11, 1));

    state.press(u"Tab"_s);
    QCOMPARE(state.view(), TuiState::View::Week);
    for (const Event &e : state.events()) {
        const QDate expected = e.summary == u"Tomorrow"_s ? QDate(2026, 11, 2)
                               : e.summary == u"Late"_s && state.rangeStart() < QDate(2026, 11, 1)
                                   ? QDate(2026, 10, 31)
                                   : QDate(2026, 11, 1);
        QCOMPARE(state.dayOf(e), expected);
    }
}

void TestTui::syncStatusRedraws()
{
    QSignalSpy redraws(m_state.get(), &TuiState::changed);
    Q_EMIT m_source->statusChanged();
    QCOMPARE(redraws.count(), 1);
}

void TestTui::drawsInTheTheme()
{
    const Palette palette = Palette::forTheme({});
    QString drawn = renderTui(*m_state, *m_settings, palette, 110, 30);
    QVERIFY2(drawn.contains(u"Today, Wednesday, October 7"_s), qPrintable(drawn));
    QVERIFY(drawn.contains(u"Dentist"_s));
    QVERIFY(drawn.contains(u"Day"_s) && drawn.contains(u"Week"_s) && drawn.contains(u"Agenda"_s));
    QVERIFY(!m_state->syncStatus().isEmpty());
    QVERIFY(drawn.contains(m_state->syncStatus()));
    QVERIFY(drawn.contains(u"▌"_s));

    m_state->press(u"Tab"_s);
    drawn = renderTui(*m_state, *m_settings, palette, 110, 30);
    QVERIFY2(drawn.contains(u"Wed 7"_s), qPrintable(drawn));
    m_state->press(u"?"_s);
    drawn = renderTui(*m_state, *m_settings, palette, 110, 30);
    QVERIFY(drawn.contains(u"Keys"_s));
    QVERIFY(drawn.contains(u"Agenda view"_s));
}

bool TestTui::select(const QString &summary)
{
    m_state->press(u"g"_s);
    m_state->press(u"g"_s);
    for (int i = 0; i < m_state->events().size(); ++i) {
        if (m_state->events().at(m_state->selected()).summary == summary)
            return true;
        m_state->press(u"j"_s);
    }
    return false;
}

void TestTui::detailsAnswerAndTakeBack()
{
    // The coming week's Berlin sync waits for an answer.
    m_state->press(m_settings->leaderKey());
    m_state->press(u"a"_s);
    QVERIFY(select(u"Berlin sync"_s));
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Details);
    QString drawn = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY2(drawn.contains(u"Not answered"_s), qPrintable(drawn));
    QVERIFY(drawn.contains(u"y yes"_s));

    // It repeats, so the answer is for this occurrence or all of them.
    m_state->press(u"y"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Confirming);
    m_state->press(u"t"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Details);
    QCOMPARE(m_state->current()->responseStatus, u"accepted"_s);
    QVERIFY2(m_state->message().contains(u"u takes it back"_s), qPrintable(m_state->message()));
    // Taken back from the details, as the message says.
    QVERIFY(m_state->press(u"u"_s));
    QCOMPARE(m_state->mode(), TuiState::Mode::Details);
    QCOMPARE(m_state->current()->responseStatus, u"needsAction"_s);
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    // Only an invitation can be answered.
    QVERIFY(select(u"Dentist"_s));
    m_state->press(u"Enter"_s);
    m_state->press(u"y"_s);
    QVERIFY(m_state->messageIsError());
}

void TestTui::decliningAHiddenOneClosesItsDetails()
{
    m_settings->setShowDeclined(false);
    m_state->press(m_settings->leaderKey());
    m_state->press(u"a"_s);
    QVERIFY(select(u"Berlin sync"_s));
    const QString declined = m_state->current()->eventId;
    const int shown = int(m_state->events().size());
    m_state->press(u"Enter"_s);
    m_state->press(u"n"_s);
    m_state->press(u"t"_s);
    // Declined, so no longer shown, and the details do not move on to another
    // event that a following x would delete.
    for (const Event &e : m_state->events())
        QVERIFY(e.eventId != declined);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    QVERIFY(!m_state->press(u"x"_s));
    QCOMPARE(int(m_state->events().size()), shown - 1);
}

void TestTui::deletingCanBeTakenBack()
{
    QVERIFY(select(u"Dentist"_s));
    m_state->press(u"Enter"_s);
    m_state->press(u"x"_s);
    m_state->press(u"y"_s);
    QVERIFY(!summaries().contains(u"Dentist"_s));
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    m_state->press(u"u"_s);
    QVERIFY(summaries().contains(u"Dentist"_s));
    // Nothing more to take back.
    m_state->press(u"u"_s);
    QVERIFY(m_state->messageIsError());
}

void TestTui::newEventsAsCallieAddReadsThem()
{
    m_state->press(u"n"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Typing);
    for (const QChar c : u"Pottery today 6-8pm"_s)
        m_state->press(QString(c));
    m_state->press(u"Backspace"_s);
    m_state->press(u"m"_s);
    QCOMPARE(m_state->input(), u"Pottery today 6-8pm"_s);
    const QString drawn = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY(drawn.contains(u"New event: Pottery today 6-8pm"_s));
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    QVERIFY2(summaries().contains(u"Pottery"_s), qPrintable(summaries().join(u", "_s)));
    QVERIFY(!m_state->messageIsError());

    // What callie add cannot read, such as a time with no title, it says so.
    m_state->press(u"n"_s);
    for (const QChar c : u"tomorrow 3pm"_s)
        m_state->press(QString(c));
    m_state->press(u"Enter"_s);
    QVERIFY(m_state->messageIsError());
    // Escape gives up on the line.
    m_state->press(u"n"_s);
    m_state->press(u"x"_s);
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
}

void TestTui::searchAndInvitationsJumpToTheDay()
{
    m_state->press(u"/"_s);
    for (const QChar c : u"climbing"_s)
        m_state->press(QString(c));
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Found);
    QVERIFY(!m_state->found().isEmpty());
    const Event climbing = m_state->found().at(m_state->foundSelected());
    QCOMPARE(climbing.summary, u"Climbing"_s);
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    QCOMPARE(m_state->view(), TuiState::View::Day);
    QCOMPARE(m_state->day(), climbing.start.date());
    QCOMPARE(m_state->current()->eventId, climbing.eventId);

    // A repeating event is found once, at its next occurrence.
    m_state->press(u"/"_s);
    for (const QChar c : u"standup"_s)
        m_state->press(QString(c));
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->found().size(), 1);
    QVERIFY(m_state->found().first().end > kNow);
    m_state->press(u"Escape"_s);

    m_state->press(u"i"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Found);
    for (const Event &e : m_state->found())
        QCOMPARE(e.responseStatus, u"needsAction"_s);
    QVERIFY(!m_state->found().isEmpty());
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
}

void TestTui::foundAsTheCliListsIt()
{
    // A repeating invitation, answered once, and where the user works, which
    // labels a day rather than being an event.
    QList<Event> events;
    for (int week = 0; week < 3; ++week) {
        Event e;
        e.calendarId = u"work"_s;
        e.eventId = u"planning-%1"_s.arg(week);
        e.seriesId = u"planning"_s;
        e.uid = u"planning"_s;
        e.summary = u"Planning"_s;
        e.start = kNow.addDays(1 + 7 * week);
        e.end = e.start.addSecs(3600);
        e.responseStatus = u"needsAction"_s;
        e.canRespond = true;
        events.append(e);
    }
    Event office;
    office.calendarId = u"work"_s;
    office.eventId = office.uid = u"office"_s;
    office.summary = u"Office"_s;
    office.start = kNow.addDays(1);
    office.end = office.start.addDays(1);
    office.allDay = office.workPlace = true;
    office.responseStatus = u"needsAction"_s;
    office.canRespond = true;
    events.append(office);
    FixedSource source(events);
    TuiState state(source, *m_settings, [] { return kNow; });

    state.press(u"i"_s);
    QCOMPARE(state.found().size(), 1);
    QCOMPARE(state.found().first().eventId, u"planning-0"_s);
    state.press(u"Escape"_s);

    for (const QString &key : {u"/"_s, u"o"_s, u"f"_s, u"f"_s, u"Enter"_s})
        state.press(key);
    QVERIFY(state.found().isEmpty());
    state.press(u"Escape"_s);
    for (const QString &key : {u"/"_s, u"p"_s, u"l"_s, u"a"_s, u"n"_s, u"Enter"_s})
        state.press(key);
    QCOMPARE(state.found().size(), 1);
}

void TestTui::foundKeepsItsCalendar()
{
    // One meeting on two calendars, with the same id in each.
    QList<Event> events;
    for (const QString &calendar : {u"work"_s, u"team"_s}) {
        Event e;
        e.calendarId = calendar;
        e.eventId = e.uid = u"review"_s;
        e.summary = u"Review"_s;
        e.start = kNow.addDays(2);
        e.end = e.start.addSecs(3600);
        events.append(e);
    }
    FixedSource source(events);
    TuiState state(source, *m_settings, [] { return kNow; });
    for (int pick = 0; pick < 2; ++pick) {
        for (const QString &key : {u"/"_s, u"r"_s, u"e"_s, u"v"_s, u"Enter"_s})
            state.press(key);
        QCOMPARE(state.found().size(), 2);
        state.press(u"g"_s);
        state.press(u"g"_s);
        if (pick == 1)
            state.press(u"j"_s);
        QCOMPARE(state.foundSelected(), pick);
        const QString calendar = state.found().at(pick).calendarId;
        state.press(u"Enter"_s);
        QCOMPARE(state.current()->calendarId, calendar);
    }
}

void TestTui::refusedChangesAreShown()
{
    Q_EMIT m_source->errorOccurred(u"Delete Dentist: not allowed"_s);
    QVERIFY(m_state->messageIsError());
    QCOMPARE(m_state->message(), u"Delete Dentist: not allowed"_s);
}

void TestTui::calendarsShowAndHide()
{
    m_state->press(u"c"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Calendars);
    const CalendarInfo first = m_state->calendars().first();
    QVERIFY(m_state->calendarShown(first));
    QString drawn = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY(drawn.contains(u"Calendars"_s) && drawn.contains(first.displayName));
    m_state->press(u" "_s);
    QVERIFY(!m_state->calendarShown(first));
    for (const Event &e : m_state->events())
        QVERIFY(e.calendarId != first.id);
    m_state->press(u" "_s);
    QVERIFY(m_state->calendarShown(first));
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
}

void TestTui::callsOpen()
{
    QSignalSpy opened(m_state.get(), &TuiState::openRequested);
    m_state->press(m_settings->leaderKey());
    m_state->press(u"a"_s);
    QVERIFY(select(u"Standup"_s));
    m_state->press(u"Enter"_s);
    m_state->press(u"o"_s);
    QCOMPARE(opened.size(), 1);
    QCOMPARE(opened.first().first().toUrl(), m_state->current()->joinUrl());
}

void TestTui::onlyWebCallsOpen()
{
    Event e;
    e.calendarId = u"work"_s;
    e.eventId = e.uid = u"call"_s;
    e.summary = u"Call"_s;
    e.start = kNow.addSecs(3600);
    e.end = e.start.addSecs(3600);
    e.conferenceUrl = QUrl(u"file:///home/sam/run.sh"_s);
    FixedSource source({e});
    TuiState state(source, *m_settings, [] { return kNow; });
    QSignalSpy opened(&state, &TuiState::openRequested);
    QCOMPARE(state.current()->summary, u"Call"_s);
    state.press(u"Enter"_s);
    state.press(u"o"_s);
    QCOMPARE(opened.size(), 0);
    QVERIFY(state.messageIsError());
}

void TestTui::retype(const QString &text)
{
    while (!m_state->input().isEmpty())
        m_state->press(u"Backspace"_s);
    for (const QChar c : text)
        m_state->press(QString(c));
}

std::optional<Event> TestTui::shown(const QString &summary) const
{
    for (const Event &e : m_state->events()) {
        if (e.summary == summary)
            return e;
    }
    return std::nullopt;
}

void TestTui::changesAnEventsFields()
{
    QVERIFY(select(u"Dentist"_s));
    m_state->press(u"e"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Editing);
    QCOMPARE(m_state->field(), TuiState::Field::Title);
    const QString drawn = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY2(drawn.contains(u"Where"_s) && drawn.contains(u"Clinic"_s), qPrintable(drawn));
    QVERIFY(drawn.contains(u"2026-10-07 16:00"_s));

    // A field is typed again from what it holds, and saved as callie edit saves it.
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Typing);
    QCOMPARE(m_state->input(), u"Dentist"_s);
    retype(u"Orthodontist"_s);
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Editing);
    QVERIFY2(shown(u"Orthodontist"_s), qPrintable(summaries().join(u", "_s)));
    QVERIFY(!m_state->messageIsError());

    // Its place, and its end, later the same day.
    m_state->press(u"j"_s);
    QCOMPARE(m_state->field(), TuiState::Field::Where);
    m_state->press(u"Enter"_s);
    retype(u"Smile Studio"_s);
    m_state->press(u"Enter"_s);
    QCOMPARE(shown(u"Orthodontist"_s)->location, u"Smile Studio"_s);
    m_state->press(u"j"_s);
    m_state->press(u"j"_s);
    QCOMPARE(m_state->field(), TuiState::Field::Ends);
    m_state->press(u"Enter"_s);
    retype(u"18:00"_s);
    m_state->press(u"Enter"_s);
    QCOMPARE(shown(u"Orthodontist"_s)->end,
             QDateTime(QDate(2026, 10, 7), QTime(18, 0), QTimeZone::UTC));

    // What cannot be read is said, and changes nothing.
    m_state->press(u"Enter"_s);
    retype(u"sometime"_s);
    m_state->press(u"Enter"_s);
    QVERIFY(m_state->messageIsError());
    QCOMPARE(shown(u"Orthodontist"_s)->end,
             QDateTime(QDate(2026, 10, 7), QTime(18, 0), QTimeZone::UTC));

    // Escape on a field keeps it, and goes back to the fields; u takes back the last change.
    m_state->press(u"Enter"_s);
    retype(u"19:00"_s);
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Editing);
    m_state->press(u"u"_s);
    QCOMPARE(shown(u"Orthodontist"_s)->end,
             QDateTime(QDate(2026, 10, 7), QTime(17, 0), QTimeZone::UTC));

    // All day switches with Enter or Space.
    m_state->press(u"j"_s);
    QCOMPARE(m_state->field(), TuiState::Field::AllDay);
    m_state->press(u" "_s);
    QVERIFY(shown(u"Orthodontist"_s)->allDay);
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
}

void TestTui::movesAnEventKeepingItsLength()
{
    QVERIFY(select(u"Dentist"_s));
    m_state->press(u"m"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Typing);
    const QString prompt = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY(prompt.contains(u"Move to:"_s));
    retype(u"tomorrow 9am"_s);
    m_state->press(u"Enter"_s);
    QVERIFY(!m_state->messageIsError());
    QVERIFY(!shown(u"Dentist"_s));
    m_state->press(u"l"_s);
    const std::optional<Event> moved = shown(u"Dentist"_s);
    QVERIFY2(moved, qPrintable(summaries().join(u", "_s)));
    QCOMPARE(moved->start, QDateTime(QDate(2026, 10, 8), QTime(9, 0), QTimeZone::UTC));
    QCOMPARE(moved->start.secsTo(moved->end), 3600);
}

void TestTui::copiesAndDuplicatesEvents()
{
    // p with nothing copied says what y does.
    m_state->press(u"p"_s);
    QVERIFY(m_state->messageIsError());

    // Copied, then put on the day of the event selected there, at its own time.
    QVERIFY(select(u"Dentist"_s));
    m_state->press(u"y"_s);
    QVERIFY(!m_state->messageIsError());
    m_state->press(u"l"_s);
    QVERIFY(!shown(u"Dentist"_s));
    m_state->press(u"p"_s);
    QVERIFY(!m_state->messageIsError());
    const std::optional<Event> copy = shown(u"Dentist"_s);
    QVERIFY2(copy, qPrintable(summaries().join(u", "_s)));
    QCOMPARE(copy->start, QDateTime(QDate(2026, 10, 8), QTime(16, 0), QTimeZone::UTC));

    // D duplicates the selected event where it is.
    QVERIFY(select(u"Standup"_s));
    const qsizetype before = summaries().count(u"Standup"_s);
    m_state->press(u"D"_s);
    QCOMPARE(summaries().count(u"Standup"_s), before + 1);
}

void TestTui::onlyChangesWhatCanBeChanged()
{
    Event e;
    e.eventId = u"read-only"_s;
    e.calendarId = u"shared"_s;
    e.summary = u"Board meeting"_s;
    e.start = QDateTime(QDate(2026, 10, 7), QTime(15, 0), QTimeZone::UTC);
    e.end = e.start.addSecs(3600);
    FixedSource source({e});
    TuiState state(source, *m_settings, [] { return kNow; });
    for (const QString &key : {u"e"_s, u"m"_s}) {
        state.press(key);
        QCOMPARE(state.mode(), TuiState::Mode::Browse);
        QVERIFY(state.messageIsError());
    }
    state.press(u"Enter"_s);
    state.press(u"e"_s);
    QCOMPARE(state.mode(), TuiState::Mode::Details);
    QVERIFY(state.messageIsError());
}

void TestTui::anEventMovedOutOfViewClosesItsFields()
{
    // From its details, a new start in another week takes the event out of what is shown; the
    // fields close, rather than going back to the details of whatever is selected then.
    QVERIFY(select(u"Dentist"_s));
    m_state->press(u"Enter"_s);
    m_state->press(u"e"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Editing);
    m_state->press(u"j"_s);
    m_state->press(u"j"_s);
    QCOMPARE(m_state->field(), TuiState::Field::Starts);
    m_state->press(u"Enter"_s);
    retype(u"2026-11-20 10:00"_s);
    m_state->press(u"Enter"_s);
    QVERIFY(!m_state->messageIsError());
    QVERIFY(!shown(u"Dentist"_s));
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    const QStringList before = summaries();
    m_state->press(u"x"_s);
    QCOMPARE(summaries(), before);
}

void TestTui::aRepeatingEventIsDeletedOnceOrWhole()
{
    // The vendor call repeats every Wednesday, as one series.
    const auto calls = [this] {
        int found = 0;
        for (const Event &e :
             m_queue->eventsBetween(kNow.addDays(-1), kNow.addDays(14), QTimeZone::UTC))
            found += e.summary == u"Vendor call"_s;
        return found;
    };
    const int before = calls();
    QVERIFY(before >= 2);
    QVERIFY(select(u"Vendor call"_s));
    m_state->press(u"Enter"_s);
    m_state->press(u"x"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Confirming);
    const QString drawn = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY2(drawn.contains(u"t this event"_s) && drawn.contains(u"a all events"_s),
             qPrintable(drawn));

    // Escape keeps it, and goes back to its details.
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Details);
    QCOMPARE(calls(), before);

    // t deletes this one alone, a every one of them.
    m_state->press(u"x"_s);
    m_state->press(u"t"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    QCOMPARE(calls(), before - 1);
    m_state->press(u"u"_s);
    QCOMPARE(calls(), before);
    QVERIFY(select(u"Vendor call"_s));
    m_state->press(u"Enter"_s);
    m_state->press(u"x"_s);
    m_state->press(u"a"_s);
    QCOMPARE(calls(), 0);
}

void TestTui::aRepeatingInvitationIsAnsweredOnceOrWhole()
{
    m_state->press(m_settings->leaderKey());
    m_state->press(u"a"_s);
    QVERIFY(select(u"Berlin sync"_s));
    const QString series = m_state->current()->seriesId;
    m_state->press(u"Enter"_s);
    m_state->press(u"y"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Confirming);
    m_state->press(u"a"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Details);
    // Every occurrence of the series has the answer.
    int answered = 0;
    for (const Event &e : m_queue->eventsBetween(kNow, kNow.addDays(28), QTimeZone::UTC)) {
        if (e.seriesId == series) {
            QCOMPARE(e.responseStatus, u"accepted"_s);
            ++answered;
        }
    }
    QVERIFY(answered >= 2);
}

void TestTui::aOneOffEventIsDeletedOnceConfirmed()
{
    m_state->press(u"n"_s);
    for (const QChar c : u"Pottery today 6-8pm"_s)
        m_state->press(QString(c));
    m_state->press(u"Enter"_s);
    QVERIFY(select(u"Pottery"_s));
    m_state->press(u"Enter"_s);
    m_state->press(u"x"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Confirming);
    QVERIFY(!m_state->askingScope());
    QVERIFY(m_state->question().contains(u"Pottery"_s));
    // Enter, which opens and closes details, does not delete; n keeps it, y deletes it.
    QVERIFY(!m_state->press(u"Enter"_s));
    QCOMPARE(m_state->mode(), TuiState::Mode::Confirming);
    m_state->press(u"n"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Details);
    QVERIFY(summaries().contains(u"Pottery"_s));
    m_state->press(u"x"_s);
    m_state->press(u"y"_s);
    QVERIFY(!summaries().contains(u"Pottery"_s));
}

void TestTui::sharesTheAppsKeys()
{
    // The app's leader keys, with the leader Settings chooses: invitations after it, as in the
    // app, and the view keys.
    m_settings->setLeaderKey(u":"_s);
    m_state->press(u":"_s);
    QCOMPARE(m_state->pending(), u"leader"_s);
    m_state->press(u"i"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Found);
    QCOMPARE(m_state->foundTitle(), u"Invitations"_s);
    m_state->press(u"Escape"_s);
    m_state->press(u":"_s);
    m_state->press(u"w"_s);
    QCOMPARE(m_state->view(), TuiState::View::Week);
    // i alone, as before, still lists them.
    m_state->press(u"i"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Found);
    m_state->press(u"Escape"_s);

    // The help lists the table's keys, with that leader, the details' beside the list's.
    // On a standard terminal, all of it fits.
    m_state->press(u"?"_s);
    const QString drawn = renderTui(*m_state, *m_settings, Palette::forTheme({}), 80, 24);
    for (const QString &shown :
         {u": i"_s, u": w"_s, u"Next event"_s, u"Quit"_s, u"Duplicate the event"_s, u"Answer yes"_s,
          u"Delete the event"_s, u"Change the event"_s})
        QVERIFY2(drawn.contains(shown), qPrintable(shown + u"\n"_s + drawn));
}

void TestTui::remindersShowInTheFooter()
{
    // A reminder says so in the footer, and its click shows the event on its day.
    Event dentist;
    for (const Event &e : m_state->events()) {
        if (e.summary == u"Dentist"_s)
            dentist = e;
    }
    QVERIFY(!dentist.eventId.isEmpty());
    m_state->remind(dentist);
    QVERIFY2(m_state->message().startsWith(u"Reminder: Dentist, "_s),
             qPrintable(m_state->message()));
    QVERIFY(!m_state->messageIsError());
    m_state->press(u"l"_s);
    m_state->showEvent(dentist);
    QCOMPARE(m_state->day(), QDate(2026, 10, 7));
    QCOMPARE(selected(), u"Dentist"_s);
}

void TestTui::editsTheLineBeingTyped()
{
    m_state->press(u"n"_s);
    retype(u"Lunch tomorrow"_s);
    // Home, and typing there; the cursor moves with the arrows, and Delete takes what is under it.
    m_state->press(u"Home"_s);
    QCOMPARE(m_state->cursor(), 0);
    for (const QChar c : u"Big "_s)
        m_state->press(QString(c));
    QCOMPARE(m_state->input(), u"Big Lunch tomorrow"_s);
    m_state->press(u"Right"_s);
    m_state->press(u"Delete"_s);
    QCOMPARE(m_state->input(), u"Big Lnch tomorrow"_s);
    m_state->press(u"Left"_s);
    m_state->press(u"Backspace"_s);
    QCOMPARE(m_state->input(), u"BigLnch tomorrow"_s);
    // Ctrl-W takes the word before the cursor, Ctrl-U the line before it, as in a shell.
    m_state->press(u"Ctrl+E"_s);
    m_state->press(u"Ctrl+W"_s);
    QCOMPARE(m_state->input(), u"BigLnch "_s);
    m_state->press(u"Ctrl+A"_s);
    m_state->press(u"Ctrl+F"_s);
    m_state->press(u"Ctrl+U"_s);
    QCOMPARE(m_state->input(), u"igLnch "_s);
    QCOMPARE(m_state->cursor(), 0);
    m_state->press(u"Escape"_s);
}

void TestTui::recallsEarlierLines()
{
    m_state->press(u"/"_s);
    retype(u"dentist"_s);
    m_state->press(u"Enter"_s);
    m_state->press(u"Escape"_s);
    m_state->press(u"/"_s);
    retype(u"lunch"_s);
    m_state->press(u"Enter"_s);
    m_state->press(u"Escape"_s);
    // Up goes back through the searches, newest first; Down comes back to the line being typed.
    m_state->press(u"/"_s);
    retype(u"ret"_s);
    m_state->press(u"Up"_s);
    QCOMPARE(m_state->input(), u"lunch"_s);
    m_state->press(u"Up"_s);
    QCOMPARE(m_state->input(), u"dentist"_s);
    m_state->press(u"Down"_s);
    m_state->press(u"Down"_s);
    QCOMPARE(m_state->input(), u"ret"_s);
    m_state->press(u"Escape"_s);
    // New events have their own, and so does each field of an event.
    m_state->press(u"n"_s);
    m_state->press(u"Up"_s);
    QCOMPARE(m_state->input(), QString());
    m_state->press(u"Escape"_s);
    QVERIFY(select(u"Dentist"_s));
    m_state->press(u"e"_s);
    m_state->press(u"Enter"_s);
    retype(u"Orthodontist"_s);
    m_state->press(u"Enter"_s);
    m_state->press(u"j"_s);
    m_state->press(u"Enter"_s);
    const QString where = m_state->input();
    m_state->press(u"Up"_s);
    QCOMPARE(m_state->input(), where);
    m_state->press(u"Escape"_s);
    m_state->press(u"k"_s);
    m_state->press(u"Enter"_s);
    m_state->press(u"Up"_s);
    QCOMPARE(m_state->input(), u"Orthodontist"_s);
    m_state->press(u"Escape"_s);
    m_state->press(u"Escape"_s);
}

void TestTui::searchesAsItIsTyped()
{
    // Until something is typed, the view stays as it was.
    m_state->press(u"/"_s);
    const QString empty = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY2(!empty.contains(u"Nothing found."_s) && empty.contains(u"Dentist"_s),
             qPrintable(empty));
    for (const QChar c : u"dent"_s)
        m_state->press(QString(c));
    QCOMPARE(m_state->mode(), TuiState::Mode::Typing);
    QVERIFY(!m_state->found().isEmpty());
    for (const Event &e : m_state->found())
        QVERIFY(e.summary.contains(u"Dent"_s, Qt::CaseInsensitive));
    const QString drawn = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY2(drawn.contains(u"Dentist"_s), qPrintable(drawn));
    // Enter keeps them, to go to one.
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Found);
}

void TestTui::completesCalendarNames()
{
    QVERIFY(select(u"Dentist"_s));
    m_state->press(u"e"_s);
    while (m_state->field() != TuiState::Field::Calendar)
        m_state->press(u"j"_s);
    m_state->press(u"Enter"_s);
    retype(u"wo"_s);
    m_state->press(u"Tab"_s);
    QCOMPARE(m_state->input(), u"Work"_s);
    // Tab again goes on to the next that matches, and back round.
    retype(u""_s);
    m_state->press(u"Tab"_s);
    const QString first = m_state->input();
    m_state->press(u"Tab"_s);
    QVERIFY(m_state->input() != first);
    m_state->press(u"Escape"_s);
}

void TestTui::runsCommandsAfterAColon()
{
    m_state->press(u":"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Typing);
    QCOMPARE(m_state->typing(), TuiState::Typing::Command);
    retype(u"week"_s);
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->view(), TuiState::View::Week);
    m_state->press(u":"_s);
    retype(u"add Pottery today 6-8pm"_s);
    m_state->press(u"Enter"_s);
    QVERIFY(summaries().contains(u"Pottery"_s));
    m_state->press(u":"_s);
    retype(u"dance"_s);
    m_state->press(u"Enter"_s);
    QVERIFY(m_state->messageIsError());
    // With : as the leader, Space opens the command line instead.
    m_settings->setLeaderKey(u":"_s);
    m_state->press(u" "_s);
    QCOMPARE(m_state->typing(), TuiState::Typing::Command);
    retype(u"q"_s);
    m_state->press(u"Enter"_s);
    QVERIFY(m_state->quitRequested());
}

void TestTui::goesToADate()
{
    // gd asks for a day, as callie add reads one; :go does the same.
    m_state->press(u"g"_s);
    m_state->press(u"d"_s);
    QCOMPARE(m_state->typing(), TuiState::Typing::Date);
    retype(u"2026-12-25"_s);
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    QCOMPARE(m_state->day(), QDate(2026, 12, 25));
    m_state->press(u":"_s);
    retype(u"go friday"_s);
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->day().dayOfWeek(), int(Qt::Friday));
    m_state->press(u":"_s);
    retype(u"go someday"_s);
    m_state->press(u"Enter"_s);
    QVERIFY(m_state->messageIsError());
}

void TestTui::countsRepeatMoves()
{
    // 3l goes three days on; the count shows until the move takes it.
    m_state->press(u"3"_s);
    QCOMPARE(m_state->count(), 3);
    const QString drawn = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY(drawn.contains(u" 3 "_s));
    m_state->press(u"l"_s);
    QCOMPARE(m_state->day(), QDate(2026, 10, 10));
    QCOMPARE(m_state->count(), 0);
    // 1 0 h is ten days back; a count before anything else is dropped.
    m_state->press(u"1"_s);
    m_state->press(u"0"_s);
    m_state->press(u"h"_s);
    QCOMPARE(m_state->day(), QDate(2026, 9, 30));
    m_state->press(u"t"_s);
    m_state->press(u"2"_s);
    m_state->press(u"?"_s);
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->count(), 0);
}

void TestTui::movesHalfAScreen()
{
    // Ctrl-D and Ctrl-U move by half the rows the terminal shows.
    m_state->press(m_settings->leaderKey());
    m_state->press(u"a"_s);
    m_state->setVisibleRows(6);
    m_state->press(u"g"_s);
    m_state->press(u"g"_s);
    QCOMPARE(m_state->selected(), 0);
    m_state->press(u"Ctrl+D"_s);
    QCOMPARE(m_state->selected(), 3);
    m_state->press(u"2"_s);
    m_state->press(u"Ctrl+D"_s);
    QCOMPARE(m_state->selected(), 9);
    m_state->press(u"Ctrl+U"_s);
    QCOMPARE(m_state->selected(), 6);
}

void TestTui::setsTheAgendasLength()
{
    m_state->press(u":"_s);
    retype(u"agenda 30"_s);
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->view(), TuiState::View::Agenda);
    QCOMPARE(m_state->rangeDays(), 30);
    m_state->press(u":"_s);
    retype(u"agenda 0"_s);
    m_state->press(u"Enter"_s);
    QVERIFY(m_state->messageIsError());
    QCOMPARE(m_state->rangeDays(), 30);
}

QTEST_GUILESS_MAIN(TestTui)
#include "tst_tui.moc"
