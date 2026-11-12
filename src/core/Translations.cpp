#include "callie/Translations.h"

#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>

using namespace Qt::StringLiterals;

namespace callie {

void installTranslations(QCoreApplication &app)
{
    auto *words = new QTranslator(&app);
    if (words->load(QLocale(), u"callie"_s, u"_"_s, u":/i18n"_s) ||
        words->load(u"callie_en"_s, u":/i18n"_s))
        QCoreApplication::installTranslator(words);
    else
        delete words;
}

} // namespace callie
