#include "TuiState.h"

#include "Commands.h"

#include "callie/KeyActions.h"
#include "callie/QueuedSource.h"
#include "callie/QuickAdd.h"
#include "callie/ReminderNotifier.h"
#include "callie/Repeat.h"
#include "callie/Settings.h"
#include "callie/Times.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocumentFragment>
#include <QTextStream>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace callie::cli {

namespace {

/// The terminal's action for `keys`: its vi keys, or the key after the leader, in the list or
/// in an event's details.
QString actionFor(const QString &keys, bool afterLeader, bool inDetails)
{
    for (const KeyAction &action : keyActionsFor(KeyAction::Where::Terminal)) {
        if (action.inCard == inDetails && (afterLeader ? action.leader : action.vi) == keys)
            return action.id;
    }
    return {};
}

/// Whether `key` starts a two-key command, as g starts gg.
bool startsCommand(const QString &key)
{
    if (key.size() != 1)
        return false;
    for (const KeyAction &action : keyActionsFor(KeyAction::Where::Terminal)) {
        if (!action.inCard && action.vi.size() == 2 && action.vi.startsWith(key))
            return true;
    }
    return false;
}

/// Keys the terminal takes besides the table's: the arrows, Home and End, and i and r, which it
/// had before it shared the app's leader keys.
QString aliasFor(const QString &key)
{
    static const QHash<QString, QString> aliases = {
        {u"Left"_s, u"previous"_s},  {u"Right"_s, u"next"_s},       {u"Home"_s, u"today"_s},
        {u"Down"_s, u"nextEvent"_s}, {u"Up"_s, u"previousEvent"_s}, {u"End"_s, u"lastEvent"_s},
        {u"i"_s, u"invites"_s},      {u"r"_s, u"refresh"_s},
    };
    return aliases.value(key);
}

} // namespace

TuiState::TuiState(CalendarSource &source, Settings &settings, Now now, QObject *parent)
    : QObject(parent), m_source(&source), m_settings(settings), m_now(std::move(now))
{
    m_day = today();
    connect(m_source, &CalendarSource::changed, this, &TuiState::reload);
    // A sync that finds nothing new, or fails, says so only here.
    connect(m_source, &CalendarSource::statusChanged, this, &TuiState::changed);
    connect(&m_settings, &Settings::hiddenCalendarsChanged, this, &TuiState::reload);
    connect(&m_settings, &Settings::terminalGridChanged, this, &TuiState::changed);
    connect(m_source, &CalendarSource::errorOccurred, this,
            [this](const QString &message) { say(message, true); });
    // Each change can be taken back while it is held.
    connect(m_source, &CalendarSource::changeMade, this,
            [this](const QString &id, const QString &what) {
                m_lastChange = id;
                say(tr("%1. u takes it back.").arg(what));
            });
    reload();
    selectFirstOf(m_day);
}

QDate TuiState::today() const
{
    return m_settings.times()->date(m_now()).date();
}

bool TuiState::grid() const
{
    return m_settings.terminalGrid() && (m_view == View::Day || m_view == View::Week);
}

void TuiState::stepDay(int direction)
{
    if (m_selected < 0)
        return;
    const Event &from = m_events.at(m_selected);
    const int minutes = startMinute(from);
    const QDate last = rangeStart().addDays(rangeDays() - 1);
    for (QDate day = dayOf(from).addDays(direction); day >= rangeStart() && day <= last;
         day = day.addDays(direction)) {
        int best = -1;
        for (int i = 0; i < m_events.size(); ++i) {
            if (dayOf(m_events.at(i)) != day)
                continue;
            const int distance = std::abs(startMinute(m_events.at(i)) - minutes);
            if (best < 0 || distance < std::abs(startMinute(m_events.at(best)) - minutes))
                best = i;
        }
        if (best >= 0) {
            m_selected = best;
            return;
        }
    }
}

QDate TuiState::rangeStart() const
{
    switch (m_view) {
    case View::Day:
    case View::Agenda: return m_day;
    case View::Week: {
        const int back = (m_day.dayOfWeek() - m_settings.firstDayOfWeek() + 7) % 7;
        return m_day.addDays(-back);
    }
    case View::Month: {
        // From the start of the week the month's first day is in.
        const QDate first(m_day.year(), m_day.month(), 1);
        return first.addDays(-((first.dayOfWeek() - m_settings.firstDayOfWeek() + 7) % 7));
    }
    }
    return m_day;
}

int TuiState::rangeDays() const
{
    switch (m_view) {
    case View::Day: return 1;
    case View::Week: return 7;
    case View::Agenda: return m_agendaDays;
    case View::Month: {
        // Whole weeks, to the end of the week the month's last day is in.
        const QDate last(m_day.year(), m_day.month(), m_day.daysInMonth());
        return int((rangeStart().daysTo(last) / 7 + 1) * 7);
    }
    }
    return 1;
}

int TuiState::startMinute(const Event &event) const
{
    const Times &times = *m_settings.times();
    return times.date(event.start).date() < dayOf(event) ? 0 : times.minutesIntoDay(event.start);
}

