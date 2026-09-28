// GenryBL V1 selftest - proves the C++ remake against the old GenryModEngine builder.py.
//   [1] tables       RENAMES/SPEAKERS/... == old builder (tests/golden/tables.json)
//   [2] lines        every alias x args through compile_line, legacy mode, byte for byte
//   [3] stories      whole compile_story output, legacy mode, byte for byte
//   [4] V1 fixes     the old builder's real bugs are gone in V1 mode
//   [6] screenplay   «пиши как сценарий»: play lines -> commands, pasted text -> story
//   [7] wardrobe     workshop outfits on ES bodies: resolve, 18+ only for grown-ups, compile, lint, preview
//   [8] cinema       «кино-режим»: the route the game takes (choices, «если», calls, timers, _next scenes)
#include "Builder.h"
#include "Cinema.h"
#include "Compiler.h"
#include "EsAssets.h"
#include "Forms.h"
#include "Library.h"
#include "Lint.h"
#include "Overlays.h"
#include "Renderer.h"
#include "Scene.h"
#include "Screenplay.h"
#include "Py.h"
#include "Text.h"
#include "Wardrobe.h"

#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

using namespace gb;

static int g_fail = 0, g_ok = 0;
static QTextStream out(stdout);

static void check(bool cond, const QString& what)
{
    if (cond) ++g_ok;
    else { ++g_fail; out << "  FAIL: " << what << "\n"; }
    out.flush();
}

static QByteArray readAll(const QString& p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static QString gold(const QString& name) { return QStringLiteral(GB_SOURCE_DIR "/tests/golden/") + name; }

static void testTables()
{
    out << "[1] tables\n";
    const QJsonObject t = QJsonDocument::fromJson(readAll(gold("tables.json"))).object();
    const QJsonObject rn = t["RENAMES"].toObject();
    int bad = 0;
    for (auto it = rn.begin(); it != rn.end(); ++it)
        if (renames().value(it.key()) != it.value().toString()) { if (bad++ < 5) out << "    RENAMES " << it.key() << "\n"; }
    check(bad == 0 && renames().size() == rn.size(), QStringLiteral("RENAMES == old builder (%1 aliases)").arg(rn.size()));
    const QJsonObject sp = t["SPEAKERS"].toObject();
    bad = 0;
    for (auto it = sp.begin(); it != sp.end(); ++it) if (speakers().value(it.key()) != it.value().toString()) ++bad;
    check(bad == 0 && speakers().size() == sp.size(), "SPEAKERS == old builder");
    int missing = 0;
    for (const QJsonValue& v : t["COMMAND_NAMES"].toArray()) if (!isCommandName(v.toString())) ++missing;
    check(missing == 0, "COMMAND_NAMES");
    QStringList wk;
    for (const QJsonValue& v : t["GENRY_WEATHER_ORDER"].toArray()) wk << v.toString();
    check(weatherKeys() == wk, "weather order");
    QStringList fk;
    for (const QJsonValue& v : t["GENRY_FILTER_ORDER"].toArray()) fk << v.toString();
    check(filterKeys() == fk, "filter order");
}

static void testLines()
{
    out << "[2] compile_line, legacy == python\n";
    const QJsonArray cases = QJsonDocument::fromJson(readAll(gold("lines.json"))).array();
    CompileOptions legacy;
    legacy.legacy = true;
    int bad = 0;
    for (const QJsonValue& v : cases) {
        const QJsonObject o = v.toObject();
        const QString line = o["line"].toString();
        QStringList want;
        for (const QJsonValue& w : o["out"].toArray()) want << w.toString();
        CompileState st;
        const QStringList got = compileLine(line, QStringLiteral("genry_golden"), st, legacy);
        if (got != want) {
            if (bad++ < 12) {
                out << "    line: " << line << "\n";
                for (int i = 0; i < qMax(got.size(), want.size()); ++i)
                    if (got.value(i) != want.value(i)) {
                        out << "      #" << i << " cpp: " << got.value(i) << "\n      #" << i << " py : " << want.value(i) << "\n";
                        break;
                    }
            }
        }
    }
    check(bad == 0, QStringLiteral("%1/%2 lines identical").arg(cases.size() - bad).arg(cases.size()));
}

// The old builder keeps screen-menu targets in a Python set(): the order of the
// auto-appended "label X: return" blocks at the end of the file changes between runs
// (hash seed). V1 emits them in first-seen order; compare that tail as a multiset.
static bool sameModuloTailOrder(const QString& a, const QString& b)
{
    if (a == b) return true;
    auto split = [](const QString& s, QStringList* head, QStringList* blocks) {
        QStringList l = s.split('\n');
        if (!l.isEmpty() && l.last().isEmpty()) l.removeLast();
        while (l.size() >= 3 && l[l.size() - 3].isEmpty() && l[l.size() - 2].startsWith(QLatin1String("label ")) &&
               l.last() == QLatin1String("    return")) {
            blocks->prepend(l[l.size() - 2]);
            l.removeLast(); l.removeLast(); l.removeLast();
        }
        *head = l;
    };
    QStringList ha, hb, ba, bb;
    split(a, &ha, &ba);
    split(b, &hb, &bb);
    ba.sort();
    bb.sort();
    return ha == hb && ba == bb;
}

static void testStories()
{
    out << "[3] compile_story, legacy == python\n";
    const QDir dir(gold("stories"));
    CompileOptions legacy;
    legacy.legacy = true;
    int n = 0, bad = 0;
    for (const QString& f : dir.entryList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name)) {
        const QString name = QFileInfo(f).completeBaseName();
        const QString text = QString::fromUtf8(readAll(dir.filePath(f)));
        QStringList body;
        const ModMeta meta = parseMeta(pySplitLines(text), &body, legacy);
        QString err;
        const QString got = compileStory(meta, body, legacy, {}, &err);
        const QJsonObject m = QJsonDocument::fromJson(readAll(dir.filePath(name + ".meta.json"))).object();
        ++n;
        bool ok = meta.modId == m["mod_id"].toString() && meta.modName == m["mod_name"].toString() &&
                  hasPlayableBody(body, legacy) == m["playable"].toBool();
        if (!ok) out << "    " << name << ": meta/playable differ\n";
        if (QFileInfo::exists(dir.filePath(name + ".error"))) {
            const QString want = QString::fromUtf8(readAll(dir.filePath(name + ".error")));
            if (err != want) { ok = false; out << "    " << name << ": error cpp='" << err << "' py='" << want << "'\n"; }
        } else {
            const QString want = QString::fromUtf8(readAll(dir.filePath(name + ".rpy")));
            if (!sameModuloTailOrder(got, want)) {
                ok = false;
                const QStringList a = got.split('\n'), b = want.split('\n');
                for (int i = 0; i < qMax(a.size(), b.size()); ++i)
                    if (a.value(i) != b.value(i)) {
                        out << "    " << name << ".rpy line " << i + 1 << "\n      cpp: " << a.value(i) << "\n      py : " << b.value(i) << "\n";
                        break;
                    }
            }
        }
        if (!ok) ++bad;
    }
    check(bad == 0 && n > 30, QStringLiteral("%1/%2 stories identical").arg(n - bad).arg(n));
}

// An indented line whose nearest unindented line above is not a block opener (label x:, screen,
// init python:, ...) is a Ren'Py parse error that stops the whole game. "" = the .rpy is sound.
static QString orphanIndent(const QString& rpy)
{
    QString opener;
    const QStringList lines = rpy.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString& l = lines[i];
        if (l.trimmed().isEmpty() || l.trimmed().startsWith(QLatin1Char('#'))) continue;
        if (!l.startsWith(QLatin1Char(' '))) { opener = l; continue; }
        if (!opener.trimmed().endsWith(QLatin1Char(':'))) return QStringLiteral("line %1: %2 (after: %3)").arg(i + 1).arg(l.trimmed(), opener);
    }
    return {};
}

