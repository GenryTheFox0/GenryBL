// GenryBL V1 - «Таймлайн»: the scene under the cursor the way a video editor shows a clip - every line of it is a beat,
// and what is on screen and in the ears at each beat lies on tracks: the background, each character (a clip from
// «показать» to «убрать», cut where the emotion changes), the words, music, ambience, sounds, effects and the ways
// out. The editor draws it (qml/Timeline.qml), a click on a clip takes the cursor to its line, a drag moves the line.
#pragma once
#include "Compiler.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace gb {

struct TimelineClip {
    int from = 0, to = 0;          // beats, [from, to) - a one-beat clip has to = from + 1
    int line = 0;                  // the story line (1-based) that makes it
    QString label;                 // what it shows: «dv smile pioneer», «Славя: Привет…», «sunny_day»
    QString kind;                  // bg | char | say | music | amb | sfx | fx | moment | flow | other
    bool movable = true;           // a single line that can be dragged to another beat
};

struct TimelineTrack {
    QString id, title;             // "bg" / "char:dv" / "say" …, the name on the track's head
    QVector<TimelineClip> clips;
};

struct SceneTimeline {
    QString scene;                 // the scene's name ("" before the first scene)
    int headLine = 0;              // «: scene» line (0 = none)
    QVector<int> beatLines;        // beat -> story line
    QVector<TimelineTrack> tracks;
};

SceneTimeline sceneTimeline(const QString& storyText, int line, const CompileOptions& opt = {});

} // namespace gb
