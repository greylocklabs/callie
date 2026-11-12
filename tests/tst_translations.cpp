#include "callie/Translations.h"

#include <QCoreApplication>
#include <QFile>
#include <QTest>
#include <QTranslator>

using namespace Qt::StringLiterals;

class TestTranslations : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void installsTheEnglishWording();
};

void TestTranslations::installsTheEnglishWording()
{
    // The wording compiled from translations/callie_en.ts travels inside the app.
    QVERIFY(QFile::exists(u":/i18n/callie_en.qm"_s));
    callie::installTranslations(*QCoreApplication::instance());
    // KCalendarCore puts in its own translator too, so only Callie's are counted.
    QStringList ours;
    for (const QTranslator *words : QCoreApplication::instance()->findChildren<QTranslator *>()) {
        if (words->filePath().startsWith(u":/i18n/"_s))
            ours.append(words->filePath());
    }
    QCOMPARE(ours, QStringList{u":/i18n/callie_en.qm"_s});
}

QTEST_GUILESS_MAIN(TestTranslations)
#include "tst_translations.moc"