QDate TuiState::dayOf(const Event &event) const
{
    const QDate first = m_settings.times()->date(event.start).date();
    return std::max(first, rangeStart());
}

void TuiState::reload()
{
    const std::optional<Event> was =
        m_selected >= 0 ? std::optional(m_events.at(m_selected)) : std::nullopt;
    const QDate start = rangeStart();
    const QDateTime from = m_settings.times()->at(start.startOfDay(), 0);
    const QDateTime to = m_settings.times()->at(start.addDays(rangeDays()).startOfDay(), 0);
    const QStringList hidden = m_settings.hiddenCalendars();
    QList<Event> events =
        m_source ? m_source->eventsBetween(from, to, m_settings.times()->zone()) : QList<Event>();
    events.removeIf([&](const Event &e) {
        return e.workPlace || hidden.contains(e.calendarId) ||
               (e.declined && !m_settings.showDeclined());
    });
    std::sort(events.begin(), events.end(), [this](const Event &a, const Event &b) {
        const QDate dayA = dayOf(a), dayB = dayOf(b);
        if (dayA != dayB)
            return dayA < dayB;
        if (a.allDay != b.allDay)
            return a.allDay;
        return a.start != b.start ? a.start < b.start : a.summary < b.summary;
    });
    m_events = events;
    // The same event stays selected when it is still shown.
    m_selected = -1;
    if (was) {
        for (int i = 0; i < m_events.size(); ++i) {
            if (m_events.at(i).eventId == was->eventId &&
                m_events.at(i).calendarId == was->calendarId)
                m_selected = i;
        }
    }
    // Details are of one event; once it is no longer shown, such as after
    // declining it with declined events hidden, they close rather than move on.
    if (m_selected < 0 && (m_mode == Mode::Details || m_mode == Mode::Confirming))
        m_mode = Mode::Browse;
    // The fields too, as when a new start moves the event out of what is shown, so no key that
    // follows acts on another event.
    const bool fields =
        m_mode == Mode::Editing || (m_mode == Mode::Typing && m_typing == Typing::Field);
    if (fields && !editing())
        m_mode = Mode::Browse;
    if (m_selected < 0 && !m_events.isEmpty())
        m_selected = 0;
    Q_EMIT changed();
}

void TuiState::selectFirstOf(QDate day)
{
    m_selected = m_events.isEmpty() ? -1 : 0;
    // Today, the next event still to come; another day, its first.
    const QDateTime now = m_now();
    for (int i = 0; i < m_events.size(); ++i) {
        const Event &e = m_events.at(i);
        if (dayOf(e) < day)
            continue;
        if (day == today() && dayOf(e) == day && e.end <= now)
            continue;
        m_selected = i;
        break;
    }
    Q_EMIT changed();
}

void TuiState::step(int direction)
{
    m_day = m_view == View::Month ? m_day.addMonths(direction)
                                  : m_day.addDays(direction * (m_view == View::Day ? 1 : 7));
    reload();
    // A month starts at its first day, not the days before it from the week before.
    selectFirstOf(m_view == View::Day     ? m_day
                  : m_view == View::Month ? QDate(m_day.year(), m_day.month(), 1)
                                          : rangeStart());
}

void TuiState::setView(View view)
{
    m_view = view;
    reload();
    selectFirstOf(m_day);
}

bool TuiState::press(const QString &key)
{
    if (m_help) {
        if (key == u"?" || key == u"Escape" || key == u"q") {
            m_help = false;
            Q_EMIT changed();
            return true;
        }
        return false;
    }
    switch (m_mode) {
    case Mode::Confirming: return pressConfirming(key);
    case Mode::Editing: return pressEditing(key);
    case Mode::Typing: return pressTyping(key);
    case Mode::Details: return pressDetails(key);
    case Mode::Found: return pressFound(key);
    case Mode::Calendars: return pressCalendars(key);
    case Mode::Browse: break;
    }
    return pressBrowsing(key);
}

bool TuiState::pressBrowsing(const QString &key)
{
    // A count, as 3 in 3j, for the move that follows; 0 only after another digit.
    if (m_pending.isEmpty() && key.size() == 1 && key.at(0).isDigit() &&
        (key != u"0" || m_count > 0)) {
        m_count = std::min(m_count * 10 + key.at(0).digitValue(), 999);
        Q_EMIT changed();
        return true;
    }
    // Any other key takes the count: a move repeats with it, and anything else drops it.
    const int count = std::exchange(m_count, 0);
    const auto runCounted = [this, count](const QString &id) {
        m_repeat = count;
        return run(id);
    };
    // The second key of a command.
    const QString pending = std::exchange(m_pending, QString());
    if (pending == u"leader") {
        const QString id = actionFor(key, true, false);
        if (id.isEmpty()) {
            Q_EMIT changed();
            return true;
        }
        return runCounted(id);
    }
    if (!pending.isEmpty()) {
        const QString id = actionFor(pending + key, false, false);
        if (!id.isEmpty())
            return runCounted(id);
        // Not a command, so the key is taken on its own.
        Q_EMIT changed();
    }

    if (key == (m_settings.leaderKey() == u":" ? u" "_s : u":"_s)) {
        startTyping(Typing::Command);
        return true;
    }
    if (key == m_settings.leaderKey() || startsCommand(key)) {
        // The count waits for the command's second key.
        m_count = count;
        m_pending = key == m_settings.leaderKey() ? u"leader"_s : key;
        Q_EMIT changed();
        return true;
    }
    QString id = actionFor(key, false, false);
    if (id.isEmpty())
        id = aliasFor(key);
    if (id.isEmpty()) {
        if (count > 0)
            Q_EMIT changed();
        return false;
    }
    return runCounted(id);
}

