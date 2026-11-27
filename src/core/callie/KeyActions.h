#pragma once

#include <QList>
#include <QString>
#include <QVariantList>

namespace callie {

/// Something the keyboard can do, and every way to do it.
struct KeyAction
{
    /// Where it works: the desktop app, the terminal calendar, or both.
    enum class Where { App, Terminal, Both };

    QString id;
    QString label;
    /// "general", "views" or "events", for grouping in the help.
    QString group;
    /// The app's shortcuts that always work, as QKeySequence::StandardKey values or text.
    QVariantList standard;
    /// The keys for it in vi mode, and in the terminal calendar: "j", "gg", or a key's name
    /// such as "Enter".
    QString vi;
    /// The key after the leader.
    QString leader;
    /// Only works while an event's card, or its details in the terminal, is open.
    bool inCard = false;
    Where where = Where::Both;
};

/// Every action of the app and the terminal calendar, in the order the help lists them. What
/// both do is one entry, so the same keys do it in each.
[[nodiscard]] QList<KeyAction> keyActions();
/// The actions that work in `where`, the app or the terminal calendar.
[[nodiscard]] QList<KeyAction> keyActionsFor(KeyAction::Where where);

} // namespace callie
