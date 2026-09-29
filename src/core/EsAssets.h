// GenryBL V1 - Everlasting Summer's own vocabulary: every sprite with its exact layer
// list, backgrounds, CGs, music/sfx/ambience ids and the cast (names + colours).
// Read in place from the game's archive.rpa - nothing is copied to disk.
#pragma once
#include "Rpa.h"

#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gb {

struct EsLayer {
    int x = 0, y = 0;
    QString path;
};

struct EsSprite {
    int w = 900, h = 1080;
    QVector<EsLayer> layers;
    bool hasTint = false;
    double tint[3] = {1, 1, 1};
    double opacity = 1.0;
};

struct EsImage {
    QString path;       // game path (images/bg/x.jpg)
    QString color;      // "#000" for colour images
    bool sepia = false;
    bool hasTint = false;
    double tint[3] = {1, 1, 1};
};

struct EsCharacter {
    QString id;
    QString name;                       // Russian display name
    QHash<QString, QString> colors;     // day/sunset/night/prolog -> #rrggbb
};

// The camp map (ES media.rpy store.map_zones + control/mapclass.rpyc): open places are cut
// from maps/map_available.jpg over maps/map.jpg, hovered from map_selected.jpg, a chibi
// icon blinks at the place's top-left corner.
struct EsMapZone {
    QString id, title;
    int x1, y1, x2, y2;
    int cx, cy;                 // V1: where the heroine's face stands - beside the place's name, not on it
};
const QVector<EsMapZone>& esMapZones();
// tracks the game has as files but not in music_list (es-doc names them: the Zhenya route's songs, Kostry, Miku's lesson):
// word -> game path. V1 «музыка free_love» plays the file itself.
const QVector<QPair<QString, QString>>& esExtraMusic();
QString esExtraMusicPath(const QString& word);
QStringList esChibiIds();                                   // "sl", "dv", ... (ES store.map_chibi)
// The Steam build ships no map_icon_nXX.png (a stock «set_chibi» shows a missing file), so V1
// brings its own round faces: data/mod_assets/chibi/<file>.png -> <mod>/images/genry_chibi/
QString esChibiFile(const QString& id);                     // "sl", "unknown" for "?", "" if not a chibi id
QString esChibiId(const QString& word);                     // "sl" / "Славя" / "славя" -> "sl", "" if none

// «карта [обход] площадь: сцена @sl, beach: сцена2 @Алиса, готово: дальше» - one grammar for the compiler,
// the check and the preview. A place is the ES key or its Russian name; «обход» = ES day 2's walk-around list:
// a visited place goes out, «готово» is where the mod goes once all are visited.
struct MapEntry {
    QString raw, zone, target, chibi;       // zone = ES key ("" if the place is unknown), chibi = ES id or ""
};
struct MapSpec {
    bool tour = false;
    QVector<MapEntry> places;
    QString done;                           // «готово: сцена»
};
MapSpec parseMapSpec(const QString& rest);
QString esMapZoneId(const QString& word);                   // "square" / "площадь" / "Площадь" -> "square"
const EsMapZone* esMapZone(const QString& id);
QString esChibiName(const QString& id);

// The Workshop «hentai patch» (1118110148, the 18+ scenes cut from the Steam build): everything it brings
// besides the old bodies - the 20 CGs the Steam scripts still declare, the uncensored versions its
// «Альтернативные» option swaps in (miku_h_1, miku_h_2, uvao_h) and the old day cards.
struct EsPatchImage {
    QString name;      // Ren'Py image name: "cg uvao_h", "cg card_12_uvao_old"
    QString path;      // file in the patch archive
    bool declared;     // the Steam scripts declare it (else a mod declares it itself)
};
const QVector<EsPatchImage>& esPatchImages();
const EsPatchImage* esPatchImage(const QString& name);     // "cg x" -> it, nullptr if not the patch's
bool esPatchInFolder(const EsPatchImage& p);                // shown in the «18+» folder (no Ульяна)

class EsAssets {
public:
    bool load(const QString& catalogJson, const QString& esRoot, QString* error);
    bool ready() const { return m_ready; }
    const Vfs& vfs() const { return m_vfs; }
    QString esRoot() const { return m_root; }

    QHash<QString, EsSprite> sprites;       // "dv smile pioneer" / "... close" / "... far"
    QHash<QString, EsImage> images;         // "bg ext_road_day", "cg d1_food_normal", "prologue_dream", ...
    QMap<QString, QString> music;           // music_list id -> path (+ esExtraMusic() the game has as files)
    QMap<QString, QString> sounds;          // sfx_* -> path
    QMap<QString, QString> ambience;        // ambience_* -> path
    QHash<QString, EsCharacter> characters;
    QSet<QString> missingImages;             // declared by ES scripts, files absent (18+ patch not installed)
    QSet<QString> missingAudio;              // music/sfx/ambience ids whose file the Steam build lacks

    // Steam Workshop "hentai patch" (removed 18+ scenes): i8_data.rpa brings the 20 CGs
    // the Steam build declares but lacks, and swaps the character bodies for the old ones.
    static constexpr const char* kHentaiPatchId = "1118110148";
    bool hentaiPatch() const;                              // there: subscribed, or GenryBL's own copy (data/patch)
    bool hentaiPatchBundled() const { return m_vfs.bundledMounted(); }   // GenryBL's own copy is in use
    QString imageSource(const QString& name) const;        // "" = the game, else workshop item id

    QStringList spriteTags() const;                        // dv un sl ... (main cast first)
    QStringList spriteNames(const QString& tag) const;     // normal-distance names, sorted
    QStringList backgrounds() const;                       // "ext_road_day" (without "bg ")
    QStringList cgs() const;
    QString characterName(const QString& id) const;
    QString characterColor(const QString& id, const QString& timeOfDay) const;

private:
    Vfs m_vfs;
    QString m_root;
    QHash<QString, QString> m_imageSource;
    bool m_ready = false;
};

} // namespace gb
