#pragma once

class QCoreApplication;

namespace callie {

/// Puts the app's words in place: the desktop's language where Callie has it, else English as
/// translations/callie_en.ts words it, which falls back to the words in the code.
void installTranslations(QCoreApplication &app);

} // namespace callie