static void testFixes()
{
    out << "[4] V1 fixes\n";
    const CompileOptions v1;
    CompileState st;
    check(compileLine(QStringLiteral("показать dv smile pioneer2 center"), QStringLiteral("m"), st, v1).value(0).contains(QStringLiteral("pioneer2")),
          "pioneer2 (a real ES outfit) is no longer rewritten to pioneer");
    const QString rpy = compileText(QString::fromUtf8("@mod_id Мой мод\nсцена Лес\nтекст а\nпереход Лес второй\nсцена Лес второй\nтекст б\n"
                                                      "постоянная любовь 1\nпостоянная дружба 1\n"),
                                    v1);
    check(rpy.contains(QStringLiteral("label moy_mod__les:")) && rpy.contains(QStringLiteral("label moy_mod__les_vtoroy:")) &&
              rpy.contains(QStringLiteral("jump moy_mod__les_vtoroy")),
          "Russian scene names -> distinct transliterated labels, namespaced by the mod");
    check(rpy.contains(QStringLiteral("$ mods[\"moy_mod\"]")), "Russian mod id -> moy_mod, not genry_easy_mod");
    check(rpy.contains(QStringLiteral("persistent.genry_moy_mod_lyubov")) && rpy.contains(QStringLiteral("persistent.genry_moy_mod_druzhba")),
          "Russian persistent names stay distinct (old: both became genry_mod_value)");

    CompileOptions es;
    es.knownSpeakers = {QStringLiteral("dv"), QStringLiteral("me"), QStringLiteral("un")};
    const QString r2 = compileText(QString::fromUtf8("@mod_id t\nБобик: гав\nАлиса: привет\ndv: снова\nкрик Бобик | hpunch | ГАВ\n"
                                                     "установить любовь 1\nесли любовь >= 1 and trust > 0 -> x\nсмс я: иду\n"),
                                   es);
    check(r2.contains(QStringLiteral("    genry_sp_bobik \"")) && r2.contains(QString::fromUtf8("$ genry_sp_bobik = Character(u\"Бобик\"")),
          "unknown speaker gets its own Character (old: spoke as Генри / NameError)");
    check(r2.contains(QStringLiteral("    dv \"")) && !r2.contains(QStringLiteral("genry_sp_dv")), "ES ids and Russian cast names stay as they are");
    check(r2.contains(QStringLiteral("    if genry_t_lyubov >= 1 and trust > 0:")) && r2.contains(QStringLiteral("default genry_t_lyubov = 0")),
          "Cyrillic variable in a condition matches установить (no SyntaxError), declared with default");
    const QString r3 = compileText(QString::fromUtf8("@mod_id genry_x\n: start\nприбавить trust 1\nпеременная score 5\nесли trust >= 1 -> finale\n"
                                                     ": finale\nуведомление Текст | Лагерь | | #143247 | справа | 3\nпредмет key | Ключ\nпереход start\n"),
                                   es);
    check(r3.contains(QStringLiteral("default genry_x_trust = 0")) && r3.contains(QStringLiteral("default genry_x_score = 5")) &&
              r3.contains(QStringLiteral("$ genry_x_trust += 1")) && r3.contains(QStringLiteral("if genry_x_trust >= 1:")),
          "прибавить without установить: the variable is declared (old: NameError)");
    check(r3.contains(QStringLiteral("jump genry_x__finale")) && r3.contains(QStringLiteral("label genry_x__finale:")) &&
              r3.contains(QStringLiteral("jump genry_x\n")),
          "labels are namespaced; переход start goes to the mod's own start (old: into ES's own story)");
    check(r3.contains(QString::fromUtf8("show screen genry_notify_popup(u\"Лагерь\", u\"Текст\", icon=None, panel_color=\"#143247\", side=\"right\")")) &&
              r3.contains(QString::fromUtf8("panel_color=\"#143247\", side=\"right\")")) && !r3.contains(QString::fromUtf8("panel_color=\"справа\"")),
          "notify/предмет keep empty fields in place (old: colour became \"справа\" -> crash in game)");
    check(r2.contains(QString::fromUtf8("√√")) && !r2.contains(QString::fromUtf8("✓✓")), "read mark uses a glyph the ES font has");
    const QString r4 = compileText(QString::fromUtf8("@mod_id genry_a\n: start\nмузыка everlasting_summer\nзвук sfx_bush_leaves\n"
                                                     "очередьмузыки lightness | always_ready\nдостижение icon.png\n"),
                                   es);
    static const QRegularExpression tempPlay(QStringLiteral("\\n\\s*(play|queue) \\w+ _genry_"));
    check(r4.contains(QStringLiteral("play music genry_resolve_music(\"everlasting_summer\") fadein 2")) &&
              r4.contains(QStringLiteral("play sound genry_resolve_audio_expr(\"sfx_bush_leaves\")")) &&
              r4.contains(QStringLiteral("queue music [_genry_f for _genry_f in [genry_resolve_music(\"lightness\"), genry_resolve_music(\"always_ready\")] if _genry_f]")) &&
              !tempPlay.match(r4).hasMatch(),
          "play/queue name the track itself (old: temp variable -> ES lint: unable to evaluate filename)");
    const QString cgStory = QString::fromUtf8("@mod_id genry_h\n: start\ncg d6_dv_hentai dissolve\nАлиса: ...\ncg uvao_h\ncg card_12_uvao_old\n");
    const QString r5 = compileText(cgStory, es);
    CompileOptions legacy;
    legacy.legacy = true;
    check(r5.contains(QStringLiteral("init 990 python:\n    for _genry_cg, _genry_f, _genry_own in [(\"d6_dv_hentai\", \"images/cg/d6_dv_hentai.jpg\", "
                                     "\"mods/genry_h/images/genry_patch/d6_dv_hentai.jpg\"), (\"uvao_h\", \"images/cg/uvao_h.jpg\", "
                                     "\"mods/genry_h/images/genry_patch/uvao_h.jpg\"), (\"card_12_uvao_old\", \"images/cards/12_uvao_old.png\", "
                                     "\"mods/genry_h/images/genry_patch/12_uvao_old.png\")]:\n"
                                     "        if renpy.loadable(_genry_f):\n            if not renpy.has_image((\"cg\", _genry_cg), exact=True):")) &&
              !compileText(cgStory, legacy).contains(QStringLiteral("_genry_cg")),
          "18+ patch: every picture of it works in a mod (the uncensored CGs and the old cards declared by the mod), "
          "players without the patch get a card instead of a crash (legacy untouched)");
    const QString r6 = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nперсонаж gg Гость #ff9966\nГость: привет\ngg: ку\n"
                                                     "озвученнаяреплика Гость | audio/g1.ogg | эй\n"),
                                   es);
    check(r6.contains(QString::fromUtf8("$ genry_t_chr_gg = Character(u\"Гость\", color=\"#ff9966\"")) &&
              r6.contains(QString::fromUtf8("    genry_t_chr_gg \"привет\"")) && r6.contains(QString::fromUtf8("    genry_t_chr_gg \"ку\"")) &&
              r6.contains(QString::fromUtf8("    genry_t_chr_gg \"эй\"")) && !r6.contains(QStringLiteral("$ gg =")),
          "персонаж gets the mod prefix (old: bare «gg» that any other mod overwrites)");
    const QString r7 = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nвыбор картинки\n- Пойти со Славей -> square | sl smile pioneer\n"
                                                     "- На пляж -> beach | ext_beach_day\n- Просто уйти -> away\nконецвыбора\n"
                                                     "выбор\n- Да -> square\n- Нет -> beach\nконецвыбора\n"
                                                     ": square\nтекст а\n: beach\nтекст б\n: away\nтекст в\n"),
                                   es);
    check(r7.contains(QStringLiteral("    menu (screen=\"genry_choice_img\"):\n")) &&
              r7.contains(QString::fromUtf8("        \"Пойти со Славей\" (img=\"sl smile pioneer\", kind=\"sprite\"):\n            jump genry_t__square")) &&
              r7.contains(QString::fromUtf8("        \"На пляж\" (img=\"bg ext_beach_day\", kind=\"bg\"):")) &&
              r7.contains(QString::fromUtf8("        \"Просто уйти\":\n            jump genry_t__away")) &&
              r7.contains(QStringLiteral("screen genry_choice_img(items):")),
          "выбор картинки: 7DL-style picture menu on a plain menu (rollback/chosen intact)");
    check(r7.contains(QString::fromUtf8("    menu:\n        \"Да\":")) && r7.contains(QStringLiteral("screen genry_choice(items):")) &&
              !r7.contains(QStringLiteral("screen choice(items):")),
          "plain выбор = the game's own menu; the dark one is no longer forced on ES (old: `screen choice` restyled the whole game)");
    const QString r8 = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nвыбор кнопки\n- Да -> a\n- Нет -> b\nконецвыбора\n"
                                                     "выбор визуальный\n- Туда -> a | un shy pioneer\n- Сюда -> b | cg d1_food_normal\nконецвыбора\n"),
                                   es);
    check(r8.contains(QStringLiteral("    menu (screen=\"genry_choice\"):\n")) &&
              r8.contains(QString::fromUtf8("    menu (screen=\"genry_choice_img\"):\n        \"Туда\" (img=\"un shy pioneer\", kind=\"sprite\"):")) &&
              r8.contains(QString::fromUtf8("        \"Сюда\" (img=\"cg d1_food_normal\", kind=\"bg\"):")),
          "«выбор кнопки» = dark buttons, «выбор визуальный» = 7DL strips");
    const QString r9 = compileText(QString::fromUtf8("@mod_id genry_t\nмузыка everlasting_summer\n@mod_name Т\nприбавить trust 1\n: start\nтекст а\n"), es);
    check(orphanIndent(r9).isEmpty() && r9.contains(QStringLiteral("label genry_t:\n    window auto\n    $ _genry_music_file")),
          "commands above the first scene open the mod (old: left outside any label -> the whole game failed to start) " + orphanIndent(r9));
    {
        // the dialogue box the ES way: gone before a new background, back with the next line (old: an empty box
        // over every fade to black); a title is a card, not a line in the box; faces melt instead of snapping
        const QString rw = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nтитр Глава 1\nфон ext_square_day fade\n"
                                                         "показать sl smile pioneer right\nСлавя: а\nпоказать sl shy pioneer right\n"
                                                         "показать dv grin pioneer left none\nтелефонначать Мику\nсмс Мику: б\nтелефонконец\n"
                                                         "окноскрыть\nокнопоказать dissolve\nпереход two\n: two\nтекст в\n"), es);
        const QString body = rw.mid(rw.indexOf(QStringLiteral("label genry_t:")));
        check(body.contains(QStringLiteral("label genry_t:\n    window auto\n")) && body.contains(QStringLiteral("label genry_t__two:\n    window auto\n")) &&
                  !body.contains(QStringLiteral("    window show\n")) && !body.contains(QStringLiteral("    window hide\n")) &&
                  body.contains(QStringLiteral("    $ _window_hide()\n")) && body.contains(QStringLiteral("    $ _window_show(dissolve)\n")),
              "V1 «window auto»: no empty dialogue box over a fade; the author's own окноскрыть/окнопоказать stay as written");
        check(body.contains(QString::fromUtf8("Text(u\"Глава 1\"")) && !body.contains(QStringLiteral("{size=38}")),
              "титр = a title card on black (old: the title printed inside the dialogue box)");
        check(body.contains(QStringLiteral("    show sl smile pioneer at right, genry_sprite_time_tint with dissolve\n")) &&
                  body.contains(QStringLiteral("    show sl shy pioneer at right, genry_sprite_time_tint with dspr\n")) &&
                  body.contains(QStringLiteral("    show dv grin pioneer at left, genry_sprite_time_tint\n")),
              "показать: a heroine dissolves in, a new face melts (dspr), «none» = instant");
    }
    {
        // «скачок» = the eyes are the transition: shut, the words over the lids, the place changes unseen,
        // they open before the first line; no fade to black anywhere (old: dissolve to black, text, dissolve)
        const QString rs = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nфон ext_square_day\nСлавя: а\nскачок Прошла пара часов\n"
                                                         "время ночь\nфон ext_square_night fade\nпоказать sl smile pioneer center\nСлавя: б\n"), es);
        const QString body = rs.mid(rs.indexOf(QStringLiteral("label genry_t:")));
        const int shut = int(body.indexOf(QStringLiteral("    show blink onlayer overlay")));
        const int words = int(body.indexOf(QString::fromUtf8("Text(u\"Прошла пара часов\"")));
        const int place = int(body.indexOf(QStringLiteral("    scene bg ext_square_night\n")));
        const int open = int(body.indexOf(QStringLiteral("    show unblink onlayer overlay")));
        const int line = int(body.indexOf(QString::fromUtf8("sl \"б\"")));
        check(shut > 0 && words > shut && place > words && open > place && line > open &&
                  body.contains(QStringLiteral("as genry_timeskip onlayer overlay zorder 10:")) &&
                  body.contains(QStringLiteral("    show sl smile pioneer at center, genry_sprite_time_tint\n")) &&
                  !body.contains(QStringLiteral("scene black")),
              "скачок: eyes shut -> «Прошла пара часов» over the lids -> new place under them -> eyes open; no darkness");
    }
    {
        // the heroines take the picture's time (old: a night Alisa on the day beach after a night scene elsewhere);
        // «время ночь» + a day picture -> the game's night version of the place; a night picture without «время» stays
        const QString rt = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nфон ext_beach_day\nАлиса: а\nпереход two\n"
                                                         ": two\nвремя ночь\nфон ext_beach_day\nАлиса: б\nфон ext_square_day\nтекст в\n"
                                                         "фон ext_square_night\nтекст г\n"), es);
        const QString one = rt.mid(rt.indexOf(QStringLiteral("label genry_t:")));
        const QString two = rt.mid(rt.indexOf(QStringLiteral("label genry_t__two:")));
        check(one.contains(QStringLiteral("    $ persistent.sprite_time = \"day\"\n    $ day_time()\n    scene bg ext_beach_day\n")) &&
                  two.contains(QStringLiteral("    scene bg ext_beach_night\n")) && !two.contains(QStringLiteral("scene bg ext_beach_day")) &&
                  two.contains(QStringLiteral("    scene bg ext_square_night\n")) && !two.contains(QStringLiteral("scene bg ext_square_day")),
              "фон sets the heroines' time; «время ночь» turns a day picture into its night version");
        const QString rn = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nфон ext_square_day\nтекст а\nфон ext_square_night\nтекст б\n"), es);
        check(rn.contains(QStringLiteral("    scene bg ext_square_night\n")) && rn.contains(QStringLiteral("    $ night_time()\n    scene bg ext_square_night")),
              "a night picture without «время» stays night and turns the heroines night");
    }
    // every V1 feature at once: meters, timed choice, «запомнит», inventory, gallery, achievements, the mod's menu
    QFile ft(QStringLiteral(GB_SOURCE_DIR "/work/featuretest/story.txt"));
    if (ft.open(QIODevice::ReadOnly)) {
        const QString r10 = compileText(QString::fromUtf8(ft.readAll()), es);
        check(orphanIndent(r10).isEmpty() && r10.contains(QStringLiteral("menu (screen=\"genry_choice_timed\", seconds=6.0):")) &&
                  r10.contains(QStringLiteral("\"...\" (timeout=True):\n            jump genry_v1_features__silence")) &&
                  r10.contains(QString::fromUtf8("$ genry_meter_ping(u\"Алиса\", 2, \"#ff7a00\", \"genry_v1_features_simpatiya_alisy\", 0, 10)")) &&
                  r10.contains(QStringLiteral("default genry_v1_features__inv = []")) && r10.contains(QStringLiteral("screen genry_v1_features__main_menu():")) &&
                  r10.contains(QStringLiteral("label genry_v1_features__start:")) && r10.contains(QStringLiteral("define genry_v1_features__cgs = [\"cg d1_food_normal\"]")),
              "V1 features compile into sound Ren'Py " + orphanIndent(r10));
    }
    const QString r11 = compileText(QString::fromUtf8("@mod_id genry_t\n@mod_name Лето\n@mod_title_font es:fonts/PressStart2P.ttf\n"
                                                      "@mod_title_color #b3001b\n@mod_title_size 44\n@mod_title_style b\n: start\nтекст а\n"),
                                    es);
    check(r11.contains(QString::fromUtf8("$ mods[\"genry_t\"] = u\"{size=44}{color=#b3001b}{font=fonts/PressStart2P.ttf}{b}Лето{/b}{/font}{/color}{/size}\"")),
          "mod title in the ES mods list: a game font (not copied), colour, size, bold");
    for (const QString* r : {&rpy, &r2, &r3, &r4, &r5, &r6, &r7, &r8})
        if (!orphanIndent(*r).isEmpty()) check(false, "orphan indented line: " + orphanIndent(*r));
}

