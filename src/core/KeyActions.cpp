#include "callie/KeyActions.h"

#include <QCoreApplication>
#include <QKeySequence>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("callie::KeyActions", text);
}

KeyAction act(const QString &id, const QString &label, const QString &group,
              const QVariantList &standard, const QString &vi, const QString &leader,
              bool inCard = false, KeyAction::Where where = KeyAction::Where::Both)
{
    return {id, label, group, standard, vi, leader, inCard, where};
}

} // namespace

QList<KeyAction> keyActions()
{
    using K = QKeySequence;
    using W = KeyAction::Where;
    return {
        act(u"newEvent"_s, tr("New event"), u"events"_s, {int(K::New)}, u"n"_s, {}),
        act(u"search"_s, tr("Search events"), u"general"_s, {int(K::Find)}, u"/"_s, {}),
        act(u"undo"_s, tr("Undo the last change"), u"events"_s, {int(K::Undo)}, u"u"_s, {}),
        act(u"copy"_s, tr("Copy the open event"), u"events"_s, {int(K::Copy)}, {}, {}, true,
            W::App),
        act(u"duplicate"_s, tr("Duplicate the open event"), u"events"_s, {u"Ctrl+D"_s}, {}, {},
            true, W::App),
        act(u"paste"_s, tr("Paste under the pointer"), u"events"_s, {int(K::Paste)}, u"p"_s, {},
            false, W::App),
        act(u"previous"_s, tr("Previous day, week or month"), u"views"_s, {u"Alt+Left"_s}, u"h"_s,
            {}),
        act(u"next"_s, tr("Next day, week or month"), u"views"_s, {u"Alt+Right"_s}, u"l"_s, {}),
        act(u"today"_s, tr("Go to today"), u"views"_s, {u"Alt+Home"_s}, u"t"_s, {}),
        act(u"scrollDown"_s, tr("Scroll down the day"), u"views"_s, {}, u"j"_s, {}, false, W::App),
        act(u"scrollUp"_s, tr("Scroll up the day"), u"views"_s, {}, u"k"_s, {}, false, W::App),
        act(u"top"_s, tr("Go to the start of the day"), u"views"_s, {}, u"gg"_s, {}, false, W::App),
        act(u"bottom"_s, tr("Go to the end of the day"), u"views"_s, {}, u"G"_s, {}, false, W::App),
        act(u"dayView"_s, tr("Day view"), u"views"_s, {u"Ctrl+1"_s}, {}, u"d"_s),
        act(u"weekView"_s, tr("Week view"), u"views"_s, {u"Ctrl+2"_s}, {}, u"w"_s),
        act(u"monthView"_s, tr("Month view"), u"views"_s, {u"Ctrl+3"_s}, {}, u"m"_s, false, W::App),
        act(u"agendaView"_s, tr("Agenda view"), u"views"_s, {u"Ctrl+4"_s}, {}, u"a"_s),
        act(u"invites"_s, tr("Show invitations"), u"general"_s, {}, {}, u"i"_s),
        act(u"refresh"_s, tr("Sync now"), u"general"_s, {int(K::Refresh)}, {}, u"r"_s),
        act(u"settings"_s, tr("Settings"), u"general"_s, {int(K::Preferences), u"Ctrl+,"_s}, {},
            u"s"_s, false, W::App),
        act(u"help"_s, tr("Keyboard shortcuts"), u"general"_s, {int(K::HelpContents)}, u"?"_s, {}),
        act(u"quit"_s, tr("Quit Callie"), u"general"_s, {int(K::Quit)}, {}, {}, false, W::App),

        // The terminal calendar's own: it moves between events, where the app scrolls the day.
        act(u"nextEvent"_s, tr("Next event"), u"views"_s, {}, u"j"_s, {}, false, W::Terminal),
        act(u"previousEvent"_s, tr("Previous event"), u"views"_s, {}, u"k"_s, {}, false,
            W::Terminal),
        act(u"firstEvent"_s, tr("First event"), u"views"_s, {}, u"gg"_s, {}, false, W::Terminal),
        act(u"lastEvent"_s, tr("Last event"), u"views"_s, {}, u"G"_s, {}, false, W::Terminal),
        act(u"nextView"_s, tr("Next view"), u"views"_s, {}, u"Tab"_s, {}, false, W::Terminal),
        act(u"details"_s, tr("The event's details"), u"events"_s, {}, u"Enter"_s, {}, false,
            W::Terminal),
        act(u"change"_s, tr("Change the event: its title, place, times or calendar"), u"events"_s,
            {}, u"e"_s, {}, false, W::Terminal),
        act(u"move"_s, tr("Move the event, keeping its length"), u"events"_s, {}, u"m"_s, {}, false,
            W::Terminal),
        act(u"copyEvent"_s, tr("Copy the event"), u"events"_s, {}, u"y"_s, {}, false, W::Terminal),
        act(u"pasteEvent"_s, tr("Put a copy on the selected day"), u"events"_s, {}, u"p"_s, {},
            false, W::Terminal),
        act(u"duplicateEvent"_s, tr("Duplicate the event"), u"events"_s, {}, u"D"_s, {}, false,
            W::Terminal),
        act(u"calendars"_s, tr("Show or hide calendars"), u"general"_s, {}, u"c"_s, {}, false,
            W::Terminal),
        act(u"quitTerminal"_s, tr("Quit"), u"general"_s, {}, u"q"_s, {}, false, W::Terminal),
        // In an event's details.
        act(u"accept"_s, tr("Answer yes"), u"events"_s, {}, u"y"_s, {}, true, W::Terminal),
        act(u"maybe"_s, tr("Answer maybe"), u"events"_s, {}, u"m"_s, {}, true, W::Terminal),
        act(u"decline"_s, tr("Answer no"), u"events"_s, {}, u"n"_s, {}, true, W::Terminal),
        act(u"join"_s, tr("Join the call"), u"events"_s, {}, u"o"_s, {}, true, W::Terminal),
        act(u"delete"_s, tr("Delete the event"), u"events"_s, {}, u"x"_s, {}, true, W::Terminal),
        act(u"changeOpen"_s, tr("Change the event"), u"events"_s, {}, u"e"_s, {}, true,
            W::Terminal),
    };
}

QList<KeyAction> keyActionsFor(KeyAction::Where where)
{
    QList<KeyAction> found = keyActions();
    found.removeIf([where](const KeyAction &action) {
        return action.where != where && action.where != KeyAction::Where::Both;
    });
    return found;
}

} // namespace callie
