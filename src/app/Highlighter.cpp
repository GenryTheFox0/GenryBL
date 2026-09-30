#include "Highlighter.h"
#include "Screenplay.h"
#include "Compiler.h"
#include "Engine.h"
#include "Text.h"

#include <QRegularExpression>
#include <QTextDocument>

using namespace gb;

namespace {

QColor categoryColor(const QString& cat)
{
    static const QHash<QString, QColor> c{
        {QStringLiteral("Сцены"), QColor(0xff, 0x8a, 0x80)}, {QStringLiteral("Кадр"), QColor(0x80, 0xde, 0xea)},
        {QStringLiteral("Диалог"), QColor(0xff, 0xdd, 0x7d)}, {QStringLiteral("Звук"), QColor(0x9b, 0xd3, 0x5a)}, {QStringLiteral("Звук и видео"), QColor(0x9b, 0xd3, 0x5a)},
        {QStringLiteral("Эффекты"), QColor(0xce, 0x93, 0xd8)}, {QStringLiteral("Карточки"), QColor(0xff, 0xab, 0x91)},
        {QStringLiteral("Логика"), QColor(0x90, 0xca, 0xf9)}, {QStringLiteral("Телефон"), QColor(0x7d, 0xd3, 0xfc)},
        {QStringLiteral("Отношения"), QColor(0xff, 0x7a, 0x9c)}, {QStringLiteral("Инвентарь"), QColor(0xc5, 0xe1, 0xa5)},
        {QStringLiteral("Меню мода"), QColor(0xff, 0xd2, 0x7d)}};
    return c.value(cat, QColor(0xb0, 0xbe, 0xc5));
}

// compiler command id -> palette category, taken once from the command forms: the command
// word of every variant («показать», «зеркало», «идти», …) gets its form's colour
QString categoryOf(const QString& id)
{
    static QHash<QString, QString> map;
    if (map.isEmpty() && Engine::instance())
        for (const QVariant& fv : Engine::instance()->forms().forms()) {
            const QVariantMap f = fv.toMap();
            QStringList ts{f.value(QStringLiteral("build")).toString()};
            for (const QVariant& b : f.value(QStringLiteral("builds")).toMap()) ts << b.toString();
            for (const QString& t : ts)
                for (const QString& line : t.split(QLatin1Char('\n'))) {
                    int i = 0;
                    while (i < line.size() && !QStringLiteral(" [{").contains(line.at(i))) ++i;
                    const QString cmd = normalizeCommand(line.left(i));
                    if (i > 0 && !map.contains(cmd)) map.insert(cmd, f.value(QStringLiteral("cat")).toString());
                }
        }
    static const QHash<QString, QString> extra{{QStringLiteral("endchoice"), QStringLiteral("Сцены")}, {QStringLiteral("endmodmenu"), QStringLiteral("Меню мода")},
                                               {QStringLiteral("variable"), QStringLiteral("Логика")},
                                               {QStringLiteral("voice"), QStringLiteral("Звук")}, {QStringLiteral("voicefile"), QStringLiteral("Звук")},
                                               {QStringLiteral("soundqueue"), QStringLiteral("Звук")}, {QStringLiteral("stop"), QStringLiteral("Звук")},
                                               {QStringLiteral("noisefx"), QStringLiteral("Эффекты")}, {QStringLiteral("pixelfx"), QStringLiteral("Эффекты")},
                                               {QStringLiteral("dream"), QStringLiteral("Эффекты")}, {QStringLiteral("stopdream"), QStringLiteral("Эффекты")},
                                               {QStringLiteral("stopsleepyeyes"), QStringLiteral("Эффекты")}, {QStringLiteral("notedim"), QStringLiteral("Эффекты")},
                                               {QStringLiteral("notebg"), QStringLiteral("Кадр")}, {QStringLiteral("notenvl"), QStringLiteral("Диалог")},
                                               {QStringLiteral("noteadv"), QStringLiteral("Диалог")}, {QStringLiteral("closenote"), QStringLiteral("Диалог")},
                                               {QStringLiteral("hidethought"), QStringLiteral("Диалог")}, {QStringLiteral("mirrorbig"), QStringLiteral("Кадр")},
                                               {QStringLiteral("zone"), QStringLiteral("Сцены")}, {QStringLiteral("chibi"), QStringLiteral("Сцены")},
                                               {QStringLiteral("disablezone"), QStringLiteral("Сцены")}, {QStringLiteral("resetzone"), QStringLiteral("Сцены")},
                                               {QStringLiteral("achievement"), QStringLiteral("Логика")}, {QStringLiteral("stopvideobg"), QStringLiteral("Прочее")}};
    return map.value(id, extra.value(id));
}

QColor speakerColor(const QString& id)
{
    static const QHash<QString, QColor> c{{QStringLiteral("dv"), QColor(0xff, 0xaa, 0x00)}, {QStringLiteral("un"), QColor(0xb9, 0x56, 0xff)},
                                          {QStringLiteral("sl"), QColor(0xff, 0xd2, 0x00)}, {QStringLiteral("mi"), QColor(0x00, 0xde, 0xff)},
                                          {QStringLiteral("us"), QColor(0xff, 0x32, 0x00)}, {QStringLiteral("mt"), QColor(0x00, 0xea, 0x32)},
                                          {QStringLiteral("el"), QColor(0xff, 0xff, 0x00)}, {QStringLiteral("sh"), QColor(0xff, 0xe7, 0x9c)},
                                          {QStringLiteral("mz"), QColor(0x4a, 0x86, 0xff)}, {QStringLiteral("uv"), QColor(0x4e, 0xe1, 0x42)},
                                          {QStringLiteral("cs"), QColor(0xa5, 0xa5, 0xff)}, {QStringLiteral("me"), QColor(0xe1, 0xdd, 0x7d)},
                                          {QStringLiteral("genry"), QColor(0xff, 0xcc, 0x66)}, {QStringLiteral("scar"), QColor(0xff, 0x66, 0x66)}};
    return c.value(id, QColor(0xe8, 0xe8, 0xe8));
}

QTextCharFormat fmt(const QColor& color, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(color);
    if (bold) f.setFontWeight(QFont::Bold);
    f.setFontItalic(italic);
    return f;
}

} // namespace