// the game the tests read: GENRYBL_ES, else the one Steam has on this PC
static QString esRootForTests()
{
    const QString env = qEnvironmentVariable("GENRYBL_ES");
    return env.isEmpty() ? build::detectEsRoot() : env;
}

static void testRender()
{
    out << "[5] ES assets + renderer\n";
    static EsAssets es;
    QString err;
    check(es.load(QStringLiteral(GB_SOURCE_DIR "/data/es_catalog.json"), esRootForTests(), &err), "ES archive + catalog " + err);
    out << "    sprites=" << es.sprites.size() << " bg=" << es.backgrounds().size() << " cg=" << es.cgs().size()
        << " music=" << es.music.size() << " vfs files=" << es.vfs().archiveFiles().size() << "\n";
    Renderer r;
    r.setAssets(&es, QStringLiteral(GB_SOURCE_DIR "/data"));
    check(!r.sprite(QStringLiteral("dv smile pioneer")).isNull(), "dv smile pioneer composes from 3 layers");
    {
        // румянец / пот / слёзы / мокрая: drawn on the face the emotion layer marks, compiled as a Fixed() over the sprite
        const QImage ov = makeOverlay(es, QStringLiteral("dv smile pioneer"), {QStringLiteral("blush"), QStringLiteral("tears")});
        int cheek = 0, outside = 0;
        for (int y = 330; y < 420; ++y)
            for (int x = 380; x < 540; ++x) if (qAlpha(ov.pixel(x, y)) > 20) ++cheek;
        for (int y = 0; y < 150; ++y)
            for (int x = 0; x < 900; ++x) if (qAlpha(ov.pixel(x, y)) > 0) ++outside;
        CompileOptions v1;
        const QString rpy = compileText(QString::fromUtf8("@mod_id genry_ovt\n@mod_name o\n\n: start\nфон ext_beach_day\n"
                                                          "показать dv smile pioneer румянец пот left\nАлиса: Жарко.\nконецигры\n"), v1);
        check(ov.size() == QSize(900, 1080) && cheek > 600 && outside == 0 &&
                  rpy.contains(QStringLiteral("show dv smile pioneer genry_ov_blush_sweat at left")) &&
                  rpy.contains(QStringLiteral("    image dv smile pioneer genry_ov_blush_sweat = Fixed(\"dv smile pioneer\", "
                                              "\"mods/genry_ovt/images/genry_ov/dv_smile_pioneer__blush_sweat.png\", fit_first=True)")) &&
                  !r.sprite(QStringLiteral("dv smile pioneer genry_ov_blush_sweat")).isNull(),
              QStringLiteral("overlays: blush+tears on the cheeks (%1 px), nothing above the head (%2), Fixed() in the mod").arg(cheek).arg(outside));
    }
    check(!r.background(QStringLiteral("bg ext_camp_entrance_day")).isNull(), "bg ext_camp_entrance_day");
    // Steam Workshop, resolved like ES: the 18+ patch archive beats game/archive.rpa
    const QString patch = QLatin1String(EsAssets::kHentaiPatchId);
    if (es.hentaiPatch()) {
        check(es.images.contains(QStringLiteral("cg d6_dv_hentai")) && es.imageSource(QStringLiteral("cg d6_dv_hentai")) == patch &&
                  !es.missingImages.contains(QStringLiteral("cg d2_sl_swim")) && !r.background(QStringLiteral("cg d2_sl_swim")).isNull(),
              "18+ patch installed: its 20 CGs are there and previewable");
        check(es.vfs().sourceOf(QStringLiteral("images/sprites/normal/dv/dv_1_body.png")) == patch &&
                  es.imageSource(QStringLiteral("dv smile pioneer")) == patch,
              "18+ patch swaps the bodies like in game (workshop archive first)");
    } else {
        check(es.missingImages.contains(QStringLiteral("cg d6_dv_hentai")), "no 18+ patch: its CGs are not offered");
    }
    check(es.vfs().sourceOf(QStringLiteral("images/bg/ext_camp_entrance_day.jpg")).isEmpty(), "untouched files still come from the game");
    // GenryBL's own copy of the 18+ patch (data/patch, tools/bundle_patch.py): all of it but Ульяна and Электроник
    {
        RpaArchive own;
        QString oerr;
        const bool opened = own.open(QStringLiteral(GB_SOURCE_DIR "/data/patch/i8_data.rpa"), &oerr);
        check(opened && own.contains(QStringLiteral("images/cg/d6_dv_hentai.jpg")) && own.contains(QStringLiteral("images/sprites/normal/dv/dv_1_body.png")) &&
                  !own.contains(QStringLiteral("images/sprites/normal/us/us_1_body.png")) && !own.contains(QStringLiteral("images/cg/d5_dv_us_wash.jpg")) &&
                  !own.contains(QStringLiteral("images/sprites/normal/el/el_1_body.png")) &&
                  own.read(QStringLiteral("images/cg/d6_dv_hentai.jpg")).startsWith("\xff\xd8"),
              "the 18+ patch inside GenryBL: its CGs and the heroines' bodies, no Ульяна / Электроник " + oerr);
        // a PC with no subscription at all: the same game folder, no workshop next to it -> GenryBL's copy stands in
        const QString fake = QDir::tempPath() + QStringLiteral("/genrybl_nows/steamapps/common/Everlasting Summer");
        QDir().mkpath(fake);
        if (!QFileInfo::exists(fake + QStringLiteral("/game")))
            QProcess::execute(QStringLiteral("cmd"), {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"), QDir::toNativeSeparators(fake + QStringLiteral("/game")),
                                                      QDir::toNativeSeparators(es.vfs().gameDir())});
        Vfs bare;
        QString merr;
        const bool mounted = bare.mount(fake, &merr);
        check(mounted && bare.workshopItems().isEmpty() && bare.bundledMounted() &&
                  bare.sourceOf(QStringLiteral("images/cg/d6_dv_hentai.jpg")) == patch && !bare.read(QStringLiteral("images/cg/d6_dv_hentai.jpg")).isEmpty() &&
                  bare.read(QStringLiteral("images/sprites/normal/dv/dv_1_body.png")) != bare.readBase(QStringLiteral("images/sprites/normal/dv/dv_1_body.png")) &&
                  bare.sourceOf(QStringLiteral("images/sprites/normal/us/us_1_body.png")).isEmpty(),
              "no subscriptions: GenryBL's own patch copy is mounted (its CGs, the heroines' bodies; Ульяна's stay Steam's) " + merr);
        CompileOptions po;
        const QString prpy = compileText(QString::fromUtf8("@mod_id genry_pt\n@mod_name p\n\n: start\nцг d6_dv_hentai\nконецигры\n"), po);
        check(prpy.contains(QStringLiteral("\"mods/genry_pt/images/genry_patch/d6_dv_hentai.jpg\"")) &&
                  prpy.contains(QStringLiteral("elif renpy.loadable(_genry_own):")),
              "a mod showing a CG of the patch carries it itself (players need no patch)");
    }
    check(es.missingAudio.contains(QStringLiteral("doubt_everyone")) && !es.music.contains(QStringLiteral("doubt_everyone")) &&
              es.music.contains(QStringLiteral("everlasting_summer")),
          "music ids whose .ogg the Steam build lacks are not offered");
    QDir().mkpath(GB_SOURCE_DIR "/tests/out");
    struct Shot { const char* file; const char* story; };
    const Shot shots[] = {
        {"say_day.png", "фон ext_camp_entrance_day\nпоказать dv smile pioneer left\nпоказать sl smile pioneer right\nАлиса: Ну что, пионер, заблудился?"},
        {"say_night.png", "время ночь\nфон ext_square_night\nпоказать un shy pioneer center\nЛена: Я... просто гуляла."},
        {"choice.png", "фон int_dining_hall_day\nпоказать us laugh sport center\nвыбор\n- Съесть котлету -> a\n- Отдать Ульяне -> b\nконецвыбора"},
        {"phone.png", "фон int_house_of_mt_day\nтелефонначать Славя\nсмс Славя: Ты где?\nсмс я: Иду уже\nсмс Славя: Ждём у библиотеки!"},
        {"effects.png", "фон ext_road_night\nпоказать mi smile pioneer center\nпогода снег\nпомехи 0.5\nфильтр холод\nМику: Так красиво..."},
        {"timeskip.png", "фон ext_road_day\nскачок 3 дня"},
        {"nvl.png", "время вечер\nфон ext_square_sunset\nnvlначать\nАлиса: Эй, ты спишь?\nСемён: Уже нет.\nтекст За окном шумели сосны.\nЛена: Можно войти?"},
        {"player.png", "фон int_music_club_mattresses_day\nмузыка everlasting_summer\nатмосфера ambience_camp_center_day\nмузплеер Лето | audio/summer.ogg | Гроза | audio/storm.ogg | Утро | audio/morning.ogg"},
        {"hud_moment.png", "фон ext_beach_day\nмузыка everlasting_summer\nатмосфера ambience_camp_center_day\nпоказать dv angry pioneer center\nтряска 0.6 сильно"},
        {"v1_timed.png", "фон ext_square_day\nпоказать dv angry pioneer center\nвыбор на время 6\n- Помочь -> a\n- Отказаться -> b\nвремя вышло -> c\nконецвыбора"},
        {"v1_meters.png", "шкала симпатия_алисы | Алиса | #ff7a00 | 0 | 10\nшкала симпатия_слави | Славя | #f5d142 | 0 | 10\nфон ext_square_day\n"
                          "шкалы кнопка\nинвентарь кнопка\nприбавить симпатия_алисы 3\nприбавить симпатия_слави 1\nшкалы"},
        {"v1_ping.png", "шкала симпатия_алисы | Алиса | #ff7a00 | 0 | 10\nфон ext_square_day\nпоказать dv smile pioneer center\nзапомнит Алиса\n"
                        "прибавить симпатия_алисы 2"},
        {"v1_menu.png", "менюмода\nзаголовок Лето с последствиями\nфон ext_camp_entrance_day\nкнопка Начать -> start\nкнопка Галерея\nкнопка Достижения\n"
                        "кнопка Выход\nконецменюмода"},
        {"v1_inventory.png", "фон int_house_of_mt_day\nпредмет ключ | Ключ от склада\nпредмет фонарик | Фонарик\nинвентарь"},
        {"choice_es.png","фон ext_square_day\nпоказать us laugh sport center\nвыбор\n- Съесть котлету -> a\n- Отдать Ульяне -> b\nконецвыбора"},
        {"choice_img.png", "фон ext_square_day\nвыбор картинки\n- Пойти со Славей -> a | sl smile pioneer\n- Остаться с Алисой -> b | dv grin pioneer\n"
                           "- На пляж -> c | ext_beach_day\nконецвыбора"},
    };
    for (const Shot& s : shots) {
        const QString text = QString::fromUtf8(s.story);
        const QImage img = r.render(sceneAt(text, -1, &es));
        const QString path = QStringLiteral(GB_SOURCE_DIR "/tests/out/") + QString::fromLatin1(s.file);
        check(!img.isNull() && img.save(path), QStringLiteral("render %1").arg(path));
    }
    check(!r.faceThumb(QStringLiteral("dv smile pioneer"), 160).isNull() &&
              r.faceThumb(QStringLiteral("dv smile pioneer"), 160).save(GB_SOURCE_DIR "/tests/out/face_dv.png"), "face thumbnail");
}