bool TuiState::run(const QString &id)
{
    // The count pressBrowsing() hands on, for a move to repeat.
    const int times = std::max(1, std::exchange(m_repeat, 0));
    if (id == u"halfDown" || id == u"halfUp") {
        const int rows = std::max(1, m_visibleRows / 2) * (id == u"halfDown" ? 1 : -1);
        if (!m_events.isEmpty())
            m_selected = std::clamp(m_selected + rows * times, 0, int(m_events.size()) - 1);
        Q_EMIT changed();
        return true;
    }
    if (times > 1 &&
        (id == u"previous" || id == u"next" || id == u"nextEvent" || id == u"previousEvent")) {
        for (int i = 0; i < times; ++i)
            run(id);
        return true;
    }
    if (id == u"goToDate") {
        startTyping(Typing::Date);
        return true;
    }
    if (id == u"previous" || id == u"next") {
        step(id == u"next" ? 1 : -1);
        return true;
    }
    if (id == u"today") {
        m_day = today();
        reload();
        selectFirstOf(m_day);
        return true;
    }
    if (id == u"dayView" || id == u"weekView" || id == u"monthView" || id == u"agendaView") {
        setView(id == u"dayView"     ? View::Day
                : id == u"weekView"  ? View::Week
                : id == u"monthView" ? View::Month
                                     : View::Agenda);
        return true;
    }
    if (id == u"nextView") {
        setView(m_view == View::Day     ? View::Week
                : m_view == View::Week  ? View::Month
                : m_view == View::Month ? View::Agenda
                                        : View::Day);
        return true;
    }
    if (id == u"invites") {
        showFound(tr("Invitations"),
                  ask([](const Commands &commands) { return commands.waitingInvites(); }));
        return true;
    }
    if (id == u"change") {
        const std::optional<Event> event = current();
        return event && startEditing(*event);
    }
    if (id == u"copyEvent") {
        const std::optional<Event> event = current();
        if (!event)
            return false;
        m_copied = event;
        say(tr("Copied %1. p puts a copy on the day of the selected event.").arg(event->summary));
        return true;
    }
    if (id == u"pasteEvent") {
        if (!m_copied) {
            say(tr("Nothing copied; y copies the selected event."), true);
            return true;
        }
        paste();
        return true;
    }
    if (id == u"duplicateEvent") {
        const std::optional<Event> event = current();
        if (!event)
            return false;
        const QString ref = Commands::reference(*event);
        act([ref](Commands &commands, const std::function<void(int)> &done) {
            commands.duplicate(ref, std::nullopt, done);
        });
        return true;
    }
    if (id == u"undo") {
        if (m_lastChange.isEmpty() || !m_source->undoChange(m_lastChange)) {
            say(tr("Nothing to take back."), true);
            return true;
        }
        m_lastChange.clear();
        say(tr("Taken back."));
        return true;
    }

    if (id == u"nextEvent" || id == u"previousEvent") {
        const int step = id == u"nextEvent" ? 1 : -1;
        const int next = m_selected + step;
        // In the week's grid, j and k stay within the day; H and L go between days.
        const bool sameDay = grid() && m_view == View::Week && m_selected >= 0;
        if (next >= 0 && next < m_events.size() &&
            (!sameDay || dayOf(m_events.at(next)) == dayOf(m_events.at(m_selected))))
            m_selected = next;
    } else if (id == u"nextDay" || id == u"previousDay") {
        stepDay(id == u"nextDay" ? 1 : -1);
    } else if (id == u"toggleGrid") {
        m_settings.setTerminalGrid(!m_settings.terminalGrid());
        say(m_settings.terminalGrid() ? tr("Days and weeks as an hour grid.")
                                      : tr("Days and weeks as lists."));
        return true;
    } else if (id == u"firstEvent") {
        if (!m_events.isEmpty())
            m_selected = 0;
    } else if (id == u"lastEvent") {
        m_selected = int(m_events.size()) - 1;
    } else if (id == u"details") {
        if (m_selected < 0)
            return false;
        m_mode = Mode::Details;
    } else if (id == u"newEvent" || id == u"search") {
        startTyping(id == u"newEvent" ? Typing::NewEvent : Typing::Search);
    } else if (id == u"calendars") {
        m_mode = Mode::Calendars;
        m_calendarSelected = 0;
    } else if (id == u"move") {
        const std::optional<Event> event = current();
        if (!event)
            return false;
        if (!event->canEdit) {
            say(tr("%1 cannot be changed from here.").arg(event->summary), true);
            return true;
        }
        startTyping(Typing::Move);
    } else if (id == u"refresh") {
        m_source->refresh();
    } else if (id == u"help") {
        m_help = true;
    } else if (id == u"commandLine") {
        startTyping(Typing::Command);
        return true;
    } else if (id == u"quitTerminal") {
        m_quit = true;
    } else {
        return false;
    }
    Q_EMIT changed();
    return true;
}

