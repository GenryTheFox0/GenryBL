#include "Fuzz.h"
#include "Cinema.h"
#include "Compiler.h"
#include "Graph.h"
#include "Py.h"
#include "Text.h"

#include <QElapsedTimer>
#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <climits>

namespace gb {

namespace {

struct ChoiceSeen {
    QStringList every;                 // every option of the block («[если …]» ones too)
    QHash<int, QString> hint;          // ord -> the lock's «нужно …» as last seen
    QSet<int> shown, open;             // ords ever on screen / ever open
    QVector<int> route;                // the first way to it
    quint32 seed = 0;
};

// a hit met again: counted, and the shortest way to it kept (the nicest to watch)
void note(QHash<QString, FuzzHit>& table, const FuzzHit& h)
{
    const QString key = h.kind + QLatin1Char(':') + QString::number(h.line);
    auto it = table.find(key);
    if (it == table.end()) {
        FuzzHit first = h;
        first.count = 1;
        table.insert(key, first);
        return;
    }
    ++it->count;
    if (h.route.size() < it->route.size()) {
        it->route = h.route;
        it->seed = h.seed;
    }
}

QVector<FuzzHit> sorted(const QHash<QString, FuzzHit>& table, bool byCount)
{
    QVector<FuzzHit> v(table.cbegin(), table.cend());
    std::sort(v.begin(), v.end(), [byCount](const FuzzHit& a, const FuzzHit& b) {
        if (byCount && a.count != b.count) return a.count > b.count;
        return a.line < b.line;
    });
    return v;
}

} // namespace

FuzzReport breakMod(const QString& storyText, const EsAssets* es, int maxRuns, int msBudget, int maxClicks)
{
    FuzzReport r;
    Cinema c;
    c.load(storyText, es);
    c.setBlind(true);

    // the scenes as the story map sees them: where each is, how much it has to read, whether any way leads there
    const StoryGraph g = storyGraph(storyText);
    QVector<const GraphNode*> scenes;
    QHash<QString, int> wordsOf;
    for (const GraphNode& n : g.nodes) {
        if (n.kind != QLatin1String("scene")) continue;
        scenes << &n;
        wordsOf.insert(c.labelOf(n.name), n.words);
    }
    r.scenes = int(scenes.size());
    auto sceneOf = [&](int line) -> QString {
        for (const GraphNode* n : scenes)
            if (line >= n->line && line <= qMax(n->line, n->lastLine)) return n->name;
        return {};
    };

    QRandomGenerator rng(0x5EEDu);               // the same story, the same report
    QHash<qint64, int> picks;                    // (choice line, ord) -> times picked
    QHash<int, ChoiceSeen> choices;
    QHash<QString, FuzzHit> ends, problems;
    QSet<QString> seen;
    QSet<int> finalesHit;
    qint64 clicksSum = 0;
    int clicksMin = INT_MAX, clicksMax = 0, wordsMin = INT_MAX, wordsMax = 0;
    // a long mod is long, not endless: 3 clicks a line before a walk counts as a circle
    const int limit = qMax(maxClicks, int(pySplitLines(storyText).size()) * 3);
    QElapsedTimer clock;
    clock.start();

    for (int run = 0; run < maxRuns && (run < 24 || clock.elapsed() < msBudget); ++run) {
        if (run >= 24 && choices.isEmpty()) break;       // nothing to choose: every walk is the same one
        QVector<int> route;
        QHash<int, int> met;                     // choices met this walk: a hub met again is not a stubborn one's business
        const quint32 seed = quint32(run + 1);
        c.setSeed(seed);
        CinemaStop st = c.start(1);
        for (;;) {
            if (st.kind == CinemaStop::End) {
                FuzzHit h;
                h.line = st.line;
                h.scene = sceneOf(st.line);
                h.detail = st.note;
                h.route = route.mid(0, qMax(0, int(route.size()) - 1));   // the moment right before the end
                h.seed = seed;
                switch (st.endKind) {
                case CinemaStop::Finale: h.kind = QStringLiteral("finale"); break;
                case CinemaStop::SceneEnd: h.kind = QStringLiteral("sceneend"); break;
                case CinemaStop::Missing: h.kind = QStringLiteral("missing"); break;
                case CinemaStop::Loop: h.kind = QStringLiteral("loop"); break;
                default: h.kind = QStringLiteral("fell"); break;
                }
                if (st.endKind == CinemaStop::Finale || st.endKind == CinemaStop::SceneEnd) {
                    note(ends, h);
                    if (st.endKind == CinemaStop::Finale) finalesHit.insert(st.line);
                } else {
                    if (st.endKind == CinemaStop::Missing) {   // the scene that is not there, by name (the window words it)
                        static const QRegularExpression named(QStringLiteral("«([^»]*)»"));
                        h.detail = named.match(st.note).captured(1);
                    }
                    note(problems, h);
                }
                break;
            }
            if (route.size() >= limit) {         // still going: a circle the player can't leave
                FuzzHit h;
                h.kind = QStringLiteral("endless");
                h.line = st.line;
                h.scene = sceneOf(st.line);
                h.route = route;
                h.seed = seed;
                note(problems, h);
                break;
            }
            int arg = -1;
            if (st.kind == CinemaStop::Choice) {
                ChoiceSeen& cs = choices[st.line];
                if (cs.every.isEmpty()) {
                    cs.every = st.everyOption.isEmpty() ? st.options : st.everyOption;
                    cs.route = route;
                    cs.seed = seed;
                }
                QVector<int> ok;                    // the options a player can press, by their place on screen
                for (int i = 0; i < st.options.size(); ++i) {
                    const int ord = i < st.optionOrd.size() ? st.optionOrd[i] : i;
                    cs.shown.insert(ord);
                    const QString why = st.optionHints.value(i);
                    if (why.isEmpty()) {
                        cs.open.insert(ord);
                        ok << i;
                    } else {
                        cs.hint.insert(ord, why);
                    }
                }
                auto ordOf = [&](int i) { return qint64(st.line) << 16 | (i < st.optionOrd.size() ? st.optionOrd[i] : i); };
                if (ok.isEmpty()) {
                    if (st.seconds <= 0) {           // every option shut and no timer: the player is stuck here
                        FuzzHit h;
                        h.kind = QStringLiteral("stuck");
                        h.line = st.line;
                        h.scene = sceneOf(st.line);
                        h.route = route;
                        h.seed = seed;
                        note(problems, h);
                        break;
                    }
                } else if (st.seconds > 0 && rng.bounded(100) < 12) {
                    arg = -1;                        // a timed choice: sometimes the timer runs out
                } else if (run == 0 && met.value(st.line) < 2) {
                    arg = ok.first();                // the stubborn ones: always the first, always the last
                } else if (run == 1 && met.value(st.line) < 2) {
                    arg = ok.last();
                } else if (rng.bounded(100) < 60) {  // the option walked least - every door gets tried
                    arg = ok[int(rng.bounded(int(ok.size())))];
                    int least = picks.value(ordOf(arg));
                    for (int i : ok)
                        if (picks.value(ordOf(i)) < least) { arg = i; least = picks.value(ordOf(i)); }
                } else {
                    arg = ok[int(rng.bounded(int(ok.size())))];
                }
                if (arg >= 0) ++picks[ordOf(arg)];
                ++met[st.line];
            }
            route << arg;
            st = c.next(arg);
        }
        ++r.runs;
        const int clicks = int(route.size());
        clicksSum += clicks;
        clicksMin = qMin(clicksMin, clicks);
        clicksMax = qMax(clicksMax, clicks);
        int words = 0;
        for (const QString& s : c.visited()) words += wordsOf.value(s);
        wordsMin = qMin(wordsMin, words);
        wordsMax = qMax(wordsMax, words);
        seen.unite(c.visited());
    }
    if (!r.runs) return r;
    r.clicksMin = clicksMin;
    r.clicksMax = clicksMax;
    r.clicksAvg = int(clicksSum / r.runs);
    r.minutesMin = qMax(1, (wordsMin + 159) / 160);
    r.minutesMax = qMax(1, (wordsMax + 159) / 160);
    r.choices = int(choices.size());
    r.endings = sorted(ends, true);
    r.problems = sorted(problems, true);

    // options that never opened / never showed up: their content is dead for every player
    QList<int> lines = choices.keys();
    std::sort(lines.begin(), lines.end());
    for (int line : lines) {
        const ChoiceSeen& cs = choices[line];
        for (int ord = 0; ord < cs.every.size(); ++ord) {
            if (cs.open.contains(ord)) continue;
            FuzzHit h;
            h.kind = cs.shown.contains(ord) ? QStringLiteral("lock") : QStringLiteral("hidden");
            h.line = line;
            h.scene = sceneOf(line);
            h.detail = cs.every[ord];
            h.hint = cs.hint.value(ord);
            h.route = cs.route;
            h.seed = cs.seed;
            r.locked << h;
        }
    }
    // scenes no walk went into
    for (const GraphNode* n : scenes) {
        if (seen.contains(c.labelOf(n->name))) {
            ++r.scenesSeen;
            continue;
        }
        FuzzHit h;
        h.kind = n->reachable ? QStringLiteral("blocked") : QStringLiteral("cut");
        h.line = n->line;
        h.scene = n->name;
        r.unseen << h;
    }
    // «конецигры» in a scene the walks went into, never reached (behind an «если» nobody passes)
    const QStringList src = pySplitLines(stripBom(storyText));
    for (int i = 0; i < src.size(); ++i) {
        const QString s = pyStrip(src[i]);
        if (normalizeCommand(firstWord(s)) != QLatin1String("endgame") || finalesHit.contains(i + 1)) continue;
        const QString scene = sceneOf(i + 1);
        if (scene.isEmpty() || !seen.contains(c.labelOf(scene))) continue;
        FuzzHit h;
        h.kind = QStringLiteral("finale");
        h.line = i + 1;
        h.scene = scene;
        r.never << h;
    }
    return r;
}

} // namespace gb