// [5] command forms: every form (each variant) builds a line that compiles without a
//     «needs …» / unknown-command comment and reads back into the same form and values
static void testForms()
{
    out << "\n[5] command forms\n";
    Forms forms;
    QString err;
    check(forms.load(QStringLiteral(GB_SOURCE_DIR "/data/forms.json"), &err), "forms.json loads " + err);
    const CompileOptions v1;
    int n = 0, badCompile = 0, badRead = 0;
    for (const QVariant& fv : forms.forms()) {
        const QVariantMap form = fv.toMap();
        const QString id = form.value(QStringLiteral("id")).toString();
        const QString by = form.value(QStringLiteral("by")).toString();
        QStringList variants{QString()};
        if (!by.isEmpty()) {
            variants.clear();
            for (const QVariant& f : form.value(QStringLiteral("fields")).toList())
                if (f.toMap().value(QStringLiteral("k")).toString() == by)
                    for (const QVariant& o : f.toMap().value(QStringLiteral("opts")).toList()) variants << o.toList().value(0).toString();
        }
        const bool block = form.value(QStringLiteral("block")).toBool() || form.contains(QStringLiteral("special"));
        for (const QString& v : variants) {
            QVariantMap preset;
            if (!by.isEmpty()) preset.insert(by, v);
            const QString line = forms.build(id, forms.defaults(id, preset));
            ++n;
            const bool menu = id == QLatin1String("modmenu");
            const QString story = QStringLiteral("@mod_id genry_formtest\n@mod_name t\n\n") +
                                  (menu ? line + QStringLiteral("\n\n: start\n") : QStringLiteral(": start\n")) +
                                  QString::fromUtf8("фон ext_square_day\n") + (menu ? QString() : line) + QString::fromUtf8("\nконецигры\n");
            const QString rpy = compileText(story, v1);
            static const QRegularExpression badComment(QStringLiteral("^    # (?!-)(.*(needs|Unknown command|неизвестн|нужн).*)$"),
                                                       QRegularExpression::MultilineOption);
            const QRegularExpressionMatch bm = badComment.match(rpy);
            if (rpy.isEmpty() || bm.hasMatch() || !orphanIndent(rpy).isEmpty()) {
                ++badCompile;
                out << "  compile " << id << "/" << v << ": " << line << "  ->  " << (rpy.isEmpty() ? QStringLiteral("(empty)") : bm.captured(1) + orphanIndent(rpy)) << "\n";
            }
            if (block) continue;
            const QVariantMap p = forms.parse(line);
            const QString back = p.isEmpty() ? QString() : forms.build(p.value(QStringLiteral("id")).toString(), p.value(QStringLiteral("values")).toMap());
            if (back != line) {
                ++badRead;
                out << "  read back " << id << "/" << v << ": " << line << "  ->  " << p.value(QStringLiteral("id")).toString() << ": " << back << "\n";
            }
        }
    }
    check(n > 100 && badCompile == 0, QStringLiteral("%1 form lines compile cleanly").arg(n - badCompile));
    check(badRead == 0, QStringLiteral("form lines read back into their forms (%1 bad)").arg(badRead));
    // hand-written lines open in their form with the right values
    auto parsed = [&](const QString& line) { return forms.parse(line); };
    const QVariantMap a = parsed(QString::fromUtf8("показать  dv smile pioneer   left"));
    check(a.value(QStringLiteral("id")) == QLatin1String("show") && a.value(QStringLiteral("values")).toMap().value(QStringLiteral("sprite")) == QLatin1String("dv smile pioneer") &&
              a.value(QStringLiteral("values")).toMap().value(QStringLiteral("pos")) == QLatin1String("left") &&
              a.value(QStringLiteral("values")).toMap().value(QStringLiteral("fx")).toString().isEmpty(),
          "«показать dv smile pioneer left» -> Персонаж: sprite, pos, no transition");
    const QVariantMap b = parsed(QString::fromUtf8("запомнит Алиса | флаг=помог|где=справа | тихо"));
    const QVariantMap bv = b.value(QStringLiteral("values")).toMap();
    check(b.value(QStringLiteral("id")) == QLatin1String("remember") && bv.value(QStringLiteral("text")).toString().isEmpty() &&
              bv.value(QStringLiteral("flag")) == QString::fromUtf8("помог") && bv.value(QStringLiteral("where")) == QString::fromUtf8("справа") &&
              bv.value(QStringLiteral("quiet")).toBool(),
          "«запомнит Алиса | флаг=помог | где=справа | тихо» -> flag, where, quiet (not the text)");
    const QVariantMap c = parsed(QString::fromUtf8("Алиса: Ну и чего ты встал?"));
    check(c.value(QStringLiteral("id")) == QLatin1String("say") && c.value(QStringLiteral("values")).toMap().value(QStringLiteral("who")) == QString::fromUtf8("Алиса"),
          "«Алиса: …» -> Реплика");
    const QVariantMap d = parsed(QString::fromUtf8("карта square: square @sl, beach: beach"));
    check(d.value(QStringLiteral("id")) == QLatin1String("map") && d.value(QStringLiteral("values")).toMap().value(QStringLiteral("zones")).toList().size() == 2,
          "«карта …» -> two places");
    check(parsed(QString::fromUtf8("смс Славя: Ты где?")).value(QStringLiteral("id")) == QLatin1String("phone"), "«смс Славя: …» is the phone, not a line of Славя");
    const QString wr = compileText(QString::fromUtf8("@mod_id genry_w\n@mod_name w\n\n: start\nфон ext_square_day\nпогода снег сильно dissolve\n"
                                                     "погода дождь\nпогода стоп\nконецигры\n"), v1);
    check(wr.contains(QStringLiteral("    show genry_w__weather_snow_3 as genry_weather_snow with dissolve\n")) &&
              wr.contains(QStringLiteral("    show genry_w__weather_rain_2 as genry_weather_rain\n")) &&
              wr.contains(QStringLiteral("image genry_w__weather_snow_3 = Fixed(Solid(\"#dfe8f41a\"), SnowBlossom(Transform(\"mods/genry_w/images/genry_fx/snow.png\"")) &&
              wr.contains(QStringLiteral("rotate=-9)")) && wr.contains(QStringLiteral("    hide genry_weather_snow\n")) && orphanIndent(wr).isEmpty(),
          "V1 weather: layered particles from the mod's genry_fx pictures, «сильно» = more of them, «стоп» hides by tag");
    const QString px = compileText(QString::fromUtf8("@mod_id genry_px\n@mod_name p\n\n: start\nфон ext_square_day\nпараллакс 0.5\nтабло Первый день\n"
                                                     "параллакс выкл\nконецигры\n"), v1);
    check(px.contains(QStringLiteral("    $ genry_parallax = 0.5\n")) && px.contains(QStringLiteral("    $ genry_parallax = 0\n")) &&
              px.contains(QStringLiteral("config.underlay.append(GenryParallax())")) &&
              px.contains(QString::fromUtf8("DynamicDisplayable(genry_flap_frame, text=u\"ПЕРВЫЙ ДЕНЬ\") as genry_flap:")) &&
              px.contains(QStringLiteral("def genry_flap_frame(")) && orphanIndent(px).isEmpty(),
          "V1 «параллакс» (3D режим) and «табло» (Саманта): commands + their blocks only when used");
    const QString ph = compileText(QString::fromUtf8("@mod_id genry_ph\n@mod_name p\n\n: start\nфон ext_square_day\nтелефон Славя\nсмс Славя: Привет\n"
                                                     "фото Славя: cg d1_food_normal | Смотри | 18+\nголосовое я: audio/v1.ogg | 0:07\n"
                                                     "звонок Алиса | принять -> talk | сбросить -> alone | ждать=8 | лицо=dv smile pioneer\n"
                                                     "пост Славя: Утро на пляже! | bg ext_beach_day | лайки=12\nлента\nтелефонконец\nконецигры\n\n"
                                                     ": talk\nконецигры\n\n: alone\nконецигры\n"), v1);
    check(ph.contains(QStringLiteral("show screen genry_phone2(_genry_phone, _genry_phone_contact)")) &&
              ph.contains(QString::fromUtf8("$ _genry_phone.append((\"them_photo18\", u\"Славя\", u\"cg d1_food_normal\", u\"14:27\", u\"Смотри\"))")) &&
              ph.contains(QStringLiteral("$ _genry_phone.append((\"me_voice\", u\"")) && ph.contains(QStringLiteral("mods/genry_ph/audio/v1.ogg")) &&
              ph.contains(QString::fromUtf8("call screen genry_phone_call(u\"Алиса\", face=Transform(\"dv smile pioneer\", crop=(300, 40, 300, 300), zoom=0.69), secs=8)")) &&
              ph.contains(QStringLiteral("        jump genry_ph__talk\n")) && ph.contains(QStringLiteral("        jump genry_ph__alone\n")) &&
              ph.contains(QString::fromUtf8("$ genry_ph__feed.insert(0, {\"who\": u\"Славя\", \"text\": u\"Утро на пляже!\", \"img\": u\"bg ext_beach_day\", \"likes\": 12, \"liked\": False})")) &&
              ph.contains(QStringLiteral("default genry_ph__feed = []")) && ph.contains(QStringLiteral("screen genry_phone_feed(")) &&
              ph.contains(QStringLiteral("class GenryFit(")) && orphanIndent(ph).isEmpty(),
          "V1 Телефон 2.0: photo (18+), voice, a call into branches, the feed with likes");
    const QString ph3 = compileText(QString::fromUtf8("@mod_id genry_ph3\n@mod_name p\n\n: start\nфон ext_square_day\nпуш Алиса: Жду у сцены\n"
                                                      "телефон Славя\nфото Славя: bg ext_beach_day | Забудь | 1раз\nвыбор телефон\n- Бегу! -> run\n- (молчать) -> run\n"
                                                      "конецвыбора\n\n: run\nпост Славя: Пляж | bg ext_beach_day\nкоммент Алиса: Опять без меня?\n"
                                                      "телефон дом\nтелефонконец\nконецигры\n"), v1);
    check(ph3.contains(QString::fromUtf8("show screen genry_ph_banner(u\"Алиса\", u\"Жду у сцены\")")) &&
              ph3.contains(QStringLiteral("(\"them_photo1\", ")) && ph3.contains(QStringLiteral("    menu (screen=\"genry_phone_reply\"):\n")) &&
              ph3.contains(QString::fromUtf8("$ genry_ph3__feed[0][\"comments\"] = genry_ph3__feed[0].get(\"comments\", []) + [(u\"Алиса\", u\"Опять без меня?\")]")) &&
              ph3.contains(QStringLiteral("call screen genry_phone_home(genry_ph3__feed)")) &&
              ph3.contains(QStringLiteral("image genry_ph body = \"mods/genry_ph3/images/genry_phone/body.png\"")) &&
              ph3.contains(QString::fromUtf8("u\"алиса\": \"genry_chibi dv\"")) && ph3.contains(QStringLiteral("screen genry_phone_home(")) &&
              !ph3.contains(QStringLiteral("genry_ph_photos + [(u\"bg ext_beach_day\", u\"Забудь\")]")) && orphanIndent(ph3).isEmpty(),
          "V1 Телефон 3.0: push banner, one-time photo, replies inside the phone, feed comments, home screen, generated pictures + avatars");
    CompileState mst;
    const QString mp = compileLine(QString::fromUtf8("карта square: square @sl, beach: beach"), QStringLiteral("genry_m"), mst, v1).join(QLatin1Char('\n'));
    check(mp.contains(QStringLiteral("(\"square\", \"genry_m__square\", \"mods/genry_m/images/genry_chibi/sl.png\", u\"")) &&
              mp.contains(QStringLiteral("(\"beach\", \"genry_m__beach\", None, u\"")) &&
              mp.contains(QStringLiteral("    call screen genry_camp_map(_genry_map)\n    jump expression _return[1]")) &&
              !mp.contains(QStringLiteral("set_zone")),
          "V1 map: our own map screen with the mod's own faces (old: ES's map - dead from «Моды», no icons in the Steam build)");
    {
        // «обход» = ES day 2's walk-around list; Russian place names and heroine names
        CompileState tst;
        const QString tp = compileLine(QString::fromUtf8("карта обход площадь: a @Славя, Пляж: b @dv, готово: c"), QStringLiteral("genry_m"), tst, v1)
                               .join(QLatin1Char('\n'));
        check(tp.contains(QStringLiteral("(\"square\", \"genry_m__a\", \"mods/genry_m/images/genry_chibi/sl.png\"")) &&
                  tp.contains(QStringLiteral("(\"beach\", \"genry_m__b\", \"mods/genry_m/images/genry_chibi/dv.png\"")) &&
                  tp.contains(QStringLiteral("genry_m__map_seen.get(\"square,beach\", [])")) &&
                  tp.contains(QStringLiteral("    if not _genry_map:\n        jump genry_m__c")) &&
                  tp.contains(QStringLiteral("genry_m__map_seen.setdefault(\"square,beach\", []).append(_return[0])")),
              "карта обход: visited places go out, «готово» when all are visited; «площадь», «@Славя» understood");
        const QString full = compileText(QString::fromUtf8("@mod_id genry_m\n: start\nкарта обход площадь: a, пляж: b, готово: c\n: a\nтекст а\nпереход start\n"
                                                           ": b\nтекст б\nпереход start\n: c\nтекст в\n"), v1);
        check(full.contains(QStringLiteral("screen genry_camp_map(places")) && full.contains(QStringLiteral("default genry_m__map_seen = {}")) &&
                  orphanIndent(full).isEmpty(),
              "the map screen and the visited list ride only in mods that use them");
    }
    CompileState vst;
    const QString vid = compileLine(QString::fromUtf8("видео es:video/opening.ogv"), QStringLiteral("m"), vst, v1).join(QLatin1Char('\n'));
    check(vid.contains(QStringLiteral("movie_cutscene(\"video/opening.ogv\")")), "«видео es:video/…» plays the game's own video " + vid);
}

