// GenryBL V1 - puts a compiled mod into Everlasting Summer and runs it.
//   game/mods/<mod_id>/<mod_id>.rpy + the project's own images/audio/video/fonts
//   "Играть" starts ES in a window straight at the scene under the cursor, with its
//   own save folder, through a temporary hook that is removed when the game closes.
#pragma once
#include "Compiler.h"
#include "Lint.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace gb {

struct BuildEnv {
    QString esRoot;       // Everlasting Summer folder (Everlasting Summer.exe + game/)
    QString dataDir;      // V1 data (mod_assets/genry_phone_body.png)
    QString assetsDir;    // project assets: images/ audio/ video/ fonts/
    QString backupsDir;
    QString saveDir;      // isolated ES save folder for test runs
    const class EsAssets* es = nullptr;   // sprites for the «румянец/пот/слёзы/мокрая» overlays (loaded on demand if null)
};

struct BuildReport {
    bool ok = false;
    QString error;
    QString modDir;
    QString modFile;
    ModMeta meta;
    int copied = 0;
    QStringList labels;   // labels defined in the generated .rpy
};

using BuildLog = std::function<void(const QString&)>;

namespace build {

QString findEsRoot(const QStringList& candidates);
// where Steam put Everlasting Summer (app 331470) on THIS computer: the Steam registry keys, every
// library of libraryfolders.vdf, then the usual folders of every drive; "" = not found
QString detectEsRoot();
bool isEsRoot(const QString& dir);
// which Ren'Py the game in that folder runs: "7.4.11" (the usual Steam build, Python 2) or "8.3.4" (the «renpy8» beta
// branch, Python 3); "" = not known. The mods GenryBL writes run on both - this only tells the maker which one is here.
QString esRenpyVersion(const QString& esRoot);
QString esExe(const QString& esRoot);
// project images as Ren'Py names: "bg my_room.png" -> ("bg my_room", mods/<id>/images/bg my_room.png)
QVector<CustomImage> customImages(const QString& assetsDir, const QString& modId);
QHash<QString, QString> customImageFiles(const QString& assetsDir);    // name -> absolute file (for the preview)
// the project's own files before «Играть»: broken pictures (the game dies on them), giants, «лже-буквы» in names
QVector<LintIssue> checkAssets(const QString& assetsDir);

BuildReport install(const BuildEnv& env, const QString& storyText, const CompileOptions& opt = {}, const BuildLog& log = {});
// Start ES at `label` (empty = the mod's start) in a window with env.saveDir; returns the pid or 0.
qint64 runAt(const BuildEnv& env, const QString& modId, const QString& label, QString* err);
void removeHook(const QString& esRoot);
bool isRunning(qint64 pid);
// "Everlasting Summer.exe" <root> lint -> the lines of the report that mention this mod (empty = clean)
QStringList lint(const QString& esRoot, const QString& modId, QString* err, int timeoutMs = 180000);

// The game itself checks installed mods in ONE start: «genry_smoke» (data/gate/genry_gate_smoke.rpy, put into
// game/mods/_genry_gate only for the run) builds every screen of the mods once with the arguments the mods call
// them with and checks every name their python reads - what the game's lint never looks at (V2.0 died on a menu
// that way) - and then the game's own lint checks on the mods' statements (the whole game with the workshop takes
// minutes). Hits: "mods/<id>/<id>.rpy:<line>: <what>" (empty = clean).
struct GameCheck {
    bool ran = false;            // the game started, checked and reported
    QString error;               // why it did not
    QStringList hits;
    QStringList lintHits;        // the part of `hits` that is the game's lint (the rest: crashes the game would have)
    int statements = 0, screens = 0;
    QString console;             // what the game printed (a mod that does not even parse: its «File "…", line N»)
};
GameCheck gameCheck(const QString& esRoot, const QString& dataDir, const QStringList& modIds, int timeoutMs = 900000);
void removeGate(const QString& esRoot);          // a check that never finished (GenryBL was closed) leaves no trace
bool zipMod(const QString& modDir, const QString& zipPath, QString* err);
// «Экспорт» (V1): the mod as OTHER people get it.
// every "mods/<id>/..." file the mod's .rpy point at that is not inside its folder (empty = self-contained)
QStringList missingModFiles(const QString& modDir, const QString& modId);
// V2.1.2 the statement names of the .rpyc the game just compiled (a check, an export, a publish) go to the project
// (<project>/genry_names/<id>.rpyc): the next build borrows them even if the mod folder is gone by then (a player's
// copy from the Workshop only, the showcase removed from game/mods) - saves made in the published version live on
bool keepNames(const BuildEnv& env, const QString& modId);
// the ZIP for players: <id>/... + КАК_УСТАНОВИТЬ.txt, file names in UTF-8 (Qt's zip writer marks them - the old
// tar-made archive could turn Cyrillic names into garbage on another PC), then read back and compared byte for byte
bool exportZip(const QString& modDir, const QString& modId, const QString& modName, const QString& zipPath, QString* err);
// «Мастерская GenryBL» -> «Установить»: a players' archive (<mod>/…, or mods/<mod>/…) into game/mods. A folder that is
// there already is replaced only if the catalog put it there (its .genrybl_catalog mark); *folder = the mod's folder
bool installModArchive(const QString& zipPath, const QString& modsDir, const QString& expectFolder, QString* folder, QString* err);
// what the game's own Workshop uploader wants: <dest>/mods/<id>/... + КАК_ВЫЛОЖИТЬ.txt (preview.jpg: the caller)
bool exportWorkshopFolder(const QString& modDir, const QString& modId, const QString& modName, const QString& dest, QString* err);
// «Андроид»: the mod the way the mobile Бесконечное лето takes it (the community's ESTool rules, es-doc «mobile_port»):
// the game there runs at 1280×720 - the mod's pictures two thirds, the pixel numbers in its code two thirds, Latin file
// names; ZIP = mods/<id>/… + КАК_УСТАНОВИТЬ_ANDROID.txt. `notes`: what to look at on a phone (the camp map, workshop clothes…)
bool exportAndroid(const QString& modDir, const QString& modId, const QString& modName, const QString& zipPath, QString* err, QStringList* notes = nullptr);
// the pixel numbers of a line of Ren'Py code times `k` (xpos 300 -> 200, size 36 -> 24, xysize (1920, 1080) -> (1280, 720))
QString scalePixels(const QString& line, double k);
// game/mods/<id>/.genrybl_owner: the project folder that built the mod. Another copy of GenryBL (a developer
// own build vs the public one) never overwrites a mod it did not build. "" = free (or ours).
QString modOwner(const QString& esRoot, const QString& modId);
QString modOwnerFile(const QString& esRoot, const QString& modId);

} // namespace build
} // namespace gb
