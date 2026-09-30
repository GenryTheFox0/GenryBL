#pragma once
// A string a person sees, written in Russian and translated at run time by data/i18n/<lang>.json
// (src/app/I18n.h installs the translator; without one - the tests, gb_cli - it stays Russian).
#include <QCoreApplication>
#include <QString>

inline QString gbTr(const char* s) { return QCoreApplication::translate("GenryBL", s); }