// [6] «пиши как сценарий»: the expander against the real ES catalog (tables tuned in tools/screenplay/scr_proto.py)
static void testScreenplay()
{
    out << "\n[6] screenplay\n";
    const QVector<QPair<QStringList, QStringList>> cases{
        {{QString::fromUtf8("Алиса (злая, слева): Ну и чего ты встал?")}, {QString::fromUtf8("показать dv angry pioneer left dissolve"), QString::fromUtf8("Алиса: Ну и чего ты встал?")}},
        {{QString::fromUtf8("АЛИСА (смущённо, в купальнике): Не смотри так!")}, {QString::fromUtf8("показать dv shy body center dissolve"), QString::fromUtf8("Алиса: Не смотри так!")}},
        {{QString::fromUtf8("Славя (улыбается, справа): Привет!"), QString::fromUtf8("Славя (уходит)")}, {QString::fromUtf8("показать sl smile pioneer right dissolve"), QString::fromUtf8("Славя: Привет!"), QString::fromUtf8("убрать sl dissolve")}},
        {{QString::fromUtf8("ИНТ. СТОЛОВАЯ — ВЕЧЕР")}, {QString::fromUtf8("время вечер"), QString::fromUtf8("фон int_dining_hall_sunset fade")}},
        {{QString::fromUtf8("НАТ. ПЛЯЖ — ДЕНЬ")}, {QString::fromUtf8("время день"), QString::fromUtf8("фон ext_beach_day fade")}},
        {{QString::fromUtf8("НАТ. ЛОДОЧНАЯ СТАНЦИЯ — ВЕЧЕР")}, {QString::fromUtf8("время вечер"), QString::fromUtf8("фон ext_boathouse_day fade")}},
        {{QString::fromUtf8("ИНТ. ДОМИК ВОЖАТОЙ — НОЧЬ")}, {QString::fromUtf8("время ночь"), QString::fromUtf8("фон int_house_of_mt_night fade")}},
        {{QString::fromUtf8("СЦЕНА 3. НАТ. ПЛОЩАДЬ, НОЧЬ")}, {QString::fromUtf8("время ночь"), QString::fromUtf8("фон ext_square_night fade")}},
        {{QString::fromUtf8("время ночь"), QString::fromUtf8("НАТ. ПЛЯЖ — НОЧЬ")}, {QString::fromUtf8("время ночь"), QString::fromUtf8("фон ext_beach_night fade")}},
        {{QString::fromUtf8("смс Славя: Ты где?")}, {QString::fromUtf8("смс Славя: Ты где?")}},
        {{QString::fromUtf8("Алиса: Просто реплика (без скобок у имени).")}, {QString::fromUtf8("Алиса: Просто реплика (без скобок у имени).")}},
        {{QString::fromUtf8("Лена (плачет): Уйди...")}, {QString::fromUtf8("показать un cry pioneer center dissolve"), QString::fromUtf8("Лена: Уйди...")}},
        {{QString::fromUtf8("Лена (улыбается сквозь слёзы): Спасибо.")}, {QString::fromUtf8("показать un cry_smile pioneer center dissolve"), QString::fromUtf8("Лена: Спасибо.")}},
        {{QString::fromUtf8("Ульяна (удивлённо): Чего?!")}, {QString::fromUtf8("показать us surp1 pioneer center dissolve"), QString::fromUtf8("Ульяна: Чего?!")}},
        {{QString::fromUtf8("Юля (злая): Фр-р.")}, {QString::fromUtf8("показать uv rage center dissolve"), QString::fromUtf8("Юля: Фр-р.")}},
        {{QString::fromUtf8("Ольга Дмитриевна (строго, в панаме): Семён!")}, {QString::fromUtf8("показать mt normal panama pioneer center dissolve"), QString::fromUtf8("Ольга Дмитриевна: Семён!")}},
        {{QString::fromUtf8("Ольга Дмитриевна (смеётся, в купальнике): Вода тёплая!")}, {QString::fromUtf8("показать mt smile swim center dissolve"), QString::fromUtf8("Ольга Дмитриевна: Вода тёплая!")}},
        {{QString::fromUtf8("Женя (дуется): Не мешай.")}, {QString::fromUtf8("показать mz bukal glasses pioneer center dissolve"), QString::fromUtf8("Женя: Не мешай.")}},
        {{QString::fromUtf8("Женя (без очков, улыбается): Ой.")}, {QString::fromUtf8("показать mz smile pioneer center dissolve"), QString::fromUtf8("Женя: Ой.")}},
        {{QString::fromUtf8("Славя (в спортивной форме, серьёзно): Разминка!")}, {QString::fromUtf8("показать sl serious sport center dissolve"), QString::fromUtf8("Славя: Разминка!")}},
        {{QString::fromUtf8("Алиса (ухмыляется, близко): Попался.")}, {QString::fromUtf8("показать dv grin pioneer close center dissolve"), QString::fromUtf8("Алиса: Попался.")}},
        {{QString::fromUtf8("Алиса (очень злая): Всё!")}, {QString::fromUtf8("показать dv rage pioneer center dissolve"), QString::fromUtf8("Алиса: Всё!")}},
        {{QString::fromUtf8("Славя (слегка улыбается): Ну...")}, {QString::fromUtf8("показать sl smile2 pioneer center dissolve"), QString::fromUtf8("Славя: Ну...")}},
        {{QString::fromUtf8("Семён (думает): Опять она.")}, {QString::fromUtf8("th: Опять она.")}},
        {{QString::fromUtf8("Алиса (кричит): Стой!")}, {QString::fromUtf8("показать dv normal pioneer center dissolve"), QString::fromUtf8("крик Алиса | hpunch | Стой!")}},
        {{QString::fromUtf8("Алиса (за кадром): Эй!")}, {QString::fromUtf8("Алиса: Эй!")}},
        {{QString::fromUtf8("Алиса (входит слева)")}, {QString::fromUtf8("показать dv normal pioneer left moveinleft")}},
        {{QString::fromUtf8("Алиса (слева): Раз."), QString::fromUtf8("Алиса (уходит направо)")}, {QString::fromUtf8("показать dv normal pioneer left dissolve"), QString::fromUtf8("Алиса: Раз."), QString::fromUtf8("убрать dv moveoutright")}},
        {{QString::fromUtf8("Алиса (злая): Ты!"), QString::fromUtf8("Славя (улыбается): Тише.")}, {QString::fromUtf8("показать dv angry pioneer center dissolve"), QString::fromUtf8("Алиса: Ты!"), QString::fromUtf8("показать dv angry pioneer cleft"), QString::fromUtf8("показать sl smile pioneer cright dissolve"), QString::fromUtf8("Славя: Тише.")}},
        {{QString::fromUtf8("Алиса (злая): Ты!"), QString::fromUtf8("Алиса (краснеет): ...ладно.")}, {QString::fromUtf8("показать dv angry pioneer center dissolve"), QString::fromUtf8("Алиса: Ты!"), QString::fromUtf8("показать dv shy pioneer румянец dspr"), QString::fromUtf8("Алиса: ...ладно.")}},
        {{QString::fromUtf8("Алиса и Ульяна (смеются): Ха-ха!")}, {QString::fromUtf8("показать dv laugh pioneer center dissolve"), QString::fromUtf8("показать dv laugh pioneer cleft"), QString::fromUtf8("показать us laugh pioneer cright dissolve"), QString::fromUtf8("Алиса и Ульяна: Ха-ха!")}},
        {{QString::fromUtf8("Незнакомка (злая): Кто здесь?")}, {QString::fromUtf8("Незнакомка: Кто здесь?")}},
        {{QString::fromUtf8("ВОЖАТЫЙ (строго): Отбой!")}, {QString::fromUtf8("Вожатый: Отбой!")}},
        {{QString::fromUtf8("dv (angry swim close): Ну?")}, {QString::fromUtf8("показать dv angry body close center dissolve"), QString::fromUtf8("Алиса: Ну?")}},
        {{QString::fromUtf8("Славя (вдалеке, машет рукой)")}, {QString::fromUtf8("показать sl normal pioneer far center dissolve")}},
        {{QString::fromUtf8("ЗАТЕМНЕНИЕ.")}, {QString::fromUtf8("фон black fade")}},
        {{QString::fromUtf8("НАТ. ЛЕС — НОЧЬ")}, {QString::fromUtf8("время ночь"), QString::fromUtf8("фон ext_path_night fade")}},
        {{QString::fromUtf8("ИНТ. КУХНЯ — ДЕНЬ")}, {QString::fromUtf8("# ИНТ. КУХНЯ — ДЕНЬ")}},
        {{QString::fromUtf8("Электроник (с фингалом): Я в порядке!")}, {QString::fromUtf8("показать el fingal pioneer center dissolve"), QString::fromUtf8("Электроник: Я в порядке!")}},
        {{QString::fromUtf8("— Привет, — улыбнулась Славя. — Ты новенький?")}, {QString::fromUtf8("показать sl smile pioneer center dissolve"), QString::fromUtf8("Славя: Привет. Ты новенький?")}},
        {{QString::fromUtf8("— Иду, — буркнул я.")}, {QString::fromUtf8("Я: Иду.")}},
        {{QString::fromUtf8("Солнце садилось за лес.")}, {QString::fromUtf8("текст Солнце садилось за лес.")}},
        {{QString::fromUtf8("показать dv smile pioneer left"), QString::fromUtf8("(Алиса уходит налево)")}, {QString::fromUtf8("показать dv smile pioneer left"), QString::fromUtf8("убрать dv moveoutleft")}},
        {{QString::fromUtf8("(Пауза)")}, {QString::fromUtf8("пауза 1")}},
        {{QString::fromUtf8("менюмода"), QString::fromUtf8("Заголовок Мой мод"), QString::fromUtf8("конецменюмода")}, {QString::fromUtf8("менюмода"), QString::fromUtf8("Заголовок Мой мод"), QString::fromUtf8("конецменюмода")}},
        {{QString::fromUtf8("выбор"), QString::fromUtf8("- Пойти -> a"), QString::fromUtf8("конецвыбора"), QString::fromUtf8(": a"), QString::fromUtf8("Алиса (злая): Ну?")}, {QString::fromUtf8("выбор"), QString::fromUtf8("- Пойти -> a"), QString::fromUtf8("конецвыбора"), QString::fromUtf8(": a"), QString::fromUtf8("показать dv angry pioneer center dissolve"), QString::fromUtf8("Алиса: Ну?")}},
        {{QString::fromUtf8("персонаж gg Гоша #ff8800"), QString::fromUtf8("Гоша (злой): Эй!")}, {QString::fromUtf8("персонаж gg Гоша #ff8800"), QString::fromUtf8("Гоша: Эй!")}},
    };
    int bad = 0;
    for (const auto& c : cases) {
        const QStringList got = expandScreenplay(c.first);
        if (got != c.second) {
            ++bad;
            out << "  " << c.first.join(QStringLiteral(" / ")) << "\n     want: " << c.second.join(QStringLiteral(" / ")) << "\n     got : "
                << got.join(QStringLiteral(" / ")) << "\n";
        }
    }
    check(bad == 0, QStringLiteral("screenplay lines: %1 of %2 wrong").arg(bad).arg(cases.size()));

    const QStringList conv = convertToStory(QString::fromUtf8(
        "НАТ. ПЛЯЖ — ДЕНЬ\n\nЖаркий полдень. На песке никого.\n\nАЛИСА\n(ухмыляясь)\nНу что, струсил?\n\nСЕМЁН\nЕщё чего.\n\n"
        "— Тогда догоняй, — засмеялась Алиса.\n— Подожди!\n"));
    const QStringList convWant{QString::fromUtf8("НАТ. ПЛЯЖ — ДЕНЬ"), QString::fromUtf8("Жаркий полдень. На песке никого."),
                               QString::fromUtf8("Алиса (ухмыляясь): Ну что, струсил?"), QString::fromUtf8("Семён: Ещё чего."),
                               QString::fromUtf8("Алиса (засмеялась): Тогда догоняй."), QString::fromUtf8("Семён: Подожди!")};
    check(conv == convWant, QString::fromUtf8("pasted screenplay/prose -> story: ") + conv.join(QStringLiteral(" / ")));

    CompileOptions v1;
    v1.knownSpeakers = {QStringLiteral("dv"), QStringLiteral("sl"), QStringLiteral("me"), QStringLiteral("th")};
    const QString rpy = compileText(QString::fromUtf8("@mod_id genry_scr\n@mod_name s\n\n: start\nНАТ. ПЛЯЖ — ДЕНЬ\n"
                                                      "Алиса (злая, слева): Ну и чего ты встал?\nСолнце жарило нещадно.\n— Иду, — буркнул я.\n"
                                                      "Алиса (уходит направо)\nконецигры\n"), v1);
    check(rpy.contains(QStringLiteral("ext_beach_day")) && rpy.contains(QStringLiteral("show dv angry pioneer at left")) &&
              rpy.contains(QString::fromUtf8("dv \"Ну и чего ты встал?\"")) && rpy.contains(QString::fromUtf8("\"Солнце жарило нещадно.\"")) &&
              rpy.contains(QString::fromUtf8("me \"Иду.\"")) && rpy.contains(QStringLiteral("hide dv with moveoutright")) &&
              !rpy.contains(QStringLiteral("Unknown command")),
          "a play compiles: heading -> bg, sprite with emotion + position, prose -> narration, «— реплика, — буркнул я»\n" + rpy.right(900));
    const SceneState sc = sceneAt(QString::fromUtf8("@mod_id m\n: start\nНАТ. ПЛЯЖ — ДЕНЬ\nАлиса (злая, слева): Ну?\nконецигры\n"), 4);
    check(sc.bg == QStringLiteral("bg ext_beach_day") && sc.sprites.size() == 1 && sc.sprites[0].image == QStringLiteral("dv angry pioneer") &&
              sc.text == QString::fromUtf8("Ну?") && sc.line == 4,
          "the preview reads the play too: " + sc.bg + " / " + (sc.sprites.isEmpty() ? QString() : sc.sprites[0].image) + " / " + sc.text);
}

