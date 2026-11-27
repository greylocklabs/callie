#include "callie/KeyActions.h"

#include <QCoreApplication>
#include <QKeySequence>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

/// Gives the labels a context of their own, for translators.
class KeyActions
{
    Q_DECLARE_TR_FUNCTIONS(callie::KeyActions)
};

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
        act(u"newEvent"_s, KeyActions::tr("New event"), u"events"_s, {int(K::New)}, u"n"_s, {}),
        act(u"search"_s, KeyActions::tr("Search events"), u"general"_s, {int(K::Find)}, u"/"_s, {}),
        act(u"undo"_s, KeyActions::tr("Undo the last change"), u"events"_s, {int(K::Undo)}, u"u"_s,
            {}),
        act(u"copy"_s, KeyActions::tr("Copy the open event"), u"events"_s, {int(K::Copy)}, {}, {},
            true, W::App),
        act(u"duplicate"_s, KeyActions::tr("Duplicate the open event"), u"events"_s, {u"Ctrl+D"_s},
            {}, {}, true, W::App),
        act(u"paste"_s, KeyActions::tr("Paste under the pointer"), u"events"_s, {int(K::Paste)},
            u"p"_s, {}, false, W::App),
        act(u"previous"_s, KeyActions::tr("Previous day, week or month"), u"views"_s,
            {u"Alt+Left"_s}, u"h"_s, {}),
        act(u"next"_s, KeyActions::tr("Next day, week or month"), u"views"_s, {u"Alt+Right"_s},
            u"l"_s, {}),
        act(u"today"_s, KeyActions::tr("Go to today"), u"views"_s, {u"Alt+Home"_s}, u"t"_s, {}),
        // The terminal moves between events, where the app scrolls the day.
        act(u"nextEvent"_s, KeyActions::tr("Next event"), u"views"_s, {}, u"j"_s, {}, false,
            W::Terminal),
        act(u"previousEvent"_s, KeyActions::tr("Previous event"), u"views"_s, {}, u"k"_s, {}, false,
            W::Terminal),
        act(u"firstEvent"_s, KeyActions::tr("First event"), u"views"_s, {}, u"gg"_s, {}, false,
            W::Terminal),
        act(u"lastEvent"_s, KeyActions::tr("Last event"), u"views"_s, {}, u"G"_s, {}, false,
            W::Terminal),
        act(u"scrollDown"_s, KeyActions::tr("Scroll down the day"), u"views"_s, {}, u"j"_s, {},
            false, W::App),
        act(u"scrollUp"_s, KeyActions::tr("Scroll up the day"), u"views"_s, {}, u"k"_s, {}, false,
            W::App),
        act(u"top"_s, KeyActions::tr("Go to the start of the day"), u"views"_s, {}, u"gg"_s, {},
            false, W::App),
        act(u"bottom"_s, KeyActions::tr("Go to the end of the day"), u"views"_s, {}, u"G"_s, {},
            false, W::App),
        act(u"dayView"_s, KeyActions::tr("Day view"), u"views"_s, {u"Ctrl+1"_s}, {}, u"d"_s),
        act(u"weekView"_s, KeyActions::tr("Week view"), u"views"_s, {u"Ctrl+2"_s}, {}, u"w"_s),
        act(u"monthView"_s, KeyActions::tr("Month view"), u"views"_s, {u"Ctrl+3"_s}, {}, u"m"_s,
            false, W::App),
        act(u"agendaView"_s, KeyActions::tr("Agenda view"), u"views"_s, {u"Ctrl+4"_s}, {}, u"a"_s),
        act(u"invites"_s, KeyActions::tr("Show invitations"), u"general"_s, {}, {}, u"i"_s),
        act(u"refresh"_s, KeyActions::tr("Sync now"), u"general"_s, {int(K::Refresh)}, {}, u"r"_s),
        act(u"settings"_s, KeyActions::tr("Settings"), u"general"_s,
            {int(K::Preferences), u"Ctrl+,"_s}, {}, u"s"_s, false, W::App),
        act(u"help"_s, KeyActions::tr("Keyboard shortcuts"), u"general"_s, {int(K::HelpContents)},
            u"?"_s, {}),
        act(u"quit"_s, KeyActions::tr("Quit Callie"), u"general"_s, {int(K::Quit)}, {}, {}, false,
            W::App),

        // The terminal calendar's own.
        act(u"nextView"_s, KeyActions::tr("Next view"), u"views"_s, {}, u"Tab"_s, {}, false,
            W::Terminal),
        act(u"details"_s, KeyActions::tr("The event's details"), u"events"_s, {}, u"Enter"_s, {},
            false, W::Terminal),
        act(u"change"_s, KeyActions::tr("Change the event"), u"events"_s, {}, u"e"_s, {}, false,
            W::Terminal),
        act(u"move"_s, KeyActions::tr("Move the event"), u"events"_s, {}, u"m"_s, {}, false,
            W::Terminal),
        act(u"copyEvent"_s, KeyActions::tr("Copy the event"), u"events"_s, {}, u"y"_s, {}, false,
            W::Terminal),
        act(u"pasteEvent"_s, KeyActions::tr("Paste a copy on the day"), u"events"_s, {}, u"p"_s, {},
            false, W::Terminal),
        act(u"duplicateEvent"_s, KeyActions::tr("Duplicate the event"), u"events"_s, {}, u"D"_s, {},
            false, W::Terminal),
        act(u"calendars"_s, KeyActions::tr("Show or hide calendars"), u"general"_s, {}, u"c"_s, {},
            false, W::Terminal),
        act(u"quitTerminal"_s, KeyActions::tr("Quit"), u"general"_s, {}, u"q"_s, {}, false,
            W::Terminal),
        // In an event's details.
        act(u"accept"_s, KeyActions::tr("Answer yes"), u"events"_s, {}, u"y"_s, {}, true,
            W::Terminal),
        act(u"maybe"_s, KeyActions::tr("Answer maybe"), u"events"_s, {}, u"m"_s, {}, true,
            W::Terminal),
        act(u"decline"_s, KeyActions::tr("Answer no"), u"events"_s, {}, u"n"_s, {}, true,
            W::Terminal),
        act(u"join"_s, KeyActions::tr("Join the call"), u"events"_s, {}, u"o"_s, {}, true,
            W::Terminal),
        act(u"delete"_s, KeyActions::tr("Delete the event"), u"events"_s, {}, u"x"_s, {}, true,
            W::Terminal),
        act(u"changeOpen"_s, KeyActions::tr("Change the event"), u"events"_s, {}, u"e"_s, {}, true,
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
