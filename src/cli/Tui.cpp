#include "Tui.h"

#include "TuiState.h"
#include "greylock/cli/Palette.h"

#include "callie/KeyActions.h"
#include "callie/Settings.h"
#include "callie/Times.h"

#include <QCoreApplication>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

#include <ftxui/component/component.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

using namespace Qt::StringLiterals;

namespace callie::cli {

namespace {

using ftxui::Element;
using ftxui::Elements;

ftxui::Color rgb(const QColor &color)
{
    return ftxui::Color::RGB(uint8_t(color.red()), uint8_t(color.green()), uint8_t(color.blue()));
}

Element label(const QString &text)
{
    return ftxui::text(text.toStdString());
}

/// What the TUI draws with: the theme, and the times as the user reads them.
struct Look
{
    const Palette &palette;
    const Settings &settings;
    /// The terminal's width, in cells.
    int width = 80;
    const greylock::ThemeSpec::Colors &c = palette.colors();
};

QString when(const Event &event, const Look &look)
{
    if (event.allDay)
        return QObject::tr("all day");
    return QObject::tr("%1 to %2")
        .arg(look.settings.times()->time(event.start), look.settings.times()->time(event.end));
}

/// An event as a sticker, in its calendar's colors as the app draws it: hollow
/// until answered, struck through when declined.
Element sticker(const Event &event, const QString &text, const Look &look)
{
    using namespace ftxui;
    const QColor fill = look.palette.fill(event.color);
    Element body = label(u' ' + text + u' ');
    if (event.responseStatus == u"needsAction")
        body = body | color(rgb(look.palette.onPage(event.color))) | bold;
    else
        body = body | color(rgb(look.palette.ink(event.color))) | bgcolor(rgb(fill));
    if (event.declined)
        body = body | strikethrough;
    return body;
}

/// The marker beside the selected event.
Element marker(bool selected, const Look &look)
{
    using namespace ftxui;
    return selected ? text("▌") | color(rgb(look.c.accent)) : text(" ");
}

/// The widest of the events' times, so they line up.
int timeWidth(const TuiState &state, const Look &look)
{
    qsizetype widest = 0;
    for (const Event &event : state.events())
        widest = std::max(widest, when(event, look).size());
    return int(widest) + 2;
}

Element eventRow(const TuiState &state, int index, int timeColumn, const Look &look)
{
    using namespace ftxui;
    const Event &event = state.events().at(index);
    const bool selected = index == state.selected();
    Element row =
        hbox({marker(selected, look),
              label(when(event, look).leftJustified(timeColumn)) | color(rgb(look.c.textMuted)),
              sticker(event, event.summary, look),
              selected && !event.location.isEmpty()
                  ? label(u"  "_s + event.location) | color(rgb(look.c.textFaint))
                  : text(""),
              filler()});
    if (event.end <= state.now())
        row = row | dim;
    return selected ? row | focus : row;
}

QString dayHeading(QDate day, const TuiState &state)
{
    const QString name = QLocale().toString(day, u"dddd, MMMM d"_s);
    return day == state.today() ? QObject::tr("Today, %1").arg(name) : name;
}

Element empty(const QString &text, const Look &look)
{
    using namespace ftxui;
    return label(u"  "_s + text) | color(rgb(look.c.textFaint));
}

Element dayView(const TuiState &state, const Look &look)
{
    Elements rows;
    const int timeColumn = timeWidth(state, look);
    for (int i = 0; i < state.events().size(); ++i)
        rows.push_back(eventRow(state, i, timeColumn, look));
    if (rows.empty())
        rows.push_back(empty(QObject::tr("Nothing on this day."), look));
    return ftxui::vbox(std::move(rows));
}

Element agendaView(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    Elements rows;
    QDate current;
    const int timeColumn = timeWidth(state, look);
    for (int i = 0; i < state.events().size(); ++i) {
        const QDate day = state.dayOf(state.events().at(i));
        if (day != current) {
            current = day;
            if (!rows.empty())
                rows.push_back(text(""));
            rows.push_back(label(dayHeading(day, state)) | bold |
                           color(rgb(day == state.today() ? look.c.accent : look.c.text)));
        }
        rows.push_back(eventRow(state, i, timeColumn, look));
    }
    if (rows.empty())
        rows.push_back(
            empty(QObject::tr("Nothing in the next %n days.", nullptr, state.rangeDays()), look));
    return vbox(std::move(rows));
}

Element weekView(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    // Seven equal columns, whatever their events hold, between six rules and the margins.
    const int column = std::max(4, (look.width - 2 - 6) / 7);
    Elements columns;
    for (int d = 0; d < 7; ++d) {
        const QDate day = state.rangeStart().addDays(d);
        const bool today = day == state.today();
        Elements cells;
        cells.push_back(label(QLocale().toString(day, u"ddd d"_s)) | bold |
                        color(rgb(today ? look.c.accent : look.c.textMuted)));
        for (int i = 0; i < state.events().size(); ++i) {
            const Event &event = state.events().at(i);
            if (state.dayOf(event) != day)
                continue;
            const QString time =
                event.allDay ? QString() : look.settings.times()->time(event.start) + u' ';
            Element cell = hbox({marker(i == state.selected(), look),
                                 sticker(event, time + event.summary, look) | flex});
            if (event.end <= state.now())
                cell = cell | dim;
            cells.push_back(i == state.selected() ? cell | focus : cell);
        }
        columns.push_back(vbox(std::move(cells)) | size(WIDTH, EQUAL, column));
        if (d < 6)
            columns.push_back(separatorLight() | color(rgb(look.c.hairline)));
    }
    return hbox(std::move(columns));
}

/// The day, or the week's days side by side, as half hours from the earliest event or eight to
/// the latest or six: each event where its times put it, beside those it overlaps, free time
/// left blank, and a line across today at the time it is.
Element gridView(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    const Times &times = *look.settings.times();
    const int days = state.rangeDays();
    constexpr int kTimeColumn = 7;
    const int column = std::max(6, (look.width - 2 - kTimeColumn - (days - 1)) / days);

    // The hours shown: at least eight to six, and whatever the events need beyond.
    int first = 8 * 60, last = 18 * 60;
    for (const Event &event : state.events()) {
        if (event.allDay)
            continue;
        first = std::min(first, state.startMinute(event) / 60 * 60);
        const int end = state.dayOf(event) == times.date(event.end).date()
                            ? times.minutesIntoDay(event.end)
                            : 24 * 60;
        last = std::max(last, (end + 59) / 60 * 60);
    }
    const int rows = (last - first) / 30;

    // Each day's timed events in lanes, a later one beside any it overlaps.
    struct Placed
    {
        int index, lane, from, to;
    };
    QList<QList<Placed>> placed(days);
    QList<int> lanes(days, 1);
    Elements allDay;
    allDay.push_back(text(QString(kTimeColumn, u' ').toStdString()));
    for (int d = 0; d < days; ++d) {
        const QDate day = state.rangeStart().addDays(d);
        Elements top;
        QList<int> laneEnds;
        for (int i = 0; i < state.events().size(); ++i) {
            const Event &event = state.events().at(i);
            if (state.dayOf(event) != day)
                continue;
            if (event.allDay) {
                top.push_back(hbox({marker(i == state.selected(), look),
                                    sticker(event, event.summary, look) | flex}));
                continue;
            }
            const int from = (state.startMinute(event) - first) / 30;
            const int to =
                times.date(event.end).date() == day
                    ? std::max(from + 1, (times.minutesIntoDay(event.end) - first + 29) / 30)
                    : rows;
            int lane = 0;
            while (lane < laneEnds.size() && laneEnds.at(lane) > from)
                ++lane;
            if (lane == laneEnds.size())
                laneEnds.append(to);
            else
                laneEnds[lane] = to;
            placed[d].append({i, lane, from, to});
        }
        lanes[d] = std::max(1, int(laneEnds.size()));
        if (top.empty())
            top.push_back(text(""));
        allDay.push_back(vbox(std::move(top)) | size(WIDTH, EQUAL, column));
        if (d < days - 1)
            allDay.push_back(text(" "));
    }

    Elements lines;
    if (days > 1) {
        Elements names;
        names.push_back(text(QString(kTimeColumn, u' ').toStdString()));
        for (int d = 0; d < days; ++d) {
            const QDate day = state.rangeStart().addDays(d);
            names.push_back(label(QLocale().toString(day, u"ddd d"_s)) | bold |
                            color(rgb(day == state.today() ? look.c.accent : look.c.textMuted)) |
                            size(WIDTH, EQUAL, column));
            if (d < days - 1)
                names.push_back(text(" "));
        }
        lines.push_back(hbox(std::move(names)));
    }
    lines.push_back(hbox(std::move(allDay)));
    lines.push_back(separatorLight() | color(rgb(look.c.hairline)));

    const QDateTime now = state.now();
    for (int r = 0; r < rows; ++r) {
        const int minute = first + r * 30;
        Elements row;
        // The hour on its first half hour.
        row.push_back((minute % 60 == 0 ? label(times.hour(minute / 60)) : text("")) |
                      color(rgb(look.c.textMuted)) | size(WIDTH, EQUAL, kTimeColumn));
        for (int d = 0; d < days; ++d) {
            const QDate day = state.rangeStart().addDays(d);
            const bool nowHere = day == state.today() && times.minutesIntoDay(now) >= minute &&
                                 times.minutesIntoDay(now) < minute + 30;
            const int laneWidth = std::max(2, column / lanes.at(d));
            Elements cells;
            for (int lane = 0; lane < lanes.at(d); ++lane) {
                Element cell = nowHere ? text(QString(laneWidth, u'─').toStdString()) |
                                             color(rgb(look.c.accent))
                                       : text("");
                for (const Placed &p : placed.at(d)) {
                    if (p.lane != lane || r < p.from || r >= p.to)
                        continue;
                    const Event &event = state.events().at(p.index);
                    const bool selected = p.index == state.selected();
                    // The title on the event's first half hour; its color below, to its end.
                    cell = hbox(
                        {marker(selected && r == p.from, look),
                         sticker(event, r == p.from ? event.summary : QString(), look) | flex});
                    if (event.end <= now)
                        cell = cell | dim;
                    if (selected && r == p.from)
                        cell = cell | focus;
                }
                cells.push_back(cell | size(WIDTH, EQUAL, laneWidth));
            }
            row.push_back(hbox(std::move(cells)) | size(WIDTH, EQUAL, column));
            if (d < days - 1)
                row.push_back(separatorLight() | color(rgb(look.c.hairline)));
        }
        lines.push_back(hbox(std::move(row)));
    }
    return vbox(std::move(lines));
}

/// The month as weeks of days, each day with its first events and how many more it has.
Element monthView(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    const int column = std::max(4, (look.width - 2 - 6) / 7);
    const int month = state.day().month();
    // A day's first few events; the rest it counts.
    constexpr int kShown = 3;
    Elements weeks;
    Elements names;
    for (int d = 0; d < 7; ++d) {
        names.push_back(label(QLocale().toString(state.rangeStart().addDays(d), u"ddd"_s)) |
                        color(rgb(look.c.textMuted)) | size(WIDTH, EQUAL, column));
        if (d < 6)
            names.push_back(text(" "));
    }
    weeks.push_back(hbox(std::move(names)));
    for (int w = 0; w < state.rangeDays() / 7; ++w) {
        weeks.push_back(separatorLight() | color(rgb(look.c.hairline)));
        Elements days;
        for (int d = 0; d < 7; ++d) {
            const QDate day = state.rangeStart().addDays(w * 7 + d);
            const bool today = day == state.today();
            Elements cells;
            Element number = label(QString::number(day.day())) | bold;
            cells.push_back(today                  ? number | color(rgb(look.c.accent))
                            : day.month() != month ? number | color(rgb(look.c.textFaint))
                                                   : number);
            int shown = 0, more = 0;
            for (int i = 0; i < state.events().size(); ++i) {
                const Event &event = state.events().at(i);
                if (state.dayOf(event) != day)
                    continue;
                // The selected event shows, though the day has more than fit.
                if (shown >= kShown && i != state.selected()) {
                    ++more;
                    continue;
                }
                ++shown;
                Element cell = hbox({marker(i == state.selected(), look),
                                     sticker(event, event.summary, look) | flex});
                if (event.end <= state.now() || day.month() != month)
                    cell = cell | dim;
                cells.push_back(i == state.selected() ? cell | focus : cell);
            }
            if (more > 0)
                cells.push_back(label(QObject::tr("+%n more", nullptr, more)) |
                                color(rgb(look.c.textMuted)));
            days.push_back(vbox(std::move(cells)) | size(WIDTH, EQUAL, column));
            if (d < 6)
                days.push_back(separatorLight() | color(rgb(look.c.hairline)));
        }
        weeks.push_back(hbox(std::move(days)));
    }
    return vbox(std::move(weeks));
}

QString title(const TuiState &state)
{
    if (state.mode() == TuiState::Mode::Found)
        return state.foundTitle();
    const QLocale locale;
    switch (state.view()) {
    case TuiState::View::Day: return dayHeading(state.day(), state);
    case TuiState::View::Week: {
        const QDate first = state.rangeStart(), last = first.addDays(6);
        return first.month() == last.month() ? QObject::tr("%1 to %2")
                                                   .arg(locale.toString(first, u"MMMM d"_s),
                                                        locale.toString(last, u"d, yyyy"_s))
                                             : QObject::tr("%1 to %2")
                                                   .arg(locale.toString(first, u"MMMM d"_s),
                                                        locale.toString(last, u"MMMM d, yyyy"_s));
    }
    case TuiState::View::Agenda: return QObject::tr("From %1").arg(dayHeading(state.day(), state));
    case TuiState::View::Month: return QLocale().toString(state.day(), u"MMMM yyyy"_s);
    }
    return {};
}

Element header(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    Elements tabs;
    const std::pair<TuiState::View, QString> views[] = {
        {TuiState::View::Day, QObject::tr("Day")},
        {TuiState::View::Week, QObject::tr("Week")},
        {TuiState::View::Month, QObject::tr("Month")},
        {TuiState::View::Agenda, QObject::tr("Agenda")}};
    for (const auto &[view, name] : views) {
        Element tab = label(u' ' + name + u' ');
        tabs.push_back(view == state.view() ? tab | bold | color(rgb(look.c.accentText)) |
                                                  bgcolor(rgb(look.c.accent))
                                            : tab | color(rgb(look.c.textMuted)));
    }
    return hbox({label(title(state)) | bold, filler(), hbox(std::move(tabs))});
}

/// Google's descriptions are HTML; the terminal shows their text.
QString plainText(QString html)
{
    static const QRegularExpression breaks(u"<br\\s*/?>|</p>|</div>|</li>"_s,
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(u"<[^>]*>"_s);
    html.replace(breaks, u"\n"_s).remove(tags);
    html.replace(u"&nbsp;"_s, u" "_s)
        .replace(u"&lt;"_s, u"<"_s)
        .replace(u"&gt;"_s, u">"_s)
        .replace(u"&quot;"_s, u"\""_s)
        .replace(u"&#39;"_s, u"'"_s)
        .replace(u"&amp;"_s, u"&"_s);
    return html.trimmed();
}

QString answerName(const QString &response)
{
    if (response == u"accepted")
        return QObject::tr("Yes");
    if (response == u"tentative")
        return QObject::tr("Maybe");
    if (response == u"declined")
        return QObject::tr("No");
    return QObject::tr("Not answered");
}

Element detailsView(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    const std::optional<Event> found = state.current();
    if (!found)
        return text("");
    const Event &event = *found;
    const auto row = [&look](const QString &name, Element value) {
        return hbox({label(name.leftJustified(12)) | color(rgb(look.c.textMuted)), value | flex});
    };
    const QString day =
        QLocale().toString(look.settings.times()->date(event.start).date(), u"dddd, MMMM d"_s);
    Elements rows = {
        sticker(event, event.summary, look) | bold, text(""),
        row(QObject::tr("When"), label(event.allDay ? day : day + u", "_s + when(event, look)))};
    if (!event.location.isEmpty())
        rows.push_back(row(QObject::tr("Where"), label(event.location)));
    for (const CalendarInfo &calendar : state.calendars()) {
        if (calendar.id == event.calendarId) {
            rows.push_back(row(QObject::tr("Calendar"),
                               hbox({text("● ") | color(rgb(look.palette.onPage(event.color))),
                                     label(calendar.displayName)})));
        }
    }
    if (!event.joinUrl().isEmpty())
        rows.push_back(row(QObject::tr("Call"), label(event.joinUrl().host())));
    if (event.canRespond)
        rows.push_back(row(QObject::tr("Your answer"), label(answerName(event.responseStatus))));
    if (!event.guests.isEmpty()) {
        Elements guests;
        for (const Guest &guest : event.guests) {
            QString name = guest.name.isEmpty() ? guest.email : guest.name;
            if (guest.optional)
                name = QObject::tr("%1, optional").arg(name);
            guests.push_back(hbox({label(name), label(u"  "_s + answerName(guest.response)) |
                                                    color(rgb(look.c.textFaint))}));
        }
        rows.push_back(row(QObject::tr("Guests"), vbox(std::move(guests))));
    }
    const QString notes = plainText(event.description);
    if (!notes.isEmpty()) {
        rows.push_back(text(""));
        for (const QString &line : notes.split(u'\n'))
            rows.push_back(paragraph(line.toStdString()));
    }
    return vbox(std::move(rows));
}

/// The fields of the event being changed, the selected one marked.
Element editView(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    const std::optional<Event> event = state.editing();
    if (!event)
        return text("");
    Elements rows = {sticker(*event, event->summary, look) | bold, text("")};
    const QList<TuiState::Field> fields = TuiState::fields();
    for (int i = 0; i < fields.size(); ++i) {
        const TuiState::Field field = fields.at(i);
        const bool selected = field == state.field();
        Element row = hbox(
            {marker(selected, look),
             label(TuiState::fieldName(field).leftJustified(12)) | color(rgb(look.c.textMuted)),
             label(state.fieldValue(*event, field)), filler()});
        rows.push_back(selected ? row | focus : row);
    }
    return vbox(std::move(rows));
}

Element foundView(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    Elements rows;
    for (int i = 0; i < state.found().size(); ++i) {
        const Event &event = state.found().at(i);
        const bool selected = i == state.foundSelected();
        const QString day = QLocale().toString(look.settings.times()->date(event.start).date(),
                                               u"ddd d MMM yyyy"_s);
        Element line = hbox({marker(selected, look),
                             label(day.leftJustified(16)) | color(rgb(look.c.textMuted)),
                             label((event.allDay ? QObject::tr("all day")
                                                 : look.settings.times()->time(event.start))
                                       .leftJustified(10)) |
                                 color(rgb(look.c.textMuted)),
                             sticker(event, event.summary, look), filler()});
        if (event.end <= state.now())
            line = line | dim;
        rows.push_back(selected ? line | focus : line);
    }
    if (state.found().isEmpty())
        rows.push_back(empty(QObject::tr("Nothing found."), look));
    return vbox(std::move(rows));
}

Element calendarsWindow(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    Elements rows;
    const QList<CalendarInfo> calendars = state.calendars();
    for (int i = 0; i < calendars.size(); ++i) {
        const CalendarInfo &calendar = calendars.at(i);
        const bool shown = state.calendarShown(calendar);
        rows.push_back(
            hbox({marker(i == state.calendarSelected(), look),
                  text(shown ? "● " : "○ ") | color(rgb(look.palette.onPage(calendar.color))),
                  label(calendar.displayName) | color(rgb(shown ? look.c.text : look.c.textFaint)),
                  text(" ")}));
    }
    if (rows.empty())
        rows.push_back(empty(QObject::tr("No calendars yet."), look));
    return window(label(QObject::tr(" Calendars ")) | bold, vbox(std::move(rows))) |
           color(rgb(look.c.text)) | bgcolor(rgb(look.c.surface)) | clear_under | center;
}

/// The keys for what is open, at the bottom right.
QString keysFor(const TuiState &state)
{
    switch (state.mode()) {
    case TuiState::Mode::Details: {
        const std::optional<Event> event = state.current();
        QStringList keys;
        if (event && event->canRespond)
            keys << QObject::tr("y yes   m maybe   n no");
        if (event && !event->joinUrl().isEmpty())
            keys << QObject::tr("o join");
        if (event && !event->location.isEmpty())
            keys << QObject::tr("M map");
        if (event && !event->webUrl.isEmpty())
            keys << QObject::tr("O page");
        if (event && (!event->joinUrl().isEmpty() || !event->webUrl.isEmpty()))
            keys << QObject::tr("c copy");
        if (event && event->canEdit)
            keys << QObject::tr("e change   x delete");
        keys << QObject::tr("Esc back");
        return keys.join(u"   "_s);
    }
    case TuiState::Mode::Confirming:
        return state.askingScope() ? QObject::tr("t this event   a all events   Esc cancel")
                                   : QObject::tr("y delete   n keep");
    case TuiState::Mode::Editing:
        return state.field() == TuiState::Field::AllDay || state.field() == TuiState::Field::Call
                   ? QObject::tr("Enter switch   j k fields   u take back   Esc back")
                   : QObject::tr("Enter change   j k fields   u take back   Esc back");
    case TuiState::Mode::Typing:
        switch (state.typing()) {
        case TuiState::Typing::NewEvent:
            return QObject::tr("Enter add   Tab add and fill in the rest   Esc cancel");
        case TuiState::Typing::Search: return QObject::tr("Enter search   Esc cancel");
        case TuiState::Typing::Field: return QObject::tr("Enter save   Esc cancel");
        case TuiState::Typing::Move: return QObject::tr("Enter move   Esc cancel");
        case TuiState::Typing::Command: return QObject::tr("Enter run   Esc cancel");
        case TuiState::Typing::Date: return QObject::tr("Enter go   Esc cancel");
        }
        break;
    case TuiState::Mode::Found: return QObject::tr("Enter go to it   Esc back");
    case TuiState::Mode::Calendars: return QObject::tr("Space show or hide   Esc back");
    case TuiState::Mode::Browse: break;
    }
    return QObject::tr("? keys   q quit");
}

Element footer(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    Element left = label(state.syncStatus()) |
                   color(rgb(state.syncFailed() ? look.c.danger : look.c.textMuted));
    if (!state.message().isEmpty())
        left = label(state.message()) |
               color(rgb(state.messageIsError() ? look.c.danger : look.c.accent));
    if (state.mode() == TuiState::Mode::Confirming)
        left = label(state.question()) | bold | color(rgb(look.c.danger));
    if (state.count() > 0)
        left = label(QString::number(state.count())) | bold | color(rgb(look.c.accent));
    if (state.pending() == u"leader")
        left =
            label(QObject::tr("d day   w week   m month   a agenda")) | color(rgb(look.c.accent));
    if (state.mode() == TuiState::Mode::Typing) {
        QString prompt;
        switch (state.typing()) {
        case TuiState::Typing::NewEvent: prompt = QObject::tr("New event: "); break;
        case TuiState::Typing::Search: prompt = QObject::tr("Search: "); break;
        case TuiState::Typing::Field:
            prompt = QObject::tr("%1: ").arg(TuiState::fieldName(state.field()));
            break;
        case TuiState::Typing::Move: prompt = QObject::tr("Move to: "); break;
        case TuiState::Typing::Command: prompt = u":"_s; break;
        case TuiState::Typing::Date: prompt = QObject::tr("Go to: "); break;
        }
        // The cursor on the character it is before, or after the line's end.
        const QString line = state.input();
        const int at = state.cursor();
        const Element cursor = at < line.size() ? label(line.mid(at, 1)) | inverted
                                                : text("█") | color(rgb(look.c.accent));
        left = hbox({label(prompt) | bold | color(rgb(look.c.accent)), label(line.left(at)), cursor,
                     label(line.mid(at + 1))});
    }
    return hbox({left, filler(), label(keysFor(state)) | color(rgb(look.c.textFaint))});
}

Element help(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    // From the keys' table the app reads too, so both list the same keys for what they share:
    // the list's keys, or in an event's details its own, in two columns to fit 80 by 24.
    const bool details = state.mode() == TuiState::Mode::Details;
    const QString leader = look.settings.leaderKey();
    Elements rows;
    for (const KeyAction &action : keyActionsFor(KeyAction::Where::Terminal)) {
        if (action.inCard != details)
            continue;
        QString key = action.leader.isEmpty() ? action.vi : leader + u' ' + action.leader;
        // The command line opens on Space when : is the leader.
        if (action.id == u"commandLine" && leader == u":")
            key = u"Space"_s;
        if (key.isEmpty())
            continue;
        rows.push_back(
            hbox({text(" "), label(key.leftJustified(8)) | bold | color(rgb(look.c.accent)),
                  label(action.label), text(" ")}));
    }
    const auto half = rows.begin() + (rows.size() + 1) / 2;
    Elements left(rows.begin(), half);
    Elements right(half, rows.end());
    const QString title =
        details ? QObject::tr(" Keys in an event's details ") : QObject::tr(" Keys ");
    return window(label(title) | bold,
                  hbox({vbox(std::move(left)), separatorLight() | color(rgb(look.c.hairline)),
                        vbox(std::move(right))})) |
           color(rgb(look.c.text)) | bgcolor(rgb(look.c.surface)) | clear_under | center;
}

Element render(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    const bool editing =
        state.mode() == TuiState::Mode::Editing ||
        (state.mode() == TuiState::Mode::Typing && state.typing() == TuiState::Typing::Field);
    // The details stay in view while a question about the event is asked.
    const bool details =
        state.mode() == TuiState::Mode::Details || state.mode() == TuiState::Mode::Confirming;
    const bool found = state.mode() == TuiState::Mode::Found ||
                       (state.mode() == TuiState::Mode::Typing &&
                        state.typing() == TuiState::Typing::Search && !state.input().isEmpty());
    Element body = editing                                  ? editView(state, look)
                   : details                                ? detailsView(state, look)
                   : found                                  ? foundView(state, look)
                   : state.grid()                           ? gridView(state, look)
                   : state.view() == TuiState::View::Week   ? weekView(state, look)
                   : state.view() == TuiState::View::Month  ? monthView(state, look)
                   : state.view() == TuiState::View::Agenda ? agendaView(state, look)
                                                            : dayView(state, look);
    Element page = vbox({header(state, look), separatorLight() | color(rgb(look.c.hairline)),
                         body | yframe | flex, footer(state, look)});
    Element screen = hbox({text(" "), page | flex, text(" ")}) | color(rgb(look.c.text)) |
                     bgcolor(rgb(look.c.background));
    if (state.helpShown())
        return dbox({screen, help(state, look)});
    if (state.mode() == TuiState::Mode::Calendars)
        return dbox({screen, calendarsWindow(state, look)});
    return screen;
}

/// Puts `text` on the clipboard: through wl-copy or xclip when there is one, or else by asking
/// the terminal, which most do for an OSC 52 sequence.
void copyToClipboard(const QString &text)
{
    const std::pair<QString, QStringList> tools[] = {
        {u"wl-copy"_s, {}}, {u"xclip"_s, {u"-selection"_s, u"clipboard"_s}}};
    for (const auto &[tool, arguments] : tools) {
        if (QStandardPaths::findExecutable(tool).isEmpty())
            continue;
        QProcess process;
        process.start(tool, arguments);
        process.write(text.toUtf8());
        process.closeWriteChannel();
        if (process.waitForFinished(2000) && process.exitCode() == 0)
            return;
    }
    std::fputs(("\033]52;c;" + text.toUtf8().toBase64() + "\a").constData(), stdout);
    std::fflush(stdout);
}

/// The name TuiState gives a key, or nothing for one it does not take.
QString keyName(const ftxui::Event &event)
{
    using ftxui::Event;
    if (event == Event::ArrowLeft)
        return u"Left"_s;
    if (event == Event::ArrowRight)
        return u"Right"_s;
    if (event == Event::ArrowUp)
        return u"Up"_s;
    if (event == Event::ArrowDown)
        return u"Down"_s;
    if (event == Event::Home)
        return u"Home"_s;
    if (event == Event::End)
        return u"End"_s;
    if (event == Event::Return)
        return u"Enter"_s;
    if (event == Event::Escape)
        return u"Escape"_s;
    if (event == Event::Tab)
        return u"Tab"_s;
    if (event == Event::Backspace)
        return u"Backspace"_s;
    if (event == Event::Delete)
        return u"Delete"_s;
    // Ctrl and a letter comes as that letter's control character.
    if (const std::string input = event.input();
        input.size() == 1 && input[0] >= 1 && input[0] <= 26)
        return u"Ctrl+"_s + QChar(u'A' + input[0] - 1);
    if (event.is_character())
        return QString::fromStdString(event.character());
    return {};
}

} // namespace

QString renderTui(const TuiState &state, const Settings &settings, const Palette &palette,
                  int width, int height)
{
    auto screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
    ftxui::Render(screen, render(state, Look{palette, settings, width}));
    // Without the escape codes for colors and styles.
    static const QRegularExpression codes(u"\x1b\\[[0-9;?]*[A-Za-z]"_s);
    return QString::fromStdString(screen.ToString()).remove(codes).remove(u'\r');
}

int runTui(TuiState &state, const Settings &settings, const Palette &palette)
{
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    auto view = ftxui::Renderer([&] {
        const ftxui::Dimensions size = ftxui::Terminal::Size();
        // Less the header, its rule and the footer.
        state.setVisibleRows(size.dimy - 3);
        return render(state, Look{palette, settings, size.dimx});
    });
    auto keyed = ftxui::CatchEvent(view, [&](const ftxui::Event &event) {
        const QString key = keyName(event);
        if (key.isEmpty())
            return false;
        state.press(key);
        if (state.quitRequested())
            screen.ExitLoopClosure()();
        return true;
    });
    // Qt runs the loop, so syncs and changes go on while FTXUI draws a step at a time.
    ftxui::Loop loop(&screen, keyed);
    QTimer tick;
    tick.setInterval(15);
    QObject::connect(&tick, &QTimer::timeout, &tick, [&loop] {
        loop.RunOnce();
        if (loop.HasQuitted())
            QCoreApplication::quit();
    });
    QObject::connect(&state, &TuiState::changed, &tick,
                     [&screen] { screen.PostEvent(ftxui::Event::Custom); });
    QObject::connect(&state, &TuiState::openRequested, &tick, [](const QUrl &url) {
        QProcess::startDetached(u"xdg-open"_s, {url.toString()});
    });
    QObject::connect(&state, &TuiState::copyRequested, &tick, &copyToClipboard);
    tick.start();
    return QCoreApplication::exec();
}

} // namespace callie::cli
