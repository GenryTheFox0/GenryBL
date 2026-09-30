#include "Crash.h"
#include "Py.h"
#include "Text.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace gb {

namespace {

QString readText(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QString t = QString::fromUtf8(f.readAll());
    if (t.startsWith(QChar(0xFEFF))) t.remove(0, 1);
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return t;
}

// words both lines have: letters / digits / _, two and more, lower case; Ren'Py's own words say nothing about the line
QSet<QString> wordsOf(const QString& s)
{
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}_]{2,}"), QRegularExpression::UseUnicodePropertiesOption);
    static const QSet<QString> noise{QStringLiteral("show"), QStringLiteral("hide"), QStringLiteral("scene"), QStringLiteral("at"), QStringLiteral("with"),
                                     QStringLiteral("play"), QStringLiteral("stop"), QStringLiteral("music"), QStringLiteral("sound"), QStringLiteral("jump"),
                                     QStringLiteral("call"), QStringLiteral("window"), QStringLiteral("auto"), QStringLiteral("renpy"), QStringLiteral("pause"),
                                     QStringLiteral("hard"), QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("expression"), QStringLiteral("as"),
                                     QStringLiteral("zorder"), QStringLiteral("fadein"), QStringLiteral("fadeout"), QStringLiteral("loop"), QStringLiteral("channel"),
                                     QStringLiteral("genry_sprite_time_tint"), QStringLiteral("dspr"), QStringLiteral("None"), QStringLiteral("none")};
    QSet<QString> out;
    for (auto it = word.globalMatch(s); it.hasNext();) {
        const QString w = it.next().captured(0).toLower();
        if (!noise.contains(w)) out.insert(w);
    }
    return out;
}

void classify(CrashReport& r)
{
    struct Pattern { const char* re; const char* what; };
    static const Pattern patterns[] = {
        {"Image '([^']+)' not found", "image"},
        {"[Cc]ould not find label '([^']+)'", "label"},
        {"name '([^']+)' is not defined", "name"},
        {"[Cc]ouldn't find file '([^']+)'", "file"},
        {"[Cc]ould not (?:load|open|find) (?:file |image )?'([^']+)'", "file"},
        {"No such file or directory: '([^']+)'", "file"},
    };
    for (const Pattern& p : patterns) {
        const QRegularExpression re(QString::fromUtf8(p.re));
        const auto m = re.match(r.error);
        if (m.hasMatch()) {
            r.what = QString::fromUtf8(p.what);
            r.arg = m.captured(1);
            return;
        }
    }
    r.what = r.kind == QLatin1String("parse") ? QStringLiteral("syntax") : QStringLiteral("other");
}

void whoseFile(CrashReport& r, const QString& modId)
{
    const QString f = QDir::fromNativeSeparators(r.file);
    static const QRegularExpression ws(QStringLiteral("workshop/content/331470/(\\d+)/"));
    static const QRegularExpression mod(QStringLiteral("(?:^|/)mods/([^/]+)/"));
    if (const auto m = ws.match(f); m.hasMatch()) r.workshopId = m.captured(1);
    if (const auto m = mod.match(f); m.hasMatch()) r.mod = m.captured(1);
    else if (!r.workshopId.isEmpty()) r.mod = r.workshopId;
    r.ours = !modId.isEmpty() && r.mod == modId && r.workshopId.isEmpty();
}