bool TuiState::pressDetails(const QString &key)
{
    const std::optional<Event> event = current();
    if (!event || key == u"Escape" || key == u"q" || key == u"Enter") {
        m_mode = Mode::Browse;
        Q_EMIT changed();
        return true;
    }
    const QString id = actionFor(key, false, true);
    static const QHash<QString, QString> answers = {
        {u"accept"_s, u"yes"_s}, {u"maybe"_s, u"maybe"_s}, {u"decline"_s, u"no"_s}};
    if (answers.contains(id)) {
        if (!event->canRespond) {
            say(tr("%1 is not an invitation.").arg(event->summary), true);
            return true;
        }
        // One that repeats is answered for this occurrence or the series, as asked.
        if (repeats(*event)) {
            confirm(*event, answers.value(id));
            return true;
        }
        const QString ref = Commands::reference(*event);
        const QString answer = answers.value(id);
        act([ref, answer](Commands &commands, const std::function<void(int)> &done) {
            commands.respond(ref, answer, {}, done);
        });
        return true;
    }
    if (id == u"changeOpen")
        return startEditing(*event);
    // The help lists the details' own keys here.
    if (key == u"?") {
        m_help = true;
        Q_EMIT changed();
        return true;
    }
    if (id == u"delete") {
        if (!event->canEdit) {
            say(tr("%1 cannot be changed from here.").arg(event->summary), true);
            return true;
        }
        confirm(*event, {});
        return true;
    }
    if (id == u"join") {
        const QUrl url = event->joinUrl();
        if (url.isEmpty()) {
            say(tr("%1 has no call to join.").arg(event->summary), true);
            return true;
        }
        Q_EMIT openRequested(url);
        return true;
    }
    // Moving between events and taking a change back work from the details as from the list.
    const QString listed =
        !actionFor(key, false, false).isEmpty() ? actionFor(key, false, false) : aliasFor(key);
    if (listed == u"nextEvent" || listed == u"previousEvent" || listed == u"undo")
        return run(listed);
    return false;
}

bool TuiState::pressTyping(const QString &key)
{
    const auto wordStart = [this] {
        int at = m_cursor;
        while (at > 0 && m_input.at(at - 1).isSpace())
            --at;
        while (at > 0 && !m_input.at(at - 1).isSpace())
            --at;
        return at;
    };
    const bool tab = key == u"Tab";
    if (!tab)
        m_completing.reset();
    // Each field keeps its own, apart from new events, searches and the rest.
    QStringList &history =
        m_history[m_typing == Typing::Field ? 100 + int(field()) : int(m_typing)];

    if (key == u"Escape") {
        // A field given up on goes back to the fields.
        m_mode = m_typing == Typing::Field ? Mode::Editing : Mode::Browse;
    } else if (key == u"Enter") {
        const QString line = m_input.trimmed();
        if (!line.isEmpty() && (history.isEmpty() || history.last() != line))
            history.append(line);
        submit();
        return true;
    } else if (key == u"Left" || key == u"Ctrl+B") {
        m_cursor = std::max(0, m_cursor - 1);
    } else if (key == u"Right" || key == u"Ctrl+F") {
        m_cursor = std::min(int(m_input.size()), m_cursor + 1);
    } else if (key == u"Home" || key == u"Ctrl+A") {
        m_cursor = 0;
    } else if (key == u"End" || key == u"Ctrl+E") {
        m_cursor = int(m_input.size());
    } else if (key == u"Backspace") {
        if (m_cursor > 0)
            m_input.remove(--m_cursor, 1);
    } else if (key == u"Delete") {
        m_input.remove(m_cursor, 1);
    } else if (key == u"Ctrl+W") {
        const int from = wordStart();
        m_input.remove(from, m_cursor - from);
        m_cursor = from;
    } else if (key == u"Ctrl+U") {
        m_input.remove(0, m_cursor);
        m_cursor = 0;
    } else if (key == u"Up" || key == u"Down") {
        // Back through what was typed before for the same thing, and forward to the line
        // being typed.
        if (history.isEmpty())
            return true;
        if (m_historyAt < 0)
            m_draft = m_input;
        if (key == u"Up")
            m_historyAt = m_historyAt < 0 ? int(history.size()) - 1 : std::max(0, m_historyAt - 1);
        else if (m_historyAt >= 0)
            m_historyAt = m_historyAt + 1 < history.size() ? m_historyAt + 1 : -1;
        m_input = m_historyAt < 0 ? m_draft : history.at(m_historyAt);
        m_cursor = int(m_input.size());
    } else if (tab) {
        // A new event's line opens the full form; a field completes its choices.
        if (m_typing == Typing::NewEvent) {
            // An empty line waits, as Enter does.
            if (!m_input.trimmed().isEmpty())
                addAndOpen(m_input.trimmed());
            return true;
        }
        if (m_typing != Typing::Field)
            return false;
        if (field() == Field::Calendar) {
            QStringList names;
            for (const CalendarInfo &calendar : calendars())
                names.append(calendar.displayName);
            complete(names);
        } else if (field() == Field::Repeats) {
            complete(Repeat::choices());
        } else {
            return false;
        }
    } else if (key.size() == 1 && key.at(0).isPrint()) {
        m_input.insert(m_cursor++, key);
    } else {
        return false;
    }
    if (m_mode == Mode::Typing && m_typing == Typing::Search)
        searchAsTyped();
    Q_EMIT changed();
    return true;
}

