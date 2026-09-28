// GenryBL V1 - string helpers that mirror the Python semantics the old builder relied on.
#pragma once
#include <QString>
#include <QStringList>

namespace gb {

// Python str.splitlines(): keeps empty lines, drops only the terminal break.
QStringList pySplitLines(const QString& text);
// read_text() used utf-8-sig: a leading BOM is not part of the story
QString stripBom(const QString& text);
// Python str.split() with no args: split on whitespace runs, no empty items.
QStringList splitWords(const QString& s);
QString firstWord(const QString& s);
// Python builder q(): escape backslash + double quote, wrap in quotes.
QString pyQuote(const QString& s);
// Russian/Ukrainian -> latin, readable (сцена -> stsena, счёт -> schyot).
QString translit(const QString& s);
// Python builder slug(). legacy=true reproduces the old behaviour exactly
// (Cyrillic collapses to the fallback); legacy=false transliterates first so
// `сцена Клуб` becomes `label klub` instead of a duplicate `label scene`.
QString slug(const QString& s, const QString& fallback, bool legacy);
bool startsWithAnyCI(const QString& s, const QStringList& prefixes);

} // namespace gb
