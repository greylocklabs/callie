#pragma once

#include "Event.h"

#include <QDateTime>
#include <QSet>
#include <QStringList>

#include <chrono>
#include <optional>

namespace callie {

/// How early before its start a call is offered.
inline constexpr std::chrono::minutes kCallLead{5};

/// Names one occurrence of an event, for remembering it was dismissed.
[[nodiscard]] QString callKey(const Event &event);

/// Whether `url` is a call link a browser can open.
[[nodiscard]] bool canJoin(const QUrl &url);

/// Which service a call link joins: "zoom", "meet", "teams", "web" for any other http(s) link,
/// or empty for a link Callie should not open.
[[nodiscard]] QString callService(const QUrl &url);

/// The call to offer joining at `now`: a timed event with a call link that is on, or starts within
/// `kCallLead`, in a calendar not hidden, that the user has not declined or dismissed. When calls
/// overlap, the one starting last is the one about to matter.
[[nodiscard]] std::optional<Event> nextCall(const QList<Event> &events, const QDateTime &now,
                                            const QStringList &hiddenCalendars,
                                            const QSet<QString> &dismissed);

} // namespace callie
