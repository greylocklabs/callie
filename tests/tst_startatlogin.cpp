#include "StartAtLoginSetting.h"

#include "callie/Settings.h"
#include "greylock/Autostart.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestStartAtLogin : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void followsKeepRunning();
};

void TestStartAtLogin::followsKeepRunning()
{
    QTemporaryDir dir;
    Settings settings(dir.filePath(u"settings.ini"_s));
    settings.setKeepRunning(true);
    const QString entry = dir.filePath(u"autostart/app.desktop"_s);
    greylock::Autostart autostart(u"org.example.App"_s, u"/usr/bin/callie-gui"_s, u"Reminders"_s,
                                  entry);
    StartAtLoginSetting::setup(&autostart, &settings);
    StartAtLoginSetting startAtLogin;
    QVERIFY(startAtLogin.available());

    startAtLogin.setEnabled(true);
    QVERIFY(startAtLogin.enabled());
    QVERIFY(QFile::exists(entry));

    // No longer running in the background, so nothing to start at login.
    settings.setKeepRunning(false);
    QVERIFY(!startAtLogin.enabled());
    QVERIFY(!QFile::exists(entry));
    QVERIFY(startAtLogin.error().isEmpty());
    greylock::StartAtLogin::instance()->setup(nullptr);
}

QTEST_GUILESS_MAIN(TestStartAtLogin)
#include "tst_startatlogin.moc"
