// GenryBL V1 - puts a compiled mod into Everlasting Summer and runs it.
//   game/mods/<mod_id>/<mod_id>.rpy + the project's own images/audio/video/fonts
//   "Играть" starts ES in a window straight at the scene under the cursor, with its
//   own save folder, through a temporary hook that is removed when the game closes.
#pragma once
#include "Compiler.h"

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
QString esExe(const QString& esRoot);
// project images as Ren'Py names: "bg my_room.png" -> ("bg my_room", mods/<id>/images/bg my_room.png)
QVector<CustomImage> customImages(const QString& assetsDir, const QString& modId);
QHash<QString, QString> customImageFiles(const QString& assetsDir);    // name -> absolute file (for the preview)

BuildReport install(const BuildEnv& env, const QString& storyText, const CompileOptions& opt = {}, const BuildLog& log = {});
// Start ES at `label` (empty = the mod's start) in a window with env.saveDir; returns the pid or 0.
qint64 runAt(const BuildEnv& env, const QString& modId, const QString& label, QString* err);
void removeHook(const QString& esRoot);
bool isRunning(qint64 pid);
// "Everlasting Summer.exe" <root> lint -> the lines of the report that mention this mod (empty = clean)
QStringList lint(const QString& esRoot, const QString& modId, QString* err, int timeoutMs = 180000);
bool zipMod(const QString& modDir, const QString& zipPath, QString* err);
// «Экспорт» (V1): the mod as OTHER people get it.
// every "mods/<id>/..." file the mod's .rpy point at that is not inside its folder (empty = self-contained)
QStringList missingModFiles(const QString& modDir, const QString& modId);
// the ZIP for players: <id>/... + КАК_УСТАНОВИТЬ.txt, file names in UTF-8 (Qt's zip writer marks them - the old
// tar-made archive could turn Cyrillic names into garbage on another PC), then read back and compared byte for byte
bool exportZip(const QString& modDir, const QString& modId, const QString& modName, const QString& zipPath, QString* err);
// what the game's own Workshop uploader wants: <dest>/mods/<id>/... + КАК_ВЫЛОЖИТЬ.txt (preview.jpg: the caller)
bool exportWorkshopFolder(const QString& modDir, const QString& modId, const QString& modName, const QString& dest, QString* err);
// game/mods/<id>/.genrybl_owner: the project folder that built the mod. Another copy of GenryBL (a developer
// own build vs the public one) never overwrites a mod it did not build. "" = free (or ours).
QString modOwner(const QString& esRoot, const QString& modId);
QString modOwnerFile(const QString& esRoot, const QString& modId);

} // namespace build
} // namespace gb