StoryHighlighter::StoryHighlighter(QObject* parent)
    : QSyntaxHighlighter(parent)
{
}

void StoryHighlighter::setDocument(QQuickTextDocument* doc)
{
    if (m_doc == doc) return;
    m_doc = doc;
    QSyntaxHighlighter::setDocument(doc ? doc->textDocument() : nullptr);
    emit documentChanged();
}

void StoryHighlighter::setFindText(const QString& t)
{
    if (t == m_find) return;
    m_find = t;
    rehighlight();
    emit findChanged();
}

void StoryHighlighter::setFindCase(bool on)
{
    if (on == m_findCase) return;
    m_findCase = on;
    if (!m_find.isEmpty()) rehighlight();
    emit findChanged();
}

int StoryHighlighter::replaceAll(const QString& find, const QString& with, bool caseSensitive)
{
    QTextDocument* doc = m_doc ? m_doc->textDocument() : nullptr;
    if (!doc || find.isEmpty()) return 0;
    const QTextDocument::FindFlags flags = caseSensitive ? QTextDocument::FindCaseSensitively : QTextDocument::FindFlags();
    QTextCursor block(doc);
    block.beginEditBlock();                        // the whole replace is one undo step
    int n = 0;
    QTextCursor at(doc);
    for (;;) {
        at = doc->find(find, at, flags);
        if (at.isNull()) break;
        at.insertText(with);                       // the cursor ends after the new text: «a» -> «aa» never loops
        ++n;
    }
    block.endEditBlock();
    return n;
}

void StoryHighlighter::setIssues(const QVariantList& issues)
{
    if (issues == m_issues) return;
    m_issues = issues;
    QHash<int, int> lv;
    QHash<int, QVector<QVector<int>>> ranges;
    for (const QVariant& v : issues) {
        const QVariantMap m = v.toMap();
        const int line = m.value(QStringLiteral("line")).toInt();
        const int col = m.value(QStringLiteral("col"), -1).toInt(), len = m.value(QStringLiteral("len")).toInt();
        if (col >= 0 && len > 0) ranges[line] << QVector<int>{col, len, m.value(QStringLiteral("level")).toInt()};   // under its words only
        else lv[line] = qMax(lv.value(line, -1), m.value(QStringLiteral("level")).toInt());
    }
    if (lv != m_lineLevel || ranges != m_ranges) {
        m_lineLevel = lv;
        m_ranges = ranges;
        rehighlight();
    }
    emit issuesChanged();
}

