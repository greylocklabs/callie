#pragma once

#include "callie/CalendarSource.h"
#include "callie/Event.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QSet>
#include <QUrl>

#include <functional>
#include <optional>

namespace callie {

class Settings;

/// The current or next call, for the card that offers to join it: from a few minutes before
/// it starts until it ends, unless the card is turned off in settings or this one was dismissed.
class CallOffer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(callie::CalendarSource *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    /// When the call is, such as "In 3 min, 10:00 to 10:30".
    Q_PROPERTY(QString when READ when NOTIFY changed)
    Q_PROPERTY(QUrl joinUrl READ joinUrl NOTIFY changed)
    /// "zoom", "meet", "teams" or "web", as the event popup names the call.
    Q_PROPERTY(QString service READ service NOTIFY changed)

public:
    using Now = std::function<QDateTime()>;

    explicit CallOffer(QObject *parent = nullptr);

    [[nodiscard]] CalendarSource *source() const { return m_source; }
    void setSource(CalendarSource *source);
    /// Replaces the app's settings and clock, for tests.
    void setSettings(Settings *settings);
    void setNow(Now now);

    [[nodiscard]] bool active() const { return m_call.has_value(); }
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString when() const;
    [[nodiscard]] QUrl joinUrl() const;
    [[nodiscard]] QString service() const;

    Q_INVOKABLE void join();
    /// Hides the card for this call only; the next call shows it again.
    Q_INVOKABLE void dismiss();

    /// Looks again for the call to offer. Runs by itself as time passes and calendars change;
    /// public so tests can step the clock.
    void check();

Q_SIGNALS:
    void sourceChanged();
    void changed();

private:
    void offer(std::optional<Event> call);

    QPointer<CalendarSource> m_source;
    QPointer<Settings> m_settings;
    Now m_now;
    std::optional<Event> m_call;
    QString m_when;
    QSet<QString> m_dismissed;
    bool m_reading = false;
    bool m_checkAgain = false;
};

} // namespace callie