void TuiState::startTyping(Typing typing, const QString &text)
{
    m_mode = Mode::Typing;
    m_typing = typing;
    m_input = text;
    m_cursor = int(text.size());
    m_historyAt = -1;
    m_completing.reset();
    if (typing == Typing::Search) {
        m_found.clear();
        m_foundSelected = -1;
    }
    Q_EMIT changed();
}

void TuiState::searchAsTyped()
{
    const QStringList words = m_input.split(u' ', Qt::SkipEmptyParts);
    QList<Event> found =
        words.isEmpty() ? QList<Event>()
                        : ask([&words](const Commands &commands) { return commands.found(words); });
    std::sort(found.begin(), found.end(),
              [](const Event &a, const Event &b) { return a.start < b.start; });
    m_found = found;
    m_foundSelected = -1;
}

void TuiState::complete(const QStringList &choices)
{
    if (!m_completing)
        m_completing = m_input;
    QStringList names;
    for (const QString &choice : choices) {
        if (choice.startsWith(*m_completing, Qt::CaseInsensitive))
            names.append(choice);
    }
    if (names.isEmpty())
        return;
    // Tab again goes on to the next that matches.
    const qsizetype at = names.indexOf(m_input);
    m_input = names.at(at < 0 ? 0 : (at + 1) % names.size());
    m_cursor = int(m_input.size());
}

void TuiState::runCommand(const QString &line)
{
    const QString name = line.section(u' ', 0, 0).toLower();
    const QString rest = line.section(u' ', 1).trimmed();
    static const QHash<QString, QString> actions = {
        {u"day"_s, u"dayView"_s},       {u"week"_s, u"weekView"_s},
        {u"month"_s, u"monthView"_s},   {u"grid"_s, u"toggleGrid"_s},
        {u"agenda"_s, u"agendaView"_s}, {u"today"_s, u"today"_s},
        {u"invites"_s, u"invites"_s},   {u"calendars"_s, u"calendars"_s},
        {u"sync"_s, u"refresh"_s},      {u"undo"_s, u"undo"_s},
        {u"help"_s, u"help"_s},         {u"q"_s, u"quitTerminal"_s},
        {u"quit"_s, u"quitTerminal"_s},
    };
    if (actions.contains(name) && rest.isEmpty()) {
        run(actions.value(name));
        return;
    }
    if (name == u"go" && !rest.isEmpty()) {
        goTo(rest);
        return;
    }
    if (name == u"agenda" && !rest.isEmpty()) {
        bool ok = false;
        const int days = rest.toInt(&ok);
        if (!ok || days < 1 || days > 366) {
            say(tr("The agenda takes 1 to 366 days, as :agenda 30."), true);
            return;
        }
        m_agendaDays = days;
        setView(View::Agenda);
        return;
    }
    if ((name == u"add" || name == u"search") && !rest.isEmpty()) {
        m_typing = name == u"add" ? Typing::NewEvent : Typing::Search;
        m_input = rest;
        submit();
        return;
    }
    say(tr("No command %1. There are day, week, month, agenda, grid, today, go, add, search, "
           "invites, calendars, sync, undo, help and q.")
            .arg(line),
        true);
}

bool TuiState::pressFound(const QString &key)
{
    if (key == u"Escape" || key == u"q") {
        m_mode = Mode::Browse;
    } else if (key == u"j" || key == u"Down") {
        if (m_foundSelected + 1 < m_found.size())
            ++m_foundSelected;
    } else if (key == u"k" || key == u"Up") {
        if (m_foundSelected > 0)
            --m_foundSelected;
    } else if (key == u"Enter") {
        if (m_foundSelected < 0)
            return false;
        jumpTo(m_found.at(m_foundSelected));
        return true;
    } else {
        return false;
    }
    Q_EMIT changed();
    return true;
}