void StoryHighlighter::highlightBlock(const QString& text)
{
    const QString s = text.trimmed();
    const int lead = int(text.indexOf(s.isEmpty() ? QString() : s.left(1)));
    if (s.isEmpty()) return;

    if (s.startsWith(QLatin1Char('#'))) {
        setFormat(0, int(text.size()), fmt(QColor(0x6b, 0x6b, 0x80), false, true));
    } else if (s.startsWith(QLatin1Char('@'))) {
        const int sp = int(text.indexOf(QLatin1Char(' '), lead));
        setFormat(lead, (sp < 0 ? int(text.size()) : sp) - lead, fmt(QColor(0xbd, 0x93, 0xf9), true));
        if (sp > 0) setFormat(sp, int(text.size()) - sp, fmt(QColor(0xe6, 0xd5, 0xff)));
    } else if (s.startsWith(QLatin1Char('-'))) {
        const int arrow = int(text.indexOf(QLatin1String("->"), lead));
        setFormat(lead, 1, fmt(QColor(0xff, 0x8a, 0x80), true));
        setFormat(lead + 1, (arrow < 0 ? int(text.size()) : arrow) - lead - 1, fmt(QColor(0xff, 0xdd, 0x7d)));
        // «Выбор 2.0»: the option's brackets - points green / red, the lock gold, a condition violet, «запомнит» cyan
        static const QRegularExpression br(QStringLiteral("\\[([^\\[\\]]*)\\]"));
        for (auto it = br.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            const QString inner = m.captured(1).trimmed();
            const QString w = firstWord(inner).toLower();
            QColor c;
            if (w.size() > 1 && w.at(0) == QLatin1Char('+') && w.at(1).isDigit()) c = QColor(0x50, 0xfa, 0x7b);
            else if (w.size() > 1 && (w.at(0) == QLatin1Char('-') || w.at(0) == QChar(0x2212)) && w.at(1).isDigit()) c = QColor(0xff, 0x6b, 0x6b);
            else if (w == QString::fromUtf8("нужно") || w == QString::fromUtf8("нужна") || w == QString::fromUtf8("нужен") || w == QString::fromUtf8("надо"))
                c = QColor(0xff, 0xc8, 0x57);
            else if (w == QString::fromUtf8("если")) c = QColor(0xbd, 0x93, 0xf9);
            else if (w == QString::fromUtf8("запомнит") || w == QString::fromUtf8("запомнят") || inner.toLower().endsWith(QString::fromUtf8(" запомнит")))
                c = QColor(0x8b, 0xe9, 0xfd);
            else if (w == QString::fromUtf8("флаг")) c = QColor(0xff, 0x79, 0xc6);
            else if (w == QString::fromUtf8("выход") || w == QString::fromUtf8("всегда") || w == QString::fromUtf8("хватит")) c = QColor(0x9f, 0xb3, 0xc8);
            else continue;                               // [имя], [пришёл/пришла]: the option's own words
            setFormat(int(m.capturedStart()), int(m.capturedLength()), fmt(c));
            const int wAt = int(text.indexOf(firstWord(inner), int(m.capturedStart())));
            if (wAt >= 0) setFormat(wAt, int(firstWord(inner).size()), fmt(c, true));
        }
        if (arrow >= 0) {
            setFormat(arrow, 2, fmt(QColor(0x9b, 0xd3, 0x5a), true));
            int from = arrow + 2;
            while (from < text.size() && text.at(from).isSpace()) ++from;
            const int pipe = int(text.indexOf(QLatin1Char('|'), from));
            QTextCharFormat t = fmt(QColor(0xff, 0x8a, 0x80), true);
            t.setFontUnderline(true);
            setFormat(from, (pipe < 0 ? int(text.size()) : pipe) - from, t);
            if (pipe >= 0) setFormat(pipe, int(text.size()) - pipe, fmt(QColor(0x80, 0xde, 0xea), false, true));
        } else if (const int pipe = int(text.lastIndexOf(QLatin1Char('|'))); pipe > lead) {
            setFormat(pipe, int(text.size()) - pipe, fmt(QColor(0x80, 0xde, 0xea), false, true));     // «| sl smile pioneer»
        }
    } else {
        const QString word = firstWord(s);
        const QString cmd = normalizeCommand(word);
        if (isCommandName(cmd)) {
            const bool label = cmd == QLatin1String("label");
            setFormat(lead, int(word.size()), fmt(categoryColor(categoryOf(cmd)), true));
            QTextCharFormat args = fmt(label ? QColor(0xff, 0xd1, 0xea) : QColor(0xd8, 0xd8, 0xe4), label);
            setFormat(lead + int(word.size()), int(text.size()) - lead - int(word.size()), args);
            // numbers in arguments
            for (int i = lead + int(word.size()); i < text.size(); ++i)
                if (text.at(i).isDigit() && (i == 0 || text.at(i - 1).isSpace())) {
                    int j = i;
                    while (j < text.size() && (text.at(j).isDigit() || text.at(j) == QLatin1Char('.') || text.at(j) == QLatin1Char(','))) ++j;
                    if (j == text.size() || text.at(j).isSpace()) setFormat(i, j - i, fmt(QColor(0xbd, 0x93, 0xf9)));
                    i = j;
                }
        } else if (s.startsWith(QLatin1Char(':'))) {
            setFormat(0, int(text.size()), fmt(QColor(0xff, 0xd1, 0xea), true));
        } else if (const QString kind = screenplayKind(s); !kind.isEmpty()) {
            // «пиши как сценарий»: heading gold, the name in its colour, (ремарка) cyan, prose like the narrator
            const QTextCharFormat remark = fmt(QColor(0x8b, 0xe9, 0xfd), false, true);
            if (kind == QLatin1String("heading")) {
                setFormat(0, int(text.size()), fmt(QColor(0xff, 0xdd, 0x7d), true));
            } else if (kind == QLatin1String("prose")) {
                setFormat(0, int(text.size()), fmt(QColor(0xe6, 0xe1, 0xcf)));
            } else if (s.at(0) == QChar(0x2014) || s.at(0) == QChar(0x2013) || s.startsWith(QLatin1Char('('))) {
                setFormat(0, int(text.size()), s.startsWith(QLatin1Char('(')) ? remark : fmt(QColor(0xf4, 0xf4, 0xf8)));
            } else {
                const int open = int(text.indexOf(QLatin1Char('('), lead));
                const int close = int(text.indexOf(QLatin1Char(')'), open));
                CompileState st;
                const QString who = text.mid(lead, open - lead).trimmed().section(QLatin1Char(' '), 0, 0);
                setFormat(lead, open - lead, fmt(speakerColor(speakerId(who, st, {})), true));
                setFormat(open, close - open + 1, remark);
                setFormat(close + 1, int(text.size()) - close - 1, fmt(QColor(0xf4, 0xf4, 0xf8)));
            }
        } else {
            const int colon = int(text.indexOf(QLatin1Char(':')));
            if (colon > 0 && !s.startsWith(QLatin1String("http"))) {
                const QString who = text.left(colon).trimmed();
                CompileState st;
                setFormat(0, colon + 1, fmt(speakerColor(speakerId(who, st, {})), true));
                setFormat(colon + 1, int(text.size()) - colon - 1, fmt(QColor(0xf4, 0xf4, 0xf8)));
            } else {
                setFormat(0, int(text.size()), fmt(QColor(0xc8, 0xc8, 0xd4), false, true));
            }
        }
    }

    const int level = m_lineLevel.value(currentBlock().blockNumber() + 1, -1);
    // Qt Quick's text draws no wave underline (only a plain one): a line of the issue's colour, and under the issue's
    // own words a tint as well - the writer sees at once what to fix
    auto mark = [&](int from, int to, int lvl, bool tint) {
        const QColor c = lvl >= 2 ? QColor(0xff, 0x55, 0x66) : QColor(0xff, 0xc8, 0x57);
        for (int i = qMax(0, from); i < qMin(int(text.size()), to); ++i) {
            QTextCharFormat f = format(i);
            f.setUnderlineStyle(QTextCharFormat::SingleUnderline);
            f.setUnderlineColor(c);
            if (tint) f.setBackground(QColor(c.red(), c.green(), c.blue(), 70));
            setFormat(i, 1, f);
        }
    };
    if (level >= 1) mark(0, int(text.size()), level, false);
    for (const QVector<int>& r : m_ranges.value(currentBlock().blockNumber() + 1))
        if (r.value(2) >= 1) mark(r.value(0), r.value(0) + r.value(1), r.value(2), true);
    // the find bar: every match on the line
    if (!m_find.isEmpty()) {
        const Qt::CaseSensitivity cs = m_findCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
        for (int i = int(text.indexOf(m_find, 0, cs)); i >= 0; i = int(text.indexOf(m_find, i + int(m_find.size()), cs))) {
            for (int k = i; k < i + m_find.size() && k < text.size(); ++k) {
                QTextCharFormat f = format(k);
                f.setBackground(QColor(0xff, 0xc8, 0x57, 95));
                setFormat(k, 1, f);
            }
        }
    }
}