static void testWardrobe()
{
    out << "[7] workshop wardrobe\n";
    static EsAssets es;
    QString err;
    if (!es.load(QStringLiteral(GB_SOURCE_DIR "/data/es_catalog.json"), esRootForTests(), &err)) {
        check(false, "ES for the wardrobe " + err);
        return;
    }
    const QString ws = Vfs::workshopDirFor(es.esRoot());
    if (ws.isEmpty() || !QDir(ws + QStringLiteral("/2519236508")).exists()) {
        out << "    (no modders' resource pack 2519236508 in the workshop - skipped)\n";
        return;
    }
    static Library lib;
    lib.scan(ws);
    static Wardrobe wr;
    wr.build(lib, es, QDir::tempPath() + QStringLiteral("/genrybl_selftest_boxes.tsv"));
    setWardrobe(&wr);
    out << "    workshop layers=" << wr.partCount() << " decoded=" << wr.decoded() << "\n";
    WardrobeLook look;
    check(wr.resolve(QStringLiteral("dv smile casual"), &look) && look.pose == 4 && look.layers.size() == 3 &&
              look.kinds == QVector<int>{Wardrobe::Body, Wardrobe::Clothes, Wardrobe::Face} && look.layers[1].src == QLatin1String("2519236508"),
          "dv smile casual = body + the resource pack's casual + face, at ES's pose of «smile»");
    check(!wr.resolve(QStringLiteral("dv smile pioneer")), "ES's own sprites stay the game's");
    QString why;
    check(!wr.resolve(QStringLiteral("dv casual"), nullptr, &why) && why.contains(QString::fromUtf8("эмоция")), "no face -> refused, with the reason");
    check(!wr.resolve(QStringLiteral("mi smile towel"), nullptr, &why) && why.contains(QStringLiteral("close")) &&
              wr.resolve(QStringLiteral("mi smile towel close")),
          "an outfit drawn only up close asks for «close»");
    bool young = false;
    for (const char* t : {"us", "el", "sh"})
        for (const Wardrobe::Outfit& o : wr.outfits(QString::fromLatin1(t))) young = young || o.adult;
    check(!young && !wr.resolve(QStringLiteral("us smile bra")) && !wr.resolve(QStringLiteral("us smile bdsm")) &&
              wr.resolve(QStringLiteral("dv smile nude")) && wr.resolve(QStringLiteral("sl smile topless")),
          "18+ parts: the grown-up heroines and Юля (none for Ульяна, the boys, unknown mod characters)");
    check(!Wardrobe::isAdultPart(QStringLiteral("sundress")) && !Wardrobe::isAdultPart(QStringLiteral("swim")) &&
              Wardrobe::isAdultPart(QStringLiteral("undressed")) && Wardrobe::isAdultPart(QStringLiteral("shorts_bra")),
          "18+ word list (sundress / swim are not)");
    CompileOptions v1;
    const QString story = QString::fromUtf8("@mod_id genry_wrt\n@mod_name w\n\n: start\nфон ext_beach_day\nпоказать dv smile casual left\n"
                                            "показать dv grin casual румянец right\nАлиса: Ну как?\nконецигры\n");
    const QString rpy = compileText(story, v1);
    const QString casualBody = QStringLiteral("\"mods/genry_wrt/images/genry_wardrobe/2519236508/normal/dv/dv_4_body.png\"");
    check(rpy.contains(QStringLiteral("    image dv smile casual = ConditionSwitch(\"persistent.sprite_time=='sunset'\", im.MatrixColor(im.Composite((900,1080), "
                                      "(0,0), ") + casualBody) &&
              rpy.contains(QStringLiteral("show dv smile casual at left")) && rpy.contains(QStringLiteral("    image dv grin casual = ConditionSwitch(")) &&
              rpy.contains(QStringLiteral("image dv grin casual genry_ov_blush = Fixed(\"dv grin casual\"")) &&
              !wr.readModFile(QStringLiteral("2519236508/normal/dv/dv_4_casual.png")).isEmpty(),
          "compiled like ES's own sprites (tint switch), layers under mods/<id>/images/genry_wardrobe, overlays ride on it");
    LintContext ctx;
    ctx.es = &es;
    ctx.opt = v1;
    int bad = 0;
    for (const LintIssue& i : lintStory(story, ctx))
        if (i.level != LintIssue::Info) { ++bad; out << "    lint: " << i.msg << "\n"; }
    check(bad == 0, "lint knows the wardrobe sprites");
    Renderer r;
    r.setAssets(&es, QStringLiteral(GB_SOURCE_DIR "/data"));
    const QImage day = r.sprite(QStringLiteral("dv smile casual")), night = r.sprite(QStringLiteral("dv smile casual"), QStringLiteral("night"));
    check(day.size() == QSize(900, 1080) && !night.isNull() && day.pixel(450, 700) != night.pixel(450, 700) &&
              !r.sprite(QStringLiteral("dv grin casual genry_ov_blush")).isNull(),
          "preview draws it, darker at night, with an overlay");
    // «удалить навсегда» and «поправить лицо»: the user's own lists
    wr.setHidden({QStringLiteral("dv|outfit:casual")});
    QString hiddenWhy;
    bool outfitGone = true;
    for (const Wardrobe::Outfit& o : wr.outfits(QStringLiteral("dv"))) outfitGone = outfitGone && o.part != QLatin1String("casual");
    check(!wr.resolve(QStringLiteral("dv smile casual"), nullptr, &hiddenWhy) && outfitGone && wr.resolve(QStringLiteral("dv smile sport")),
          "a deleted outfit is gone from the lists and from resolve, the rest stays");
    wr.setHidden({QStringLiteral("dv|look:smile sport"), QStringLiteral("dv|face:grin")});
    check(!wr.resolve(QStringLiteral("dv smile sport")) && wr.resolve(QStringLiteral("dv smile casual")) && !wr.resolve(QStringLiteral("dv grin casual")),
          "a deleted sprite / emotion is gone, the outfit itself stays");
    wr.setHidden({});
    wr.setFaceShifts({{QStringLiteral("dv|normal|outfit:casual"), QPoint(7, -3)}});
    WardrobeLook moved;
    QString scope;
    const QPoint zero;
    check(wr.resolve(QStringLiteral("dv smile casual"), &moved) && moved.faceShift == QPoint(7, -3) &&
              wr.definition(QStringLiteral("dv smile casual"), QStringLiteral("m")).contains(QStringLiteral("(7,-3), \"mods/m/images/genry_wardrobe/2519236508/normal/dv/dv_4_smile.png\"")) &&
              wr.faceShift(QStringLiteral("dv"), QStringLiteral("close"), QStringLiteral("smile"), QStringLiteral("casual"), &scope).isNull() &&
              wr.compose(QStringLiteral("dv smile casual")) != wr.compose(QStringLiteral("dv smile casual"), &zero),
          "a face moved by hand: the mod's Composite puts the face layer there, the preview too, only at that distance");
    wr.setFaceShifts({});
    // Ульяна: the Steam build's own body, copied into the mod - never a workshop body, never the 18+ patch's
    WardrobeLook us;
    const bool usLook = wr.resolve(QStringLiteral("us smile paintpioneer"), &us) && !us.layers.isEmpty() && us.kinds[0] == Wardrobe::Body &&
                        us.layers[0].src == QLatin1String("base") && Wardrobe::modFile(us.layers[0]).startsWith(QLatin1String("base/")) &&
                        !wr.readModFile(Wardrobe::modFile(us.layers[0])).isEmpty();
    const bool patched = usLook && es.vfs().sourceOf(us.layers[0].path) == QLatin1String(EsAssets::kHentaiPatchId);
    bool usBodies = false;
    for (const Wardrobe::Outfit& o : wr.outfits(QStringLiteral("us"))) usBodies = usBodies || o.body;
    check(usLook && (!patched || wr.read(us.layers[0]) != es.vfs().read(us.layers[0].path)) && !usBodies &&
              !wr.resolve(QStringLiteral("us smile pibody")) && !wr.resolve(QStringLiteral("us smile body")),
          QStringLiteral("Ульяна wears the Steam body (not the pack's, not the patch's%1), no body outfits").arg(patched ? QString() : QStringLiteral(" - no patch here")));
    WardrobeLook fig;
    check(wr.wornAlone(QStringLiteral("un"), QStringLiteral("boy")) && wr.looks(QStringLiteral("un"), QStringLiteral("boy")) == QStringList{QStringLiteral("boy")} &&
              wr.resolve(QStringLiteral("un boy"), &fig) && fig.layers.size() == 1 && !wr.resolve(QStringLiteral("un smile boy")),
          "a whole second figure (Лена «boy») is worn alone: no body under it, no emotion over it");
    bool pieces = false;
    for (const QString& n : wr.looks(QStringLiteral("un"), QStringLiteral("alt_pioneer"))) pieces = pieces || n.contains(QLatin1String("_new "));
    check(wr.kind(QStringLiteral("un"), QStringLiteral("ai_blink")) == Wardrobe::Face && !pieces,
          "7ДЛ's «ai_*» heads are faces; the «*_new» face pieces are no emotions (the sprite would have no face)");
    WardrobeLook bare;
    check(wr.resolve(QStringLiteral("mi smile body"), &bare) &&
              (!es.hentaiPatch() || (bare.layers.value(0).src == QLatin1String("patch") && !wr.readModFile(Wardrobe::modFile(bare.layers.value(0))).isEmpty())),
          "the bare body of a grown-up heroine: the 18+ patch's, carried by the mod (players need no patch)");
    setWardrobe(nullptr);
}

