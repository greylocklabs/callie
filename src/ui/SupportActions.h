#pragma once

#include "callie/TokenStore.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QQmlEngine>

#include <functional>

namespace callie {

/// The title bar's help menu: debug info, the logs folder and problem reports.
class SupportActions : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Support)
    QML_SINGLETON

public:
    using QObject::QObject;

    /// Puts the `callie doctor` report on the clipboard, then emits copied().
    Q_INVOKABLE void copyDebugInfo();
    Q_INVOKABLE void openLogs();
    /// Opens the folder holding the settings ("config"), the change queue
    /// ("data") or the event cache and contacts ("cache").
    Q_INVOKABLE void openFolder(const QString &which);
    /// Puts `text` on the clipboard, then emits copied().
    Q_INVOKABLE void copyText(const QString &text);
    /// Opens a new GitHub issue with the report filled in.
    Q_INVOKABLE void reportBug();
    /// Gathers the `callie doctor` report for "Report a problem" to show, then
    /// emits diagnosticsReady().
    Q_INVOKABLE void gatherDiagnostics();
    /// Sends a problem report to the report service, then emits reportSent()
    /// with the new issue, or reportFailed() with why it was not filed.
    Q_INVOKABLE void sendReport(bool idea, const QString &summary, const QString &note,
                                const QString &diagnostics);

Q_SIGNALS:
    void copied();
    void diagnosticsReady(const QString &text);
    void reportSent(const QUrl &issue);
    void reportFailed(const QString &error);

private:
    void collect(std::function<void(const QString &)> done);

    KeychainTokenStore m_tokens;
    QNetworkAccessManager m_network;
};

} // namespace callie
