// GenryBL V1 - when the game falls: «Играть» (F5) started Everlasting Summer, and Ren'Py wrote traceback.txt (an error
// while playing) or errors.txt (a script it could not even read) in the game's folder. This reads the fresh one and says
// it in the writer's terms: which mod it is (ours or somebody else's from the Workshop), which scene and which line of
// the STORY (the compiled .rpy has no map back, so the scene comes from the label above the line and the line from the
// words the Ren'Py line and the story line share), and what went wrong in plain words.
#pragma once
#include "Compiler.h"

#include <QString>

namespace gb {

struct CrashReport {
    bool found = false;
    QString kind;          // runtime (traceback.txt) | parse (errors.txt)
    QString file;          // as Ren'Py wrote it: "game/mods/genry_x/genry_x.rpy"
    int rpyLine = 0;
    QString rpyCode;       // the Ren'Py line it fell on
    QString error;         // the error line: "Exception: Image 'sl smle pioneer' not found."
    QString what;          // image | label | name | file | syntax | other
    QString arg;           // the image / label / name / file the error names
    QString raw;           // the report as the game wrote it (its first part)
    QString mod;           // the mod folder the file is in ("" - the game itself)
    QString workshopId;    // the file is in a Workshop item
    bool ours = false;     // it is this project's mod
    int storyLine = 0;     // the story line it maps to (0 = not found)
    QString scene;         // the story scene it is in
};

// The crash written after sinceMs (ms since epoch) in the game at esRoot, if any. modId / storyText: the project that
// was playing, to point the error at its line.
CrashReport readCrash(const QString& esRoot, qint64 sinceMs, const QString& modId, const QString& storyText, const CompileOptions& opt = {});

} // namespace gb
