// GenryBL V1 - «Карта сюжета»: the story as a graph of its scenes - where every «переход», option, button, call,
// place on the map and code lock leads. The old GenryModEngine had it (its best idea); here it also knows the
// V1/V2 ways out (Выбор 2.0 options, timers, the phone call, the mod's own menu, chapters) and says what the game
// would do: which scenes nobody can reach, where a way leads to a scene that is not there, where the mod ends.
#pragma once
#include "Compiler.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace gb {

struct GraphNode {
    QString name;              // as written after «:» ("" for the mod's menu)
    int line = 0;              // 1-based line of «: name» (0 = not in the story)
    int lastLine = 0;          // its last line
    int lines = 0;             // lines with something in them
    int words = 0;             // words the player reads
    QString bg;                // the first background ("bg ext_square_day" / "cg …"), for the card
    QString kind;              // scene | menu | missing
    QString ending;            // how it ends: jump | end (конецигры) | choice | stops (the mod ends there by accident) | next
    bool start = false, reachable = false, chapter = false;
    int col = 0, row = 0;      // the layout: columns = steps from the start, rows = order in the story
};

struct GraphEdge {
    int from = 0, to = 0;      // node indexes
    QString kind;              // jump | call | choice | if | timeout | code | phone | map | menu | best | button | chapter | next
    QString text;              // the option's words, the condition, the place…
    int line = 0;
};

struct StoryGraph {
    QVector<GraphNode> nodes;
    QVector<GraphEdge> edges;
    int cols = 0, rows = 0;
};

StoryGraph storyGraph(const QString& storyText, const CompileOptions& opt = {});
// scenes no way leads to (for the lint): {line, name}
QVector<QPair<int, QString>> unreachableScenes(const QString& storyText, const CompileOptions& opt = {});

} // namespace gb