bool TuiState::pressCalendars(const QString &key)
{
    const QList<CalendarInfo> all = calendars();
    if (key == u"Escape" || key == u"q" || key == u"c") {
        m_mode = Mode::Browse;
    } else if (key == u"j" || key == u"Down") {
        if (m_calendarSelected + 1 < all.size())
            ++m_calendarSelected;
    } else if (key == u"k" || key == u"Up") {
        if (m_calendarSelected > 0)
            --m_calendarSelected;
    } else if (key == u" " || key == u"Enter") {
        if (m_calendarSelected >= all.size())
            return false;
        const CalendarInfo &calendar = all.at(m_calendarSelected);
        m_settings.setCalendarVisible(calendar.id, !calendarShown(calendar));
    } else {
        return false;
    }
    Q_EMIT changed();
    return true;
}

void TuiState::submit()
{
    const QString text = m_input.trimmed();
    m_mode = Mode::Browse;
    if (m_typing == Typing::Field) {
        m_mode = Mode::Editing;
        const std::optional<Event> event = editing();
        if (!event) {
            m_mode = Mode::Browse;
            Q_EMIT changed();
            return;
        }
        // An unchanged field changes nothing; a title or a time cannot be left empty, but a
        // place, the guests and notes can.
        const Field which = field();
        const bool clears =
            which == Field::Where || which == Field::Guests || which == Field::Notes;
        if (text == fieldValue(*event, which) || (text.isEmpty() && !clears)) {
            Q_EMIT changed();
            return;
        }
        EditOptions options;
        switch (which) {
        case Field::Title: options.title = text; break;
        case Field::Where: options.where = text; break;
        case Field::Starts: options.start = text; break;
        case Field::Ends: options.end = text; break;
        case Field::Calendar: options.calendar = text; break;
        case Field::Repeats: options.repeat = text; break;
        case Field::Guests:
            options.guests = text.split(QRegularExpression(u"[,\\s]+"_s), Qt::SkipEmptyParts);
            break;
        case Field::Notes: options.notes = text; break;
        case Field::AllDay:
        case Field::Call: break;
        }
        change(Commands::reference(*event), options);
        return;
    }
    if (text.isEmpty()) {
        Q_EMIT changed();
        return;
    }
    if (m_typing == Typing::Command) {
        runCommand(text);
        return;
    }
    if (m_typing == Typing::Date) {
        goTo(text);
        return;
    }
    if (m_typing == Typing::Move) {
        const std::optional<Event> event = current();
        if (!event) {
            Q_EMIT changed();
            return;
        }
        EditOptions options;
        options.start = text;
        change(Commands::reference(*event), options);
        return;
    }
    if (m_typing == Typing::NewEvent) {
        act([text](Commands &commands, const std::function<void(int)> &done) {
            commands.add(text, {}, done);
        });
        return;
    }
    const QStringList words = text.split(u' ', Qt::SkipEmptyParts);
    showFound(tr("Found for %1").arg(text),
              ask([&words](const Commands &commands) { return commands.found(words); }));
}

QList<Event> TuiState::ask(const std::function<QList<Event>(const Commands &)> &what) const
{
    QString unused;
    QTextStream out(&unused), err(&unused);
    const Commands commands(*m_source, m_settings, out, err, m_now(), m_settings.times()->zone());
    return what(commands);
}

void TuiState::showFound(const QString &title, QList<Event> events)
{
    std::sort(events.begin(), events.end(),
              [](const Event &a, const Event &b) { return a.start < b.start; });
    m_found = events;
    m_foundTitle = title;
    // Nearest to now first in view.
    m_foundSelected = m_found.isEmpty() ? -1 : 0;
    for (int i = 0; i < m_found.size(); ++i) {
        if (m_found.at(i).end > m_now()) {
            m_foundSelected = i;
            break;
        }
    }
    m_mode = Mode::Found;
    Q_EMIT changed();
}

void TuiState::goTo(const QString &text)
{
    QString unused;
    QTextStream out(&unused), err(&unused);
    const Commands commands(*m_source, m_settings, out, err, m_now(), m_settings.times()->zone());
    const std::optional<QDateTime> when = commands.when(text, today());
    if (!when) {
        say(tr("Could not read %1 as a day.").arg(text), true);
        return;
    }
    m_mode = Mode::Browse;
    m_day = m_settings.times()->date(*when).date();
    reload();
    selectFirstOf(m_day);
}

void TuiState::remind(const Event &event)
{
    const ReminderNotifier::Text text =
        ReminderNotifier::describe(event, m_now(), *m_settings.times());
    say(tr("Reminder: %1, %2").arg(text.title, text.body.section(u'\n', 0, 0)));
}

void TuiState::showEvent(const Event &event)
{
    m_help = false;
    jumpTo(event);
}

void TuiState::jumpTo(const Event &event)
{
    m_mode = Mode::Browse;
    m_view = View::Day;
    m_day = m_settings.times()->date(event.start).date();
    reload();
    for (int i = 0; i < m_events.size(); ++i) {
        if (m_events.at(i).eventId == event.eventId &&
            m_events.at(i).calendarId == event.calendarId)
            m_selected = i;
    }
    Q_EMIT changed();
}