static void testCinema()
{
    out << "[8] cinema\n";
    static EsAssets es;
    QString err;
    if (!es.load(QStringLiteral(GB_SOURCE_DIR "/data/es_catalog.json"), esRootForTests(), &err)) {
        check(false, "ES for the cinema " + err);
        return;
    }
    const QString story = QString::fromUtf8(
        "@mod_id genry_cin\n@mod_name c\n\n"
        ": start\n"                                   // 4
        "музыка everlasting_summer\n"                 // 5
        "фон ext_camp_entrance_day\n"                 // 6
        "Славя: Привет!\n"                            // 7
        "выбор\n"                                     // 8
        "- Со Славей -> sl\n"
        "- Одному -> alone\n"
        "- В никуда -> nowhere\n"
        "конецвыбора\n"                               // 12
        ": sl\n"                                      // 13
        "прибавить любовь 2\n"
        "вызвать flash\n"                             // 15
        "Славя: Пошли.\n"                             // 16
        "если любовь >= 2 and not ссора -> good\n"    // 17
        "конецигры\n"
        ": flash\n"                                   // 19
        "текст Вспышка памяти.\n"                     // 20
        "конецсцены\n"
        ": alone\n"                                   // 22
        "выбор на время 5\n"                          // 23
        "- Ждать -> sl\n"
        "время вышло -> late\n"
        "конецвыбора\n"
        ": late\n"                                    // 27
        "звонок Славя | принять -> sl | сбросить -> good | нет ответа -> late2\n"   // 28
        ": late2\n"
        "текст Никто не пришёл.\n"                    // 30
        "переход good\n"
        ": good\n"                                    // 32
        "фон ext_square_day\n"
        "Славя: Хорошо, что ты тут.\n"                // 34
        ": good_next\n"
        "Славя: И это тоже.\n"                        // 36
        ": other\n"
        "Славя: Сюда не попадём.\n");
    auto walk = [&](const QList<int>& picks, int from = 0) {
        Cinema c;
        c.load(story, &es);
        QStringList trace;
        QList<int> p = picks;
        CinemaStop st = c.start(from);
        for (int n = 0; n < 60; ++n) {
            trace << QString::number(st.line) + QLatin1Char(':') + QString::number(int(st.kind));
            if (st.kind == CinemaStop::End) break;
            st = c.next(st.kind == CinemaStop::Choice ? (p.isEmpty() ? 0 : p.takeFirst()) : -1);
        }
        return qMakePair(trace.join(QLatin1Char(' ')), st);
    };
    // 0 Say, 1 Choice, 2 Note, 6 End
    const auto a = walk({0});
    check(a.first == QStringLiteral("7:0 8:1 20:0 16:0 34:0 36:0 36:6") && a.second.music.contains(QStringLiteral("everlasting_summer")),
          "choice -> scene, «вызвать» comes back, «если любовь >= 2» on the points, *_next goes on, the scene after ends the mod: " + a.first);
    const auto b = walk({1, -1, -1});
    check(b.first == QStringLiteral("7:0 8:1 23:1 28:1 30:0 34:0 36:0 36:6"),
          "timed choice: time runs out -> «время вышло»; the call not answered -> «нет ответа», then «переход»: " + b.first);
    const auto c = walk({2});
    check(c.first == QStringLiteral("7:0 8:1 12:6") && c.second.note.contains(QString::fromUtf8("nowhere")), "a jump to a missing scene ends with the reason");
    const auto d = walk({}, 16);
    check(d.first.startsWith(QStringLiteral("16:0 ")), "from the cursor line: the story goes on right there: " + d.first);
    Cinema e;
    e.load(story, &es);
    const CinemaStop first = e.start(0);
    check(first.scene.bg == QStringLiteral("bg ext_camp_entrance_day") && first.scene.speakerName == QString::fromUtf8("Славя"),
          "the frame of a stop is the game's picture of that route");
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    Vfs::setBundled(QStringLiteral(GB_SOURCE_DIR "/data/patch/i8_data.rpa"), QLatin1String(EsAssets::kHentaiPatchId));
    testTables();
    testLines();
    testStories();
    testFixes();
    testRender();
    testForms();
    testScreenplay();
    testWardrobe();
    testCinema();
    out << "\nRESULT: " << g_ok << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
