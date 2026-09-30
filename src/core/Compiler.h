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
    QString author;                                  // @author; empty = no «автор:» line on the mod menu
    QString heroName;                                // @hero_name: the hero instead of «Семён» (the player may retype it)
    QString heroAsk;                                 // @hero_ask: "menu" (a button «Имя» on the mod menu) | "start" | ""
    bool heroShe = false;                            // @hero_gender она: the hero is a girl («[проснулся/проснулась]»)
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
    // V1 «сам расставит»: the game's audio by kind and the project's own files by what they were imported as
    // (file name -> music | sfx | ambience | voice). A track written with «звук» still plays as music, a sound
    // written with «музыка» as a sound, «музыка audio/x.ogg» as the project's file (rightAudioKind).
    QSet<QString> esMusic, esSounds, esAmbience;
    QHash<QString, QString> audioKinds;
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
    bool eyesClosed = false;                  // V1: the eyes are shut right now (text order within a scene)
    bool autoOpen = false;                    // V1: «скачок» shut them - GenryBL opens them before the next line
    QString timeOfDay;                        // V1: day / sunset / night the story set last («время» or a «фон»)
    bool timeSynced = false;                  // V1: this scene has set the sprites' time already
    bool timeExplicit = false;                // V1: that time came from «время», not from a picture
    QString bgImage;                          // V1: the place on screen now ("ext_square_day"), "" = a CG / black / none
};

// `image <name> = "<path>"` for the mod's own pictures (paths relative to game/).
struct CustomImage {
    QString name;
    QString path;
};

ModMeta parseMeta(const QStringList& lines, QStringList* body, const CompileOptions& opt = {});
bool hasPlayableBody(const QStringList& body, const CompileOptions& opt = {});
QStringList compileLine(const QString& line, const QString& modId, CompileState& st, const CompileOptions& opt = {});
// V1: an audio line in the right command for what it plays («звук sunny_day» -> «музыка sunny_day», «музыкафайл
// audio/door.ogg» of a file imported as a sound -> «звукфайл …»); the line as it was when it is right already.
// `kind` = what the audio really is: music | sfx | ambience
QString rightAudioKind(const QString& line, const CompileOptions& opt, QString* kind = nullptr);
// «менюмода»: a button's kind by its target or caption (Дни -> главы, Фотографии -> галерея, Выселиться -> выход…)
QString menuButtonKind(const QString& word);
// «менюмода <стиль>»: panel | es | 7dl | notebook | diary | board | monitor | noir | live | cinema | map
QString menuStyleKey(const QString& word);
// «менюмода свой»: its parts, one line each - «кнопки слева|справа|по центру|снизу», «вид текст|таблички|бл|неон»,
// «цвет #hex», «частицы пыль|листья|светлячки|дождь|снег|сердца|по часам|нет», «появление выезд|проявление|снизу|печать»
struct MenuParts { QString layout, look, accent, fx, enter; };
bool menuPartLine(const QString& lowerWord, const QString& rest, MenuParts* parts);   // true: the line was one of them
// «менюмода карта»: the camp place of each button by its kind (menuButtonKind: «галерея» - library, «выход» - the gates,
// a scene - the square, the beach…)
QStringList menuMapPlaces(const QStringList& kinds);
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
// «ачивка ключ | Название [| картинка-попапа] [| описание=…] [| раздел=Славя] [| нужно=a, b] [| картинка=cg …] [| скрытое]
// [| платина]» (Достижения 2.0): the same line opens it; «платина» never opens by a line - it comes when the rest are there
struct AchSpec {
    QString key, title, icon, desc, section, image;
    QStringList needs;                               // keys as written
    bool hidden = false;                             // «???» and the closed eye until it comes
    bool plat = false;
};
AchSpec parseAchievement(const QString& rest);
// «фонарик [вкл|выкл|1.4] [| цвет=#012]»; ok=false - the line is a sentence («Фонарик погас.»), not the command
struct FlashSpec { bool ok = false, off = false; double zoom = 1.5; QString color = QStringLiteral("#050810"); };
FlashSpec parseFlashlight(const QString& rest);
// «кодовыйзамок 1905 | Год основания лагеря? | верно -> сейф | неверно -> тупик | попыток=3»
struct CodeLockSpec { QString code, hint, okTarget, badTarget; int tries = 0; };
CodeLockSpec parseCodeLock(const QString& rest);

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

// V1 «Выбор 2.0». The header: «выбор [вид] [по кругу] [наугад]» - «по кругу»: questions, a chosen option goes away and
// the menu comes back until «[выход]» or nothing is left; «наугад»: the game picks one of the options itself.
struct ChoiceHead {
    QString style;                                   // choiceStyleOf: es | buttons | images | phone | timed
    QString secs;                                    // choiceSeconds
    bool loop = false;
    bool random = false;
};
ChoiceHead parseChoiceHead(const QString& rest);
// One option: «- Текст [-> сцена] [| картинка] [если …] [нужно … | подсказка] [выход] [всегда]». Lines under it (up to the
// next option or «конецвыбора») are what happens when it is chosen; then the story goes on after the choice.
struct ChoiceItemSpec {
    QString caption, target, image;
    QString cond;                                    // [если славя 3]: hidden until it is true
    QString need, hint;                              // [нужно славя 3 | Славя должна тебе верить]: shown locked until true
    bool exit = false;                               // [выход]: ends a «по кругу» menu
    bool always = false;                             // [всегда]: stays in a «по кругу» menu after being chosen
};
bool isChoiceItemLine(const QString& stripped);
ChoiceItemSpec parseChoiceItem(const QString& stripped);
// the options' effects out of their brackets, as the story's own commands for the lines under the option:
// [+1 Славя] -> «прибавить Славя 1», [запомнит Алиса] -> «запомнит Алиса», [флаг помог] -> «установить помог True»
QStringList choiceItemEffects(const QString& stripped, QString* cleaned);
// the names a condition reads («славя 3 и не ссора» -> славя, ссора; «предмет ключ» reads no variable)
QStringList choiceConditionVars(const QString& cond);

} // namespace gb