void TuiState::act(const std::function<void(Commands &, const std::function<void(int)> &)> &run)
{
    // The commands write their mistakes as the CLI would; the TUI shows them.
    auto out = std::make_shared<QString>();
    auto err = std::make_shared<QString>();
    auto outStream = std::make_shared<QTextStream>(out.get());
    auto errStream = std::make_shared<QTextStream>(err.get());
    auto commands = std::make_shared<Commands>(*m_source, m_settings, *outStream, *errStream,
                                               m_now(), m_settings.times()->zone());
    run(*commands, [this, commands, out, err, outStream, errStream](int code) {
        errStream->flush();
        if (code != 0) {
            QString why = err->trimmed();
            why.remove(u"callie: "_s);
            say(why.isEmpty() ? tr("That did not work.") : why, true);
        }
    });
    Q_EMIT changed();
}

void TuiState::say(const QString &message, bool error)
{
    m_message = message;
    m_messageIsError = error;
    // Gone once the change can no longer be taken back, or a moment after an error.
    QTimer::singleShot(std::chrono::seconds(error ? 5 : QueuedSource::kHoldSecs), this,
                       [this, message] {
                           if (m_message == message) {
                               m_message.clear();
                               Q_EMIT changed();
                           }
                       });
    Q_EMIT changed();
}

bool TuiState::pressEditing(const QString &key)
{
    const std::optional<Event> event = editing();
    if (!event) {
        m_mode = Mode::Browse;
        Q_EMIT changed();
        return true;
    }
    if (key == u"Escape" || key == u"q") {
        m_mode = m_editingFrom;
        Q_EMIT changed();
        return true;
    }
    if (key == u"j" || key == u"Down") {
        m_field = std::min(m_field + 1, int(fields().size()) - 1);
    } else if (key == u"k" || key == u"Up") {
        m_field = std::max(m_field - 1, 0);
    } else if (key == u"Enter" || key == u" ") {
        // All day and the call switch rather than take a line.
        if (field() == Field::AllDay || field() == Field::Call) {
            EditOptions options;
            if (field() == Field::AllDay)
                options.allDay = !event->allDay;
            else
                options.video = event->joinUrl().isEmpty();
            change(Commands::reference(*event), options);
            return true;
        }
        if (key == u" ")
            return false;
        startTyping(Typing::Field, fieldValue(*event, field()));
    } else if (key == u"u") {
        return pressBrowsing(key);
    } else {
        return false;
    }
    Q_EMIT changed();
    return true;
}

bool TuiState::repeats(const Event &event)
{
    return !event.seriesId.isEmpty() && !event.detached;
}

void TuiState::confirm(const Event &event, const QString &answer)
{
    m_confirming = event;
    m_answer = answer;
    m_askScope = repeats(event);
    m_mode = Mode::Confirming;
    Q_EMIT changed();
}

QString TuiState::question() const
{
    if (!m_confirming)
        return {};
    const QString name = m_confirming->summary;
    if (m_answer.isEmpty())
        return m_askScope ? tr("Delete %1, which repeats:").arg(name) : tr("Delete %1?").arg(name);
    return tr("Answer %1, which repeats:").arg(name);
}

bool TuiState::pressConfirming(const QString &key)
{
    if (!m_confirming)
        return false;
    QString scope;
    if (m_askScope && (key == u"t" || key == u"a")) {
        scope = key == u"t" ? u"this"_s : u"all"_s;
    } else if (!m_askScope && key == u"y") {
        scope = u"this"_s;
    } else if (key == u"Escape" || key == u"q" || (!m_askScope && key == u"n")) {
        m_confirming.reset();
        m_mode = Mode::Details;
        Q_EMIT changed();
        return true;
    } else {
        return false;
    }
    const QString id = Commands::reference(*m_confirming);
    const QString answer = m_answer;
    m_confirming.reset();
    // A deleted event's details close; an answered one's stay open.
    m_mode = answer.isEmpty() ? Mode::Browse : Mode::Details;
    if (answer.isEmpty()) {
        act([id, scope](Commands &commands, const std::function<void(int)> &done) {
            commands.remove(id, scope, done);
        });
    } else {
        act([id, answer, scope](Commands &commands, const std::function<void(int)> &done) {
            commands.respond(id, answer, scope, done);
        });
    }
    return true;
}

bool TuiState::startEditing(const Event &event)
{
    if (!event.canEdit) {
        say(tr("%1 cannot be changed from here.").arg(event.summary), true);
        return true;
    }
    m_editingFrom = m_mode;
    m_editingId = event.eventId;
    m_editingCalendar = event.calendarId;
    m_field = 0;
    m_mode = Mode::Editing;
    Q_EMIT changed();
    return true;
}

std::optional<Event> TuiState::editing() const
{
    if (m_mode != Mode::Editing && !(m_mode == Mode::Typing && m_typing == Typing::Field))
        return std::nullopt;
    for (const Event &event : m_events) {
        if (event.eventId == m_editingId && event.calendarId == m_editingCalendar)
            return event;
    }
    return std::nullopt;
}

