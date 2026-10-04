// GenryBL V1 - «кино-режим»: the mod plays inside the constructor the way the game runs it.
// The player walks the real route - scenes, «переход», «вызвать», choices, «если» on the mod's
// points, phone calls, the camp map, the mod's own main menu - and stops where the game waits:
// a line of dialogue, a choice, a note, a title card, a pause. Every stop carries the frame
// (sceneAt over the lines really walked, so the picture is the one of THIS route), the music
// and ambience that play, the sounds / voice of the moment and the popups (notify, «запомнит»,
// points, items, achievements) that showed up on the way.
#pragma once
#include "Scene.h"

#include <QHash>
#include <QRandomGenerator>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gb {

class EsAssets;

struct CinemaStop {
    enum Kind { Say, Choice, Note, Card, Timed, Video, End };
    // End: how the mod ended - «конецигры», «конецсцены», fell off (a scene over with no way on, the story over),
    // a way into a scene that is not there, a loop with no line to stop at
    enum EndKind { Finale, SceneEnd, FellOff, Missing, Loop };
    Kind kind = End;
    EndKind endKind = FellOff;
    int line = 0;                 // the story line (1-based) the player is at
    SceneState scene;             // what the player sees
    QStringList options;          // Choice: captions
    QVector<bool> optionOk;       // false: that option leads nowhere in this story
    QStringList optionHints;      // "" = open; else the option is locked («[нужно …]») and this says why
    QVector<int> optionOrd;       // Choice («выбор»): each option's place in its block
    QStringList everyOption;      // Choice («выбор»): every option of the block, the ones «[если …]» hides as well
    double seconds = 0;           // Card / Timed: auto-advance; Choice: the timer of a timed choice (0 = none)
    QString music, ambience;      // what plays now: "es:<id>" | "file:audio/x.ogg" | "" (silence)
    QStringList sounds;           // sfx / voice starting with this stop: "es:<id>" | "file:audio/x.ogg"
    QStringList popups;           // "title|text" that appeared since the last stop
    QString video;                // Video: "es:video/x.ogv" | "file:video/x.webm"
    QString moment;               // shake / flash / pixels / blink since the last stop
    QString note;                 // End: why it ended; else a hint (a missing scene…)
    // V2.1.2 «ключ … время N» since the last stop: the heroes glide from where they stood (animFrom, the last stop's
    // sprites) to this stop's places, each over its own seconds, as Ren'Py's «ease» does in the game
    QVector<SpriteShow> animFrom;
    QHash<QString, double> animSeconds;   // tag -> seconds
};

// V2.1.2 the heroes of a «ключ … время» part of the way there: t = 0…1 of the longest glide, each hero over its own
// seconds, eased as Ren'Py's «ease» (renpy/atl.py); heroes that did not stand on the last stop are where they are
SceneState glideScene(SceneState st, const QVector<SpriteShow>& from, const QHash<QString, double>& seconds, double t);

class Cinema {
public:
    void load(const QString& storyText, const EsAssets* es);
    CinemaStop start(int line);             // from this story line (<= the first scene: the mod's very start)
    CinemaStop next(int option = -1);       // after a click / the option picked (-1 on a Choice = its timer ran out)
    int steps() const { return m_steps; }
    // «Сломай мой мод»: no frames (hundreds of walks in a second); the scenes this walk went into (label names)
    void setBlind(bool on) { m_blind = on; }
    // «наугад» throws the Cinema's own dice, seeded at start(): the same seed + the same clicks = the same walk
    void setSeed(quint32 seed) { m_seed = seed; }
    const QSet<QString>& visited() const { return m_visited; }
    QString labelOf(const QString& scene) const { return label(scene); }

private:
    struct Target { QString scene; bool timeout = false; };
    struct ChoiceOpt { int ord = 0, from = 0, to = 0; QString target; bool exit = false, always = false, locked = false; };
    struct Branch { int end = 0, resume = 0; QString target; int loopHeader = -1; };
    CinemaStop run();
    CinemaStop stop(CinemaStop::Kind k, int idx);
    bool jumpTo(const QString& scene, QString* note, bool keepBranches = false);
    int afterBlock(int from) const;           // a line of a choice block (its own depth) -> the line after its end
    bool insideChoice(int at) const;          // the line sits in a «выбор … конецвыбора» block
    void enterOption(const ChoiceOpt& o);
    void absorb(const QString& s);            // variables, items, music… of a line walked without stopping
    double eval(const QString& expr) const;
    bool test(const QString& cond) const;     // an option's condition in words: «славя 3 и не ссора», «предмет ключ»
    QString label(const QString& scene) const;

    const EsAssets* m_es = nullptr;
    QString m_modId;
    QStringList m_lines, m_prefix, m_path;
    // the last stop: its heroes and how far the path had gone (the «ключ» lines after it animate the next one)
    QVector<SpriteShow> m_lastSprites;
    int m_lastPathLen = -1;
    QString m_lastPathTail;
    int m_cardTurn = 0;                       // «открытка»: 1 = its face was shown, the next click turns it over
    QVector<int> m_srcOf;
    QHash<QString, int> m_labels;             // Ren'Py label -> index of its «: scene» line
    int m_pc = 0, m_steps = 0;
    CinemaStop m_pageStop;                    // a long line: its boxes are turned one per click, like the game
    int m_page = 0;
    QString m_scene;
    QVector<QPair<int, QString>> m_calls;     // return address, scene
    QHash<QString, double> m_vars;            // «установить / прибавить» (persistent ones as "p:<name>")
    QHash<QString, double> m_defaults;        // the mod's «переменная» / «шкала» values: every walk starts from them
    QSet<QString> m_visited;
    bool m_blind = false;
    quint32 m_seed = 0;
    QRandomGenerator m_rng;
    void enterScene(const QString& name) { m_scene = name; m_visited.insert(label(name)); }
    QSet<QString> m_items;
    QString m_music, m_ambience;
    QStringList m_sounds, m_popups;
    QString m_moment;
    // the pending choice
    QVector<QString> m_targets;               // per option: scene ("" = go on after the block)
    QString m_timeoutTarget;
    bool m_timeoutSet = false;
    int m_resume = -1;                        // where the story goes on after a choice
    // «выбор» (Выбор 2.0): the options on screen, the branches being walked, the «по кругу» menus' asked options
    QVector<ChoiceOpt> m_opts;
    int m_choiceLine = -1, m_choiceAfter = 0;
    bool m_choiceLoop = false;
    CinemaStop m_choiceStop;
    QVector<Branch> m_branches;
    QHash<int, QSet<int>> m_seen;
    int m_again = -1;
    bool m_ended = false;
    bool m_fromMenu = false;                  // the mod's main menu is up: its music stops when a route starts
    // the camp map «обход» (as the game: a visited place goes out, all visited -> «готово»): places seen per map
    QHash<QString, QSet<QString>> m_mapSeen;
    QStringList m_mapZones;                   // the map on screen: each option's place
    QString m_mapKey;
    bool m_mapTour = false;
};

} // namespace gb
