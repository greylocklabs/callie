#pragma once

#include <QString>
#include <QUrl>

#include <functional>

#include <functional>

namespace callie {

class TokenStore;

/// What a bug report needs: versions, environment, account and sync state,
/// and the end of each log. Email addresses are masked throughout.
namespace diagnostics {

/// Replaces each distinct email address with <email-1>, <email-2> and so on,
/// and the home folder with ~.
[[nodiscard]] QString redact(const QString &text);

/// Builds the report. Checking the keyring is asynchronous, so `done` is
/// called once, later. `extra` adds lines such as the GUI's platform plugin.
void collect(TokenStore &tokens, const QString &extra, std::function<void(QString)> done);

/// A new GitHub issue with `report` filled in, shortened to fit in a URL.
[[nodiscard]] QUrl issueUrl(const QString &report);

/// The longest new-issue address GitHub accepts.
inline constexpr qsizetype kMaxIssueUrlLength = 8000;

/// What issueUrl() ends a shortened report with: where to find the rest.
[[nodiscard]] QString shortenedMark();

/// The address `build` makes from `text`, with `text` cut from its end and
/// `mark` added until the address fits in a URL GitHub accepts.
[[nodiscard]] QUrl fitIssueUrl(const std::function<QUrl(const QString &)> &build,
                               const QString &text, const QString &mark);

} // namespace diagnostics
} // namespace callie