QList<TuiState::Field> TuiState::fields()
{
    return {Field::Title,    Field::Where,   Field::Starts, Field::Ends, Field::AllDay,
            Field::Calendar, Field::Repeats, Field::Guests, Field::Call, Field::Notes};
}

QString TuiState::fieldName(Field field)
{
    switch (field) {
    case Field::Title: return tr("Title");
    case Field::Where: return tr("Where");
    case Field::Starts: return tr("Starts");
    case Field::Ends: return tr("Ends");
    case Field::AllDay: return tr("All day");
    case Field::Calendar: return tr("Calendar");
    case Field::Repeats: return tr("Repeats");
    case Field::Guests: return tr("Guests");
    case Field::Call: return tr("Video call");
    case Field::Notes: return tr("Notes");
    }
    return {};
}

QString TuiState::fieldValue(const Event &event, Field field) const
{
    const Times &times = *m_settings.times();
    // As callie add and callie edit read them: an all-day event's days alone,
    // and its end as its last day.
    const auto at = [&](const QDateTime &when, bool end) {
        if (event.allDay)
            return times.date(when).date().addDays(end ? -1 : 0).toString(Qt::ISODate);
        return when.toTimeZone(times.zone()).toString(u"yyyy-MM-dd HH:mm"_s);
    };
    switch (field) {
    case Field::Title: return event.summary;
    case Field::Where: return event.location;
    case Field::Starts: return at(event.start, false);
    case Field::Ends: return at(event.end, true);
    case Field::AllDay: return event.allDay ? tr("yes") : tr("no");
    case Field::Calendar:
        for (const CalendarInfo &calendar : calendars()) {
            if (calendar.id == event.calendarId)
                return calendar.displayName;
        }
        return event.calendarId;
    case Field::Repeats: return Repeat::choiceOf(event.recurrence, times.date(event.start).date());
    case Field::Guests: {
        QStringList guests;
        for (const Guest &guest : event.guests) {
            if (!guest.self)
                guests.append(guest.email);
        }
        return guests.join(u", "_s);
    }
    case Field::Call: return event.joinUrl().isEmpty() ? tr("no") : tr("yes");
    // Google's notes are HTML; they are typed as their text.
    case Field::Notes: return QTextDocumentFragment::fromHtml(event.description).toPlainText();
    }
    return {};
}

void TuiState::addAndOpen(const QString &text)
{
    const EventDraft draft = QuickAdd::parse(text, m_now(), m_settings.times()->zone());
    m_mode = Mode::Browse;
    if (!draft.isValid()) {
        say(tr("Could not read an event from %1; try Lunch tomorrow 12-1pm.").arg(text), true);
        return;
    }
    // On its day, with its fields listed to fill in the rest: the event that was not there
    // before, not another of the same name and time, as a repeating one's.
    m_day = m_settings.times()->date(draft.start).date();
    reload();
    QSet<QString> before;
    for (const Event &event : std::as_const(m_events))
        before.insert(event.calendarId + u'/' + event.eventId);
    act([text](Commands &commands, const std::function<void(int)> &done) {
        commands.add(text, {}, done);
    });
    reload();
    for (const Event &event : std::as_const(m_events)) {
        if (event.summary == draft.summary && event.start == draft.start &&
            !before.contains(event.calendarId + u'/' + event.eventId)) {
            startEditing(event);
            return;
        }
    }
    selectFirstOf(m_day);
}

void TuiState::change(const QString &id, const EditOptions &options)
{
    act([id, options](Commands &commands, const std::function<void(int)> &done) {
        commands.edit(id, options, {}, done);
    });
}

void TuiState::paste()
{
    // On the day of the selected event, or the day in view, at the copy's own time.
    const std::optional<Event> here = current();
    const QDate day = here ? dayOf(*here) : m_day;
    QString at = day.toString(Qt::ISODate);
    if (!m_copied->allDay)
        at += u' ' + m_copied->start.toTimeZone(m_settings.times()->zone()).toString(u"HH:mm"_s);
    const QString id = Commands::reference(*m_copied);
    act([id, at](Commands &commands, const std::function<void(int)> &done) {
        commands.duplicate(id, at, done);
    });
}

QList<CalendarInfo> TuiState::calendars() const
{
    return m_source ? m_source->calendars() : QList<CalendarInfo>();
}

bool TuiState::calendarShown(const CalendarInfo &calendar) const
{
    return m_settings.isShown(calendar);
}

std::optional<Event> TuiState::current() const
{
    if (m_selected < 0 || m_selected >= m_events.size())
        return std::nullopt;
    return m_events.at(m_selected);
}

QString TuiState::syncStatus() const
{
    if (!m_source)
        return {};
    if (m_source->syncing())
        return tr("Syncing...");
    if (!m_source->lastError().isEmpty())
        return m_source->lastError();
    const QDateTime synced = m_source->lastSynced();
    return synced.isValid() ? tr("Updated %1").arg(m_settings.times()->time(synced))
                            : tr("Not synced yet");
}

bool TuiState::syncFailed() const
{
    return m_source && !m_source->syncing() && !m_source->lastError().isEmpty();
}

} // namespace callie::cli
