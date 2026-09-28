#include "Py.h"

#include <QRegularExpression>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gb {

namespace {
bool isPySpace(QChar c) { return c.isSpace() || (c.unicode() >= 0x1c && c.unicode() <= 0x1f); }
} // namespace

QStringList pySplit(const QString& s, int maxsplit)
{
    QStringList out;
    const int n = int(s.size());
    int i = 0;
    while (i < n) {
        while (i < n && isPySpace(s.at(i))) ++i;
        if (i >= n) break;
        if (maxsplit >= 0 && out.size() == maxsplit) {
            out << s.mid(i);        // CPython copies the remainder to the end, trailing spaces included
            return out;
        }
        int j = i;
        while (j < n && !isPySpace(s.at(j))) ++j;
        out << s.mid(i, j - i);
        i = j;
    }
    return out;
}

QStringList pySplitSep(const QString& s, const QString& sep, int maxsplit)
{
    QStringList out;
    int from = 0;
    while (maxsplit < 0 || out.size() < maxsplit) {
        const int at = int(s.indexOf(sep, from));
        if (at < 0) break;
        out << s.mid(from, at - from);
        from = at + int(sep.size());
    }
    out << s.mid(from);
    return out;
}

QString pyStrip(const QString& s)
{
    int a = 0, b = int(s.size());
    while (a < b && isPySpace(s.at(a))) ++a;
    while (b > a && isPySpace(s.at(b - 1))) --b;
    return s.mid(a, b - a);
}

QString pyLStrip(const QString& s)
{
    int a = 0;
    while (a < s.size() && isPySpace(s.at(a))) ++a;
    return s.mid(a);
}

QString pyStripChars(const QString& s, const QString& chars)
{
    int a = 0, b = int(s.size());
    while (a < b && chars.contains(s.at(a))) ++a;
    while (b > a && chars.contains(s.at(b - 1))) --b;
    return s.mid(a, b - a);
}

QString pyQ(const QString& s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + out + QLatin1Char('"');
}

QString pyUQ(const QString& s) { return QLatin1Char('u') + pyQ(s); }

QString pyFixed(double v, int digits)
{
    if (std::isnan(v)) return QStringLiteral("nan");
    if (std::isinf(v)) return v < 0 ? QStringLiteral("-inf") : QStringLiteral("inf");
    // exact decimal expansion, then round half to even at `digits` (what Python's dtoa does)
    char buf[512];
    std::snprintf(buf, sizeof buf, "%.120f", v);
    QString s = QString::fromLatin1(buf);
    const bool neg = s.startsWith(QLatin1Char('-'));
    if (neg) s.remove(0, 1);
    const int dot = int(s.indexOf(QLatin1Char('.')));
    QString intPart = s.left(dot), frac = s.mid(dot + 1);
    QString keep = frac.left(digits);
    const QString rest = frac.mid(digits);
    bool roundUp = false;
    if (!rest.isEmpty()) {
        const QChar first = rest.at(0);
        if (first > QLatin1Char('5')) roundUp = true;
        else if (first == QLatin1Char('5')) {
            bool tail = false;
            for (int i = 1; i < rest.size(); ++i) if (rest.at(i) != QLatin1Char('0')) { tail = true; break; }
            if (tail) roundUp = true;
            else {
                const QChar last = digits > 0 ? keep.at(digits - 1) : intPart.at(intPart.size() - 1);
                roundUp = (last.digitValue() % 2) == 1;
            }
        }
    }
    QString all = intPart + keep;
    if (roundUp) {
        int i = int(all.size()) - 1;
        while (i >= 0) {
            if (all.at(i) == QLatin1Char('9')) { all[i] = QLatin1Char('0'); --i; }
            else { all[i] = QChar(all.at(i).unicode() + 1); break; }
        }
        if (i < 0) all.prepend(QLatin1Char('1'));
    }
    const int ip = int(all.size()) - digits;
    QString out = all.left(ip);
    if (digits > 0) out += QLatin1Char('.') + all.mid(ip);
    bool zero = true;
    for (const QChar c : all) if (c != QLatin1Char('0')) { zero = false; break; }
    return (neg && !zero) || (neg && std::signbit(v)) ? QLatin1Char('-') + out : out;
}

