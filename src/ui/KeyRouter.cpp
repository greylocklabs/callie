#include "KeyRouter.h"

#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

// A text field takes its own keys, and so does anything open over the
// window, such as an event's card or a menu.
bool keysTaken(const QQuickWindow *window)
{
    const QQuickItem *item = window ? window->activeFocusItem() : nullptr;
    if (item && (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit")))
        return true;
    for (; item; item = item->parentItem()) {
        if (item->inherits("QQuickPopupItem"))
            return true;
    }
    return false;
}

} // namespace

KeyRouter::KeyRouter(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &KeyRouter::cancel);
    connect(this, &KeyRouter::settingsChanged, this, [this] {
        if (!m_viMode)
            cancel();
    });
}

QList<KeyAction> KeyRouter::actions()
{
    return keyActionsFor(KeyAction::Where::App);
}

QVariantList KeyRouter::actionList()
{
    QVariantList list;
    for (const KeyAction &action : actions()) {
        QStringList keys;
        for (const QVariant &key : action.standard) {
            const QKeySequence sequence = key.typeId() == QMetaType::Int
                                              ? QKeySequence(QKeySequence::StandardKey(key.toInt()))
                                              : QKeySequence(key.toString());
            if (!sequence.isEmpty())
                keys << sequence.toString(QKeySequence::NativeText);
        }
        list << QVariantMap{{u"id"_s, action.id},
                            {u"label"_s, action.label},
                            {u"group"_s, action.group},
                            {u"standard"_s, action.standard},
                            {u"keys"_s, keys},
                            {u"vi"_s, action.vi},
                            {u"leader"_s, action.leader},
                            {u"inCard"_s, action.inCard}};
    }
    return list;
}

void KeyRouter::setWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
    Q_EMIT windowChanged();
}

bool KeyRouter::press(const QString &text)
{
    if (!m_viMode || text.isEmpty())
        return false;
    const QList<KeyAction> all = actions();
    const auto find = [&all](auto matches) -> QString {
        for (const KeyAction &action : all) {
            if (matches(action))
                return action.id;
        }
        return {};
    };

    if (m_pending == u"leader") {
        const QString id = find([&text](const KeyAction &a) { return a.leader == text; });
        cancel();
        if (!id.isEmpty())
            Q_EMIT triggered(id);
        // Whatever followed the leader was meant for it.
        return true;
    }
    if (m_pending == u"g") {
        cancel();
        const QString id = find([&text](const KeyAction &a) { return a.vi == u"g"_s + text; });
        if (!id.isEmpty()) {
            Q_EMIT triggered(id);
            return true;
        }
    }

    if (text == m_leaderKey) {
        setPending(u"leader"_s);
        if (m_leaderTimeout > 0)
            m_timeout.start(m_leaderTimeout);
        return true;
    }
    if (text == u"g") {
        setPending(u"g"_s);
        m_timeout.start(std::max(m_leaderTimeout, 1000));
        return true;
    }
    const QString id = find([&text](const KeyAction &a) { return a.vi == text; });
    if (id.isEmpty())
        return false;
    Q_EMIT triggered(id);
    return true;
}

void KeyRouter::cancel()
{
    m_timeout.stop();
    setPending({});
}

void KeyRouter::setPending(const QString &pending)
{
    if (m_pending == pending)
        return;
    m_pending = pending;
    Q_EMIT pendingChanged();
}

bool KeyRouter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress || !m_viMode || keysTaken(m_window))
        return QObject::eventFilter(watched, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Escape && !m_pending.isEmpty()) {
        cancel();
        return true;
    }
    // Shortcuts with Ctrl or Alt are QML's, and Space presses a focused button
    // unless a command is under way.
    if (key->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier))
        return QObject::eventFilter(watched, event);
    const QQuickItem *focus = m_window ? m_window->activeFocusItem() : nullptr;
    if (key->key() == Qt::Key_Space && m_pending.isEmpty() && focus &&
        focus->inherits("QQuickAbstractButton"))
        return QObject::eventFilter(watched, event);
    return press(key->text()) || QObject::eventFilter(watched, event);
}

} // namespace callie
