#include "Text.h"

#include <QHash>
#include <QRegularExpression>

namespace gb {

QStringList pySplitLines(const QString& text)
{
    if (text.isEmpty())
        return {};
    static const QRegularExpression br(QStringLiteral("\r\n|\n|\r"));
    QStringList lines = text.split(br);
    if (!lines.isEmpty() && lines.last().isEmpty() &&
        (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r'))))
        lines.removeLast();
    return lines;
}

QString stripBom(const QString& text)
{
    return text.startsWith(QChar(0xFEFF)) ? text.mid(1) : text;
}

QStringList splitWords(const QString& s)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return s.trimmed().split(ws, Qt::SkipEmptyParts);
}

QString firstWord(const QString& s)
{
    const QStringList p = splitWords(s);
    return p.isEmpty() ? QString() : p.first();
}

QString pyQuote(const QString& s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + out + QLatin1Char('"');
}

QString translit(const QString& s)
{
    static const QHash<QChar, QString> map = [] {
        QHash<QChar, QString> m;
        const char* pairs[][2] = {
            {"а", "a"}, {"б", "b"}, {"в", "v"}, {"г", "g"}, {"д", "d"}, {"е", "e"}, {"ё", "yo"},
            {"ж", "zh"}, {"з", "z"}, {"и", "i"}, {"й", "y"}, {"к", "k"}, {"л", "l"}, {"м", "m"},
            {"н", "n"}, {"о", "o"}, {"п", "p"}, {"р", "r"}, {"с", "s"}, {"т", "t"}, {"у", "u"},
            {"ф", "f"}, {"х", "kh"}, {"ц", "ts"}, {"ч", "ch"}, {"ш", "sh"}, {"щ", "shch"},
            {"ъ", ""}, {"ы", "y"}, {"ь", ""}, {"э", "e"}, {"ю", "yu"}, {"я", "ya"},
            {"і", "i"}, {"ї", "yi"}, {"є", "ye"}, {"ґ", "g"},
        };
        for (const auto& p : pairs)
            m.insert(QString::fromUtf8(p[0]).at(0), QString::fromUtf8(p[1]));
        return m;
    }();
    QString out;
    out.reserve(s.size() * 2);
    for (const QChar c : s) {
        const QChar lc = c.toLower();
        auto it = map.constFind(lc);
        out += (it != map.constEnd()) ? *it : QString(c);
    }
    return out;
}

QString slug(const QString& s, const QString& fallback, bool legacy)
{
    QString v = s.trimmed().toLower();
    if (!legacy)
        v = translit(v);
    static const QRegularExpression bad(QStringLiteral("[^a-z0-9_]+"));
    static const QRegularExpression under(QStringLiteral("_+"));
    v.replace(bad, QStringLiteral("_"));
    v.replace(under, QStringLiteral("_"));
    while (v.startsWith(QLatin1Char('_'))) v.remove(0, 1);
    while (v.endsWith(QLatin1Char('_'))) v.chop(1);
    return v.isEmpty() ? fallback : v;
}

bool startsWithAnyCI(const QString& s, const QStringList& prefixes)
{
    const QString low = s.toLower();
    for (const QString& p : prefixes)
        if (low.startsWith(p))
            return true;
    return false;
}

} // namespace gb