QString pyRepr(double v)
{
    if (std::isnan(v)) return QStringLiteral("nan");
    if (std::isinf(v)) return v < 0 ? QStringLiteral("-inf") : QStringLiteral("inf");
    if (v == 0) return std::signbit(v) ? QStringLiteral("-0.0") : QStringLiteral("0.0");
    // shortest round-trip digits
    char buf[64];
    QString digits;
    int exp10 = 0;
    for (int prec = 1; prec <= 17; ++prec) {
        std::snprintf(buf, sizeof buf, "%.*e", prec - 1, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    QString e = QString::fromLatin1(buf);
    const bool neg = e.startsWith(QLatin1Char('-'));
    if (neg) e.remove(0, 1);
    const int epos = int(e.indexOf(QLatin1Char('e')));
    exp10 = e.mid(epos + 1).toInt();
    digits = e.left(epos).remove(QLatin1Char('.'));
    while (digits.size() > 1 && digits.endsWith(QLatin1Char('0'))) digits.chop(1);
    QString out;
    if (exp10 >= -4 && exp10 < 16) {       // Python repr: positional for 1e-4 <= |v| < 1e16
        if (exp10 >= 0) {
            QString ip = digits.left(exp10 + 1);
            while (ip.size() < exp10 + 1) ip += QLatin1Char('0');
            const QString fp = digits.mid(exp10 + 1);
            out = ip + QLatin1Char('.') + (fp.isEmpty() ? QStringLiteral("0") : fp);
        } else {
            out = QStringLiteral("0.") + QString(-exp10 - 1, QLatin1Char('0')) + digits;
        }
        return neg ? QLatin1Char('-') + out : out;
    }
    out = digits.left(1);
    if (digits.size() > 1) out += QLatin1Char('.') + digits.mid(1);
    const QString ex = QString::number(std::abs(exp10)).rightJustified(2, QLatin1Char('0'));
    out += QStringLiteral("e") + (exp10 < 0 ? QLatin1Char('-') : QLatin1Char('+')) + ex;
    return neg ? QLatin1Char('-') + out : out;
}

bool pyFloat(const QString& s, double* out)
{
    QString t = pyStrip(s);
    t.remove(QLatin1Char('_'));
    const QString low = t.toLower();
    if (low == QLatin1String("inf") || low == QLatin1String("+inf") || low == QLatin1String("infinity")) { *out = INFINITY; return true; }
    if (low == QLatin1String("-inf") || low == QLatin1String("-infinity")) { *out = -INFINITY; return true; }
    if (low == QLatin1String("nan") || low == QLatin1String("+nan") || low == QLatin1String("-nan")) { *out = NAN; return true; }
    static const QRegularExpression num(QStringLiteral("^[+-]?(\\d+(\\.\\d*)?|\\.\\d+)([eE][+-]?\\d+)?$"));
    if (!num.match(t).hasMatch()) return false;
    bool ok = false;
    *out = t.toDouble(&ok);
    return ok;
}

long long pyRound(double v) { return (long long)std::nearbyint(v); }

QString pyBasename(const QString& path)
{
    const int a = int(path.lastIndexOf(QLatin1Char('/'))), b = int(path.lastIndexOf(QLatin1Char('\\')));
    const int cut = qMax(a, b);
    QString p = cut >= 0 ? path.mid(cut + 1) : path;
    if (p.size() >= 2 && p.at(1) == QLatin1Char(':') && cut < 0) p = p.mid(2);   // "C:name"
    return p;
}

bool pyIsNumber(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9]+(\\.[0-9]+)?$"));
    QString t = s;
    if (t.endsWith(QLatin1Char('\n'))) t.chop(1);      // Python's $ also matches before a final newline
    return re.match(t).hasMatch();
}

bool pyIsHexColor(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    QString t = s;
    if (t.endsWith(QLatin1Char('\n'))) t.chop(1);
    return re.match(t).hasMatch();
}

} // namespace gb