// the scene (by the label above the Ren'Py line) and the story line in it that shares the most words with it
void pointAtStory(CrashReport& r, const QString& esRoot, const QString& modId, const QString& storyText, const CompileOptions& opt)
{
    const QStringList rpy = readText(QDir::cleanPath(QDir(esRoot).filePath(r.file))).split(QLatin1Char('\n'));
    QString label;
    static const QRegularExpression labelRe(QStringLiteral("^label\\s+([A-Za-z0-9_\\.]+)"));
    for (int i = qMin(r.rpyLine, int(rpy.size())) - 1; i >= 0 && label.isEmpty(); --i)
        if (const auto m = labelRe.match(rpy[i]); m.hasMatch()) label = m.captured(1);
    if (label.isEmpty()) return;                      // the mod's init / screens: no story line of its own

    const QStringList src = pySplitLines(stripBom(storyText));
    int from = -1, to = int(src.size()), bestLen = -1;
    for (int i = 0; i < src.size(); ++i) {
        const QString s = pyStrip(src[i]);
        QString name;
        if (s.startsWith(QLatin1Char(':'))) name = pyStrip(s.mid(1));
        else if (normalizeCommand(firstWord(s)) == QLatin1String("label")) name = pyStrip(s.mid(firstWord(s).size()));
        else continue;
        const QString lab = sceneLabel(modId, name, opt);
        // the scene's own label, or one of its parts (<label>_next, a choice's branch label…): the longest that fits
        if ((label == lab || label.startsWith(lab + QLatin1Char('_'))) && lab.size() > bestLen) {
            bestLen = int(lab.size());
            from = i;
            r.scene = name;
        }
    }
    if (from < 0) return;
    for (int i = from + 1; i < src.size(); ++i) {
        const QString s = pyStrip(src[i]);
        if (s.startsWith(QLatin1Char(':')) || normalizeCommand(firstWord(s)) == QLatin1String("label")) { to = i; break; }
    }
    const QSet<QString> code = wordsOf(r.rpyCode), named = wordsOf(r.arg);
    int best = from, bestScore = 0;
    for (int i = from + 1; i < to; ++i) {
        const QSet<QString> w = wordsOf(src[i]);
        int score = 0;
        for (const QString& x : w) if (code.contains(x)) score += qMin(int(x.size()), 8);
        if (!named.isEmpty()) {                          // the error names an image / a file: the line that says all of it
            bool all = true;
            for (const QString& x : named) if (!w.contains(x)) { all = false; break; }
            if (all) score += 40;
        }
        if (score > bestScore) { bestScore = score; best = i; }
    }
    r.storyLine = best + 1;
}

} // namespace

CrashReport readCrash(const QString& esRoot, qint64 sinceMs, const QString& modId, const QString& storyText, const CompileOptions& opt)
{
    CrashReport r;
    if (esRoot.isEmpty()) return r;
    const QFileInfo tb(esRoot + QStringLiteral("/traceback.txt")), er(esRoot + QStringLiteral("/errors.txt"));
    const qint64 tbAt = tb.exists() ? tb.lastModified().toMSecsSinceEpoch() : 0, erAt = er.exists() ? er.lastModified().toMSecsSinceEpoch() : 0;
    const bool tbNew = tbAt > sinceMs, erNew = erAt > sinceMs;
    if (!tbNew && !erNew) return r;
    const bool parse = erNew && (!tbNew || erAt >= tbAt);
    const QString text = readText(parse ? er.filePath() : tb.filePath());
    if (text.trimmed().isEmpty()) return r;
    r.found = true;
    r.kind = parse ? QStringLiteral("parse") : QStringLiteral("runtime");
    const QString head = text.section(QStringLiteral("-- Full Traceback"), 0, 0);
    r.raw = head.left(6000).trimmed();
    const QStringList lines = head.split(QLatin1Char('\n'));
    if (parse) {
        static const QRegularExpression fileRe(QStringLiteral("^File \"([^\"]+)\", line (\\d+): ?(.*)$"));
        for (int i = 0; i < lines.size(); ++i) {
            const auto m = fileRe.match(lines[i]);
            if (!m.hasMatch()) continue;
            r.file = m.captured(1);
            r.rpyLine = m.captured(2).toInt();
            r.error = m.captured(3).trimmed();
            if (i + 1 < lines.size()) r.rpyCode = lines[i + 1].trimmed();
            break;
        }
    } else {
        static const QRegularExpression fileRe(QStringLiteral("^\\s*File \"([^\"]+)\", line (\\d+)"));
        int at = -1;
        for (int i = 0; i < lines.size(); ++i) {
            const auto m = fileRe.match(lines[i]);
            if (!m.hasMatch()) continue;
            const QString f = m.captured(1);
            // the deepest frame in a script (.rpy), not Ren'Py's own code
            if (f.endsWith(QLatin1String(".rpy")) || at < 0) {
                at = i;
                r.file = f;
                r.rpyLine = m.captured(2).toInt();
            }
        }
        if (at >= 0 && at + 1 < lines.size() && lines[at + 1].startsWith(QLatin1Char(' '))) r.rpyCode = lines[at + 1].trimmed();
        for (int i = qMax(0, at + 1); i < lines.size(); ++i) {
            const QString l = lines[i];
            if (l.trimmed().isEmpty() || l.startsWith(QLatin1Char(' ')) || l.startsWith(QLatin1Char('\t'))) continue;
            if (l.startsWith(QLatin1String("While ")) || l.startsWith(QLatin1String("I'm sorry"))) continue;
            r.error = l.trimmed();
            break;
        }
    }
    classify(r);
    whoseFile(r, modId);
    if (r.ours && r.rpyLine > 0 && !storyText.isEmpty()) pointAtStory(r, esRoot, modId, storyText, opt);
    return r;
}

} // namespace gb
