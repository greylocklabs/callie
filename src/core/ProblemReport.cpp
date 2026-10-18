#include "callie/ProblemReport.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>

using namespace Qt::StringLiterals;

namespace callie::problemreport {

namespace {

// The service's own limits; anything longer is refused rather than cut.
constexpr qsizetype kSummary = 100;
constexpr qsizetype kNote = 4000;
constexpr qsizetype kLogs = 16000;

/// Logs keep their end, where the failure usually is.
QString lastPart(const QString &text, qsizetype max)
{
    if (text.size() <= max)
        return text;
    const QString mark = u"[earlier lines left out]\n"_s;
    return mark + text.right(max - mark.size());
}

} // namespace

QUrl serviceUrl()
{
    const QString override = qEnvironmentVariable("CALLIE_REPORT_SERVICE");
    return QUrl(override.isEmpty()
                    ? u"https://greylocklabs-reports.lararosekelley.workers.dev/report"_s
                    : override);
}

QByteArray payload(const Report &report, const QString &appVersion, const QString &platform)
{
    QJsonObject body{
        {u"app"_s, u"callie"_s},
        {u"kind"_s, report.idea ? u"idea"_s : u"bug"_s},
        {u"summary"_s, report.summary.trimmed().left(kSummary)},
        {u"appVersion"_s, appVersion.left(40)},
        {u"platform"_s, platform.left(100)},
    };
    if (!report.note.trimmed().isEmpty())
        body[u"note"_s] = report.note.trimmed().left(kNote);
    if (!report.diagnostics.trimmed().isEmpty())
        body[u"logs"_s] = lastPart(report.diagnostics.trimmed(), kLogs);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void send(QNetworkAccessManager &network, const QUrl &service, const Report &report,
          std::function<void(const QUrl &issue, const QString &error)> done)
{
    QNetworkRequest request(service);
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      u"Callie/"_s + QCoreApplication::applicationVersion());
    request.setTransferTimeout(20000);
    QNetworkReply *reply =
        network.post(request, payload(report, QCoreApplication::applicationVersion(),
                                      QSysInfo::prettyProductName()));
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done = std::move(done)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll()).object();
        const QUrl issue(answer.value(u"url"_s).toString());
        if (status == 201 && issue.isValid() && !issue.isEmpty()) {
            done(issue, {});
            return;
        }
        if (status == 429) {
            done({}, QCoreApplication::translate(
                         "ProblemReport", "Too many reports from here. Try again in a minute."));
            return;
        }
        if (status == 0) {
            done({}, QCoreApplication::translate("ProblemReport",
                                                 "Could not reach the report service: %1")
                         .arg(reply->errorString()));
            return;
        }
        const QString reason = answer.value(u"error"_s).toString();
        done({}, QCoreApplication::translate("ProblemReport", "The report service said no: %1")
                     .arg(reason.isEmpty() ? QString::number(status) : reason));
    });
}

} // namespace callie::problemreport
