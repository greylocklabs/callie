#pragma once

#include "callie/KeyActions.h"

#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantList>

namespace callie {

/// Turns typed keys into actions. Standard shortcuts are left to QML's
/// Shortcut, made from actions(); in vi mode single keys act too, and the
/// leader key starts a second key for the commands that have none of their
/// own. Keys typed into a text field, or into anything open over the window,
/// are left alone.
class KeyRouter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool viMode MEMBER m_viMode NOTIFY settingsChanged)
    Q_PROPERTY(QString leaderKey MEMBER m_leaderKey NOTIFY settingsChanged)
    /// Milliseconds the leader waits for the next key; 0 waits until Escape.
    Q_PROPERTY(int leaderTimeout MEMBER m_leaderTimeout NOTIFY settingsChanged)
    /// "leader" or "g" while waiting for the key that finishes a command, else empty.
    Q_PROPERTY(QString pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(QVariantList actions READ actionList CONSTANT)

public:
    explicit KeyRouter(QObject *parent = nullptr);

    [[nodiscard]] static QList<KeyAction> actions();
    /// actions() for QML: maps with the fields of KeyAction, plus `keys`, the
    /// standard shortcuts as the platform writes them.
    [[nodiscard]] static QVariantList actionList();

    [[nodiscard]] QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    [[nodiscard]] QString pending() const { return m_pending; }

    /// Feeds one typed key in vi mode. Returns whether the key was used.
    Q_INVOKABLE bool press(const QString &text);
    /// Stops waiting for the rest of a command.
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void triggered(const QString &id);
    void windowChanged();
    void settingsChanged();
    void pendingChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setPending(const QString &pending);

    QPointer<QQuickWindow> m_window;
    bool m_viMode = false;
    QString m_leaderKey = QStringLiteral(",");
    int m_leaderTimeout = 5000;
    QString m_pending;
    QTimer m_timeout;
};

} // namespace callie
