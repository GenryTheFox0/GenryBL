#include "Graph.h"
#include "Tr.h"
#include "EsAssets.h"
#include "Py.h"
#include "Screenplay.h"
#include "Text.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace gb {

namespace {

QString U(const char* s) { return QString::fromUtf8(s); }

QStringList fieldsOf(const QString& s)
{
    QStringList out;
    for (const QString& f : s.split(QLatin1Char('|'))) out << pyStrip(f);
    return out;
}

int wordsIn(const QString& s)
{
    int n = 0;
    bool in = false;
    for (const QChar c : s) {
        const bool w = c.isLetterOrNumber();
        if (w && !in) ++n;
        in = w;
    }
    return n;
}

} // namespace

StoryGraph storyGraph(const QString& storyText, const CompileOptions& opt)
{
    StoryGraph g;
    const QStringList raw = pySplitLines(stripBom(storyText));
    QVector<int> srcOf;
    QVector<ScreenplayNote> notes;
    const QStringList lines = opt.legacy ? raw : expandScreenplay(raw, &srcOf, &notes);
    if (opt.legacy) for (int i = 0; i < lines.size(); ++i) srcOf << i;
    auto keyOf = [&](const QString& name) {
        const QString k = slugOf(name, QStringLiteral("label"), opt);
        return k;
    };
    QHash<QString, int> byKey;
    struct Pending { int from; QString target, kind, text; int line; };
    QVector<Pending> pending;
    // the mod's own main menu is the first node when there is one: the player starts there
    int menuNode = -1;
    for (const QString& l : lines)
        if (normalizeCommand(firstWord(pyStrip(l))) == QLatin1String("modmenu")) {
            GraphNode m;
            m.kind = QStringLiteral("menu");
            m.ending = QStringLiteral("choice");
            g.nodes.push_back(m);
            menuNode = 0;
            break;
        }
    bool showsChapters = false;
    int cur = -1;
    bool inMenu = false;
    QVector<QPair<int, QString>> rawCode;     // «renpy …» lines: a scene they name is reached from there
    QString lastCmd;                          // the last meaningful command of the scene being read
    int lastIdx = -1;
    auto closeScene = [&](int endLine) {
        if (cur < 0) return;
        g.nodes[cur].lastLine = endLine;
    };
    for (int i = 0; i < lines.size(); ++i) {
        const int ln = srcOf.value(i) + 1;
        const QString s = pyStrip(lines[i]);
        if (s.isEmpty() || s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char('@'))) continue;
        const QString first = firstWord(s);
        const QString cmd = normalizeCommand(first);
        const QString rest = pyStrip(s.mid(first.size()));

        if (inMenu) {
            if (cmd == QLatin1String("endmodmenu") || cmd == QLatin1String("endchoice")) { inMenu = false; continue; }
            const QString lw = first.toLower();
            if (lw == U("кнопка") || lw == QLatin1String("button")) {
                QString caption = rest, target;
                if (rest.contains(QLatin1String("->"))) {
                    caption = pyStrip(rest.section(QStringLiteral("->"), 0, 0));
                    target = pyStrip(rest.section(QStringLiteral("->"), 1));
                }
                const QString kind = menuButtonKind(target.isEmpty() ? caption : target);
                if (kind == U("главы")) showsChapters = true;
                static const QSet<QString> special{U("галерея"), U("достижения"), U("шкалы"), U("главы"), U("настройки"), U("загрузить"), U("выход"), U("имя"), U("статистика"), U("стример")};
                if (special.contains(kind)) continue;
                pending.push_back({menuNode, target.isEmpty() ? QStringLiteral("start") : target, QStringLiteral("button"), caption, ln});
            }
            continue;
        }
        if (cmd == QLatin1String("modmenu")) { inMenu = true; continue; }
        if (s.startsWith(QLatin1Char(':')) || cmd == QLatin1String("label")) {
            const QString name = s.startsWith(QLatin1Char(':')) ? pyStrip(s.mid(1)) : rest;
            closeScene(ln - 1);
            // the scene before, left without a way out: «<name>_next» is where the game goes on by itself
            if (cur >= 0 && keyOf(name) == keyOf(g.nodes[cur].name) + QStringLiteral("_next"))
                pending.push_back({cur, name, QStringLiteral("next"), QString(), ln});
            GraphNode n;
            n.name = name;
            n.line = ln;
            n.kind = QStringLiteral("scene");
            const QString k = keyOf(name);
            if (!byKey.contains(k)) byKey.insert(k, int(g.nodes.size()));
            cur = int(g.nodes.size());
            g.nodes.push_back(n);
            lastCmd.clear();
            lastIdx = -1;
            continue;
        }
        if (cur < 0) continue;                   // before the first scene: the mod's settings
        GraphNode& node = g.nodes[cur];
        ++node.lines;
        lastCmd = cmd;
        lastIdx = i;
        const auto to = [&](const QString& target, const QString& kind, const QString& text = QString()) {
            if (!pyStrip(target).isEmpty()) pending.push_back({cur, pyStrip(target), kind, text, ln});
        };
        if (isChoiceItemLine(s)) {
            const ChoiceItemSpec it = parseChoiceItem(s);
            to(it.target, QStringLiteral("choice"), it.caption);
            node.words += wordsIn(it.caption);
            continue;
        }
        if (s.contains(QLatin1String("->")) && isTimeoutWord(s.section(QStringLiteral("->"), 0, 0))) {
            to(s.section(QStringLiteral("->"), 1), QStringLiteral("timeout"), gbTr("время вышло"));
            continue;
        }
        if (cmd == QLatin1String("jump")) { to(rest, QStringLiteral("jump")); continue; }
        if (cmd == QLatin1String("callscene")) { to(rest, QStringLiteral("call")); continue; }
        if (cmd == QLatin1String("ifjump") || cmd == QLatin1String("ifpersistent") || cmd == QLatin1String("ifitem")) {
            to(rest.section(QStringLiteral("->"), 1), QStringLiteral("if"), pyStrip(rest.section(QStringLiteral("->"), 0, 0)));
            continue;
        }
        if (cmd == QLatin1String("codelock")) {
            const CodeLockSpec k = parseCodeLock(rest);
            to(k.okTarget, QStringLiteral("code"), gbTr("верный код"));
            to(k.badTarget, QStringLiteral("code"), gbTr("неверный код"));
            continue;
        }
        if (cmd == QLatin1String("phonecall")) {
            const QStringList f = fieldsOf(rest);
            for (const QString& x : f.mid(1))
                if (x.contains(QLatin1String("->"))) to(x.section(QStringLiteral("->"), 1), QStringLiteral("phone"), pyStrip(x.section(QStringLiteral("->"), 0, 0)));
            continue;
        }
        if (cmd == QLatin1String("map")) {
            if (!opt.legacy) {
                const MapSpec m = parseMapSpec(rest);
                for (const MapEntry& p : m.places) {
                    const EsMapZone* z = esMapZone(p.zone);
                    to(p.target, QStringLiteral("map"), z ? z->title : p.raw);
                }
                to(m.done, QStringLiteral("map"), gbTr("всё обошёл"));
            } else {
                for (const QString& e : rest.split(QLatin1Char(','))) {
                    const QString entry = e.section(QLatin1Char('@'), 0, 0);
                    to(entry.contains(QLatin1Char(':')) ? entry.section(QLatin1Char(':'), 1) : entry.section(QStringLiteral("->"), 1),
                       QStringLiteral("map"), pyStrip(entry.section(QLatin1Char(':'), 0, 0)));
                }
            }
            continue;
        }
        if (cmd == QLatin1String("screenmenu")) {
            for (const QString& it : fieldsOf(rest).mid(1))
                if (it.contains(QLatin1String("->"))) to(it.section(QStringLiteral("->"), 1), QStringLiteral("menu"), pyStrip(it.section(QStringLiteral("->"), 0, 0)));
            continue;
        }
        if (cmd == QLatin1String("terminal")) {
            for (const QString& it : fieldsOf(rest).mid(1))
                if (it.contains(QLatin1String("->"))) {
                    const QString t = pyStrip(it.section(QStringLiteral("->"), 1)), tl = t.toLower();
                    if (tl != U("выход") && tl != QLatin1String("exit") && tl != U("дальше"))
                        to(t, QStringLiteral("menu"), pyStrip(it.section(QStringLiteral("->"), 0, 0).section(QLatin1Char(':'), 0, 0)));
                }
            continue;
        }
        if (cmd == QLatin1String("rps")) {
            for (const QString& it : fieldsOf(rest).mid(1))
                if (it.contains(QLatin1String("->"))) to(it.section(QStringLiteral("->"), 1), QStringLiteral("code"), pyStrip(it.section(QStringLiteral("->"), 0, 0)));
            continue;
        }
        if (cmd == QLatin1String("bestmeter")) {
            for (const QString& part : rest.split(QLatin1Char('|')))
                if (part.contains(QLatin1String("->"))) to(part.section(QStringLiteral("->"), 1), QStringLiteral("best"), pyStrip(part.section(QStringLiteral("->"), 0, 0)));
            continue;
        }
        if (cmd == QLatin1String("newchapter")) { node.chapter = true; continue; }
        if (cmd == QLatin1String("renpy") || cmd == QLatin1String("python")) { rawCode.push_back({cur, rest}); continue; }
        if (cmd == QLatin1String("chapters")) { showsChapters = true; continue; }
        if (node.bg.isEmpty() && (cmd == QLatin1String("bg") || cmd == QLatin1String("showbg") || cmd == QLatin1String("cg"))) {
            QStringList w = pySplit(rest);
            if (!w.isEmpty() && isEffect(w.last())) w.removeLast();
            if (!w.isEmpty()) node.bg = (cmd == QLatin1String("cg") ? QStringLiteral("cg ") : QStringLiteral("bg ")) + w.join(QLatin1Char(' '));
            continue;
        }
        // the words the player reads: a line of dialogue, «текст …», notes
        if (!isCommandName(cmd) && s.contains(QLatin1Char(':'))) { node.words += wordsIn(s.section(QLatin1Char(':'), 1)); continue; }
        if (cmd == QLatin1String("narration") || cmd == QLatin1String("note") || cmd == QLatin1String("monologue") || cmd == QLatin1String("say"))
            node.words += wordsIn(rest);
    }
    closeScene(srcOf.isEmpty() ? 0 : srcOf.last() + 1);
    Q_UNUSED(lastCmd);
    Q_UNUSED(lastIdx);

    // raw Ren'Py lines that jump / call a scene by its label or its name
    for (const auto& rc : rawCode)
        for (int n = 0; n < g.nodes.size(); ++n) {
            const GraphNode& t = g.nodes[n];
            if (t.kind != QLatin1String("scene")) continue;
            const QString lab = keyOf(t.name);
            static const QRegularExpression word(QStringLiteral("[A-Za-z0-9_]+"));
            for (auto m = word.globalMatch(rc.second); m.hasNext();) {
                const QString w = m.next().captured(0);
                if (w == t.name || w == lab || w.endsWith(QStringLiteral("__") + lab)) { pending.push_back({rc.first, t.name, QStringLiteral("jump"), QStringLiteral("renpy"), t.line}); break; }
            }
        }
    // the ways -> edges; a way to a scene that is not there gets a ghost node of its own
    for (const Pending& p : pending) {
        QString k = keyOf(p.target);
        int to = byKey.value(k, -1);
        if (to < 0) {
            GraphNode ghost;
            ghost.name = p.target;
            ghost.kind = QStringLiteral("missing");
            to = int(g.nodes.size());
            byKey.insert(k, to);
            g.nodes.push_back(ghost);
        }
        g.edges.push_back({p.from, to, p.kind, p.text, p.line});
    }

    // how each scene ends: its last lines (the same reading the lint does, in short)
    for (int n = 0; n < g.nodes.size(); ++n) {
        GraphNode& node = g.nodes[n];
        if (node.kind != QLatin1String("scene")) continue;
        int last = -1;
        for (int i = node.lastLine - 1; i >= node.line; --i) {
            const QString s = pyStrip(raw.value(i));
            if (!s.isEmpty() && !s.startsWith(QLatin1Char('#'))) { last = i; break; }
        }
        const QString s = last >= 0 ? pyStrip(raw[last]) : QString();
        const QString c = normalizeCommand(firstWord(s));
        bool out = false;
        for (const GraphEdge& e : g.edges) if (e.from == n && e.kind == QLatin1String("next")) out = true;
        if (out) node.ending = QStringLiteral("next");
        else if (c == QLatin1String("jump")) node.ending = QStringLiteral("jump");
        else if (c == QLatin1String("endgame")) node.ending = QStringLiteral("end");
        else if (c == QLatin1String("return")) node.ending = QStringLiteral("return");
        else if (c == QLatin1String("map") || c == QLatin1String("screenmenu") || c == QLatin1String("bestmeter") || c == QLatin1String("codelock") ||
                 c == QLatin1String("endchoice") || c == QLatin1String("phonecall") || isChoiceItemLine(s))
            node.ending = QStringLiteral("choice");
        else node.ending = QStringLiteral("stops");
    }
    // a scene someone «вызов»s returns there: it does not end the mod
    for (const GraphEdge& e : g.edges)
        if (e.kind == QLatin1String("call") && g.nodes[e.to].ending == QLatin1String("stops")) g.nodes[e.to].ending = QStringLiteral("return");

    // where the player starts: the menu, else «start», else the first scene
    QVector<int> roots;
    if (menuNode >= 0) roots << menuNode;
    else if (byKey.contains(QStringLiteral("start"))) roots << byKey.value(QStringLiteral("start"));
    else for (int n = 0; n < g.nodes.size(); ++n) if (g.nodes[n].kind == QLatin1String("scene")) { roots << n; break; }
    if (!roots.isEmpty()) g.nodes[roots.first()].start = true;
    // chapters open from the chapters screen: their scenes are ways in of their own
    if (showsChapters)
        for (int n = 0; n < g.nodes.size(); ++n)
            if (g.nodes[n].chapter) {
                if (menuNode >= 0) g.edges.push_back({menuNode, n, QStringLiteral("chapter"), gbTr("глава"), g.nodes[n].line});
                else roots << n;
            }
    QVector<int> depth(g.nodes.size(), -1);
    QVector<int> queue;
    for (int r : roots) if (depth[r] < 0) { depth[r] = 0; queue << r; }
    for (int q = 0; q < queue.size(); ++q) {
        const int at = queue[q];
        for (const GraphEdge& e : g.edges)
            if (e.from == at && depth[e.to] < 0) { depth[e.to] = depth[at] + 1; queue << e.to; }
    }
    int maxDepth = 0;
    for (int n = 0; n < g.nodes.size(); ++n) {
        g.nodes[n].reachable = depth[n] >= 0;
        maxDepth = qMax(maxDepth, depth[n]);
    }
    // the layout: a column per step from the start; the lost scenes in a column of their own at the end
    QVector<int> rowsIn(maxDepth + 2, 0);
    QVector<int> order(g.nodes.size());
    for (int n = 0; n < order.size(); ++n) order[n] = n;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int la = g.nodes[a].line ? g.nodes[a].line : 1 << 30, lb = g.nodes[b].line ? g.nodes[b].line : 1 << 30;
        return la < lb;
    });
    for (int n : order) {
        GraphNode& node = g.nodes[n];
        node.col = depth[n] >= 0 ? depth[n] : maxDepth + 1;
        node.row = rowsIn[node.col]++;
    }
    g.cols = 0;
    g.rows = 0;
    for (int c = 0; c < rowsIn.size(); ++c)
        if (rowsIn[c]) { g.cols = c + 1; g.rows = qMax(g.rows, rowsIn[c]); }
    return g;
}

QVector<QPair<int, QString>> unreachableScenes(const QString& storyText, const CompileOptions& opt)
{
    QVector<QPair<int, QString>> out;
    const StoryGraph g = storyGraph(storyText, opt);
    for (const GraphNode& n : g.nodes)
        if (n.kind == QLatin1String("scene") && !n.reachable) out.push_back({n.line, n.name});
    return out;
}

} // namespace gb
