// GenryBL V1 - the Python semantics the old builder.py relied on, so the C++ port
// can reproduce its output byte for byte (legacy mode) before improving on it.
#pragma once
#include <QString>
#include <QStringList>

namespace gb {

// str.split(None, maxsplit): whitespace runs, leading space skipped, the tail keeps its inner spacing
QStringList pySplit(const QString& s, int maxsplit = -1);
// str.split(sep, maxsplit)
QStringList pySplitSep(const QString& s, const QString& sep, int maxsplit = -1);
QString pyStrip(const QString& s);
QString pyLStrip(const QString& s);
QString pyStripChars(const QString& s, const QString& chars);
QString pyQ(const QString& s);                   // q()
QString pyUQ(const QString& s);                  // uq() = u + q()
QString pyFixed(double v, int digits);           // "%.Nf" % v (correctly rounded, ties to even)
QString pyRepr(double v);                        // str(float) / repr(float)
bool pyFloat(const QString& s, double* out);     // float(s)
long long pyRound(double v);                     // round() -> int, ties to even
QString pyBasename(const QString& path);         // ntpath.basename
bool pyIsNumber(const QString& s);               // re.match(r"^[0-9]+(\.[0-9]+)?$")
bool pyIsHexColor(const QString& s);             // re.match(r"^#[0-9a-fA-F]{6}$")

} // namespace gb
