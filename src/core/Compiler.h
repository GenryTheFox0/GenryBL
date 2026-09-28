// GenryBL V1 - story.txt -> Everlasting Summer mod .rpy.
// A port of the old GenryModEngine builder.py; legacy=true reproduces it byte for byte
// (proved by the selftest against tests/golden), legacy=false (default) fixes its bugs.
#pragma once
#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gb {

struct ModMeta {
    QString modId = QStringLiteral("genry_easy_mod");
    QString modName = QString::fromUtf8("Простой мод Генри");
    QString author = QStringLiteral("GenryTheFox");
    QString titleFont;
    QString titleColor;
    QString titleSize;
    QString titleStyle;          // V1 «@mod_title_style b i»: bold / italic
};

struct CompileOptions {
    bool legacy = false;
    // Character ids Everlasting Summer already defines (dv, un, me, ...). When set (V1 mode),
    // an unknown speaker becomes genry_sp_<name> and gets its own Character() declaration
    // instead of crashing the game with a NameError.
    QSet<QString> knownSpeakers;
};

// Per-story state the old builder threaded through compile_line.
struct CompileState {
    QHash<QString, QString> speakers;   // lower-cased name -> Character id
    bool activePrologueDream = false;
    QStringList activeSpriteTags;
    bool hasPhoneMin = false;
    int phoneMin = 0;
    QMap<QString, QString> unknownSpeakers;   // V1: genry_sp_x -> name as written
    QSet<QString> modVars;                    // V1: variables the story itself sets (namespaced)
    bool modMenu = false;                     // V1: «менюмода» - the mod opens with its own menu, ": start" = <mod>__start
    QHash<QString, QStringList> meters;       // V1: «шкала» var (namespaced) -> {title, #color, min, max}
    QSet<QString> overlayImages;              // V1: "dv smile pioneer genry_ov_blush" (Overlays.h)
};

// `image <name> = "<path>"` for the mod's own pictures (paths relative to game/).
struct CustomImage {
    QString name;
    QString path;
};

ModMeta parseMeta(const QStringList& lines, QStringList* body, const CompileOptions& opt = {});
bool hasPlayableBody(const QStringList& body, const CompileOptions& opt = {});
QStringList compileLine(const QString& line, const QString& modId, CompileState& st, const CompileOptions& opt = {});
// Returns the .rpy text; on a malformed choice block returns {} and sets *error ("ValueError: ...").
QString compileStory(const ModMeta& meta, const QStringList& body, const CompileOptions& opt = {},
                     const QVector<CustomImage>& images = {}, QString* error = nullptr);
// Convenience: whole story text (with @meta lines) -> .rpy
QString compileText(const QString& storyText, const CompileOptions& opt = {}, QString* error = nullptr,
                    const QVector<CustomImage>& images = {});

// ---- shared vocabulary (the old RENAMES & friends) ----
QString normalizeCommand(const QString& word);
bool isCommandName(const QString& id);
QString slugOf(const QString& s, const QString& fallback, const CompileOptions& opt);
QString persistentKey(const QString& modId, const QString& name, const QString& fallback, const CompileOptions& opt);
QString relModPath(const QString& modId, const QString& subpath);
// the Ren'Py label a scene name compiles to (V1: <mod>__<scene>; ": start" -> the mod id)
QString sceneLabel(const QString& modId, const QString& name, const CompileOptions& opt = {});
bool isEffect(const QString& w);
bool isPosition(const QString& w);
bool isWalkPosition(const QString& w);
double walkXalign(const QString& w);
QString speakerId(const QString& name, const CompileState& st, const CompileOptions& opt);
const QHash<QString, QString>& renames();
const QHash<QString, QString>& speakers();
QStringList weatherKeys();
QString weatherKey(const QString& word);         // "снег" -> "snow", "" if unknown
QStringList filterKeys();
QString filterKey(const QString& word);           // "сепия" -> "sepia", "" if unknown
// V1 options: after the positional fields a command may take «ключ=значение» items and switch
// words, e.g. «запомнит Алиса | Ты помог | флаг=помог | где=справа». Keys have Russian and English
// spellings (текст/text, флаг/flag, лицо/face, где/where, время/time, цвет/color, звук/sound, вид/kind, ...).
struct V1Opts {
    QStringList pos;                     // positional fields, in order
    QHash<QString, QString> kv;          // canonical key -> value
    QSet<QString> sw;                    // switch words (canonical)
    QString get(const QString& k, const QString& def = QString()) const { return kv.value(k, def); }
    bool has(const QString& s) const { return sw.contains(s) || kv.contains(s); }
};
QString v1OptKey(const QString& key);
V1Opts parseV1Opts(const QString& rest, const QHash<QString, QString>& switches = {});
QString rememberWhere(const QString& where);        // слева/справа/центр/снизу -> tl/tr/tc/bc

// CG names the Steam build declares but only the 18+ workshop patch (1118110148) has files for
const QStringList& hentaiPatchCgs();
// V1 image choice («выбор картинки», «- Текст -> сцена | картинка»): the picture of one option
// as a Ren'Py image name; kind = "sprite" (ES/custom character) or "bg" (background, CG, own PNG)
QString choiceImage(const QString& raw, QString* kind, const QSet<QString>& customImages = {});
// «выбор [вид]»: "es" = the game's own menu (default), "buttons" = the old constructor's dark
// buttons («кнопки»), "images" = the 7DL-style visual choice («визуальный», «картинки», «7дл»)
QString choiceStyleOf(const QString& word);          // + "timed": «выбор на время 8» (Telltale)
QString choiceSeconds(const QString& style);         // "8.0" (2..60, default 10)
bool isTimeoutWord(const QString& s);                // «время вышло -> сцена» inside a timed choice

} // namespace gb
