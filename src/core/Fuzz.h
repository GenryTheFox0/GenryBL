// GenryBL V1 - «Сломай мой мод»: the mod played through hundreds of times by the constructor's own cinema - the
// real route with its points, locks, conditions, the map, the phone - by random players and stubborn ones (always
// the first option, always the last, the option walked least). It finds what only a playthrough shows, never the
// text alone: an ending no choices lead to, a lock that never opens, an option «[если …]» never offers, a choice
// with every option shut, a loop the player can't leave, a mod that just stops. Every find carries its route: the
// cinema replays it to that very moment.
#pragma once
#include <QString>
#include <QVector>

namespace gb {

class EsAssets;

struct FuzzHit {
    // endings: finale | sceneend; problems: fell | missing | loop | stuck | endless; locked: lock | hidden;
    // unseen: cut (no way leads there) | blocked (ways lead there, no walk got in); never: finale
    QString kind;
    int line = 0;              // the story line (1-based)
    QString scene;             // the scene it is in
    QString detail;            // the ending's note, the option's words
    QString hint;              // a lock: what it needs
    int count = 0;             // walks that hit it
    QVector<int> route;        // the clicks / picks from the mod's start to that moment (-1 = a click / the timer)
};

struct FuzzReport {
    int runs = 0, clicksMin = 0, clicksMax = 0, clicksAvg = 0;
    int scenes = 0, scenesSeen = 0, choices = 0;
    int minutesMin = 0, minutesMax = 0;   // reading time of the walks: the words of the scenes walked, 160 a minute
    QVector<FuzzHit> endings, problems, locked, unseen, never;
};

// maxRuns walks at most, stops early after msBudget ms (24 walks at least); a walk longer than maxClicks is endless
FuzzReport breakMod(const QString& storyText, const EsAssets* es, int maxRuns = 600, int msBudget = 2500, int maxClicks = 2500);

} // namespace gb
