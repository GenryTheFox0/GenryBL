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
#include "History.h"
#include "Graph.h"
#include "Timeline.h"
#include "Fuzz.h"
#include "Crash.h"
#include "Discord.h"
#include "ModHub.h"
#include "Forms.h"
#include "Library.h"
#include "Lint.h"
#include "Overlays.h"
#include "Renderer.h"
#include "Scene.h"
#include "Weather.h"
#include "Screenplay.h"
#include "Py.h"
#include "Text.h"
#include "Wardrobe.h"

#include <QGuiApplication>
#include <QDir>
#include <functional>
#include <QtEndian>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QLocalServer>
#include <QDateTime>
#include <QThread>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
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
                                     "\"mods/genry_h/genry/patch/d6_dv_hentai.jpg\"), (\"uvao_h\", \"images/cg/uvao_h.jpg\", "
                                     "\"mods/genry_h/genry/patch/uvao_h.jpg\"), (\"card_12_uvao_old\", \"images/cards/12_uvao_old.png\", "
                                     "\"mods/genry_h/genry/patch/12_uvao_old.png\")]:\n"
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
    check(r8.contains(QStringLiteral("    menu (screen=\"genry_choice_btn\"):\n")) && r8.contains(QStringLiteral("screen genry_choice_btn(items):")) &&
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
        const int shut = int(body.indexOf(QStringLiteral("    show blink zorder 90")));
        const int words = int(body.indexOf(QString::fromUtf8("Text(u\"Прошла пара часов\"")));
        const int place = int(body.indexOf(QStringLiteral("    scene bg ext_square_night\n")));
        const int open = int(body.indexOf(QStringLiteral("    show unblink zorder 90")));
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
    {
        // the lids as ES shows them (master layer, over the heroines, UNDER the dialogue box): a line said with the
        // eyes shut is seen; a «фон» under shut eyes puts the still lids back; «моргнуть» = the game's own blinking
        const QString rl = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nфон ext_square_day\nзакрытьглаза\nтекст Темнота.\n"
                                                         "фон ext_beach_day\nоткрытьглаза\nтекст Пляж.\nморгнуть\nтекст всё\n"), es);
        const QString body = rl.mid(rl.indexOf(QStringLiteral("label genry_t:")));
        check(body.contains(QStringLiteral("    show blink zorder 90\n    $ renpy.pause(1.5, hard=True)\n")) && !body.contains(QStringLiteral("onlayer overlay")) &&
                  body.contains(QStringLiteral("    scene bg ext_beach_day\n    show genry_lids zorder 90\n")) &&
                  body.contains(QStringLiteral("    hide blink\n    hide genry_lids\n")) && body.contains(QStringLiteral("image genry_lids = ")) &&
                  body.contains(QStringLiteral("    show blinking zorder 90\n    $ renpy.pause(3.5, hard=True)\n    hide blinking\n")),
              "eyes on the master layer like ES (a line with shut eyes is seen), still lids after a «фон», «моргнуть» = ES blinking");
    }
    {
        // «менюмода» in a mod's own words: «Дни» = chapters, «Фотографии» = gallery, «Выселиться» = exit (old: all of them
        // started the mod); «автор нет» hides the author line, «автор Имя» renames it
        const QString rm = compileText(QString::fromUtf8("@mod_id genry_t\n@mod_name Общага\n@author Вахтёр\n\nменюмода\nавтор нет\n"
                                                         "кнопка Начать запись -> start\nкнопка Дни\nкнопка Фотографии\nкнопка Настройки\n"
                                                         "кнопка Выселиться\nконецменюмода\n\n: start\nтекст а\n"), es);
        check(rm.contains(QStringLiteral("Show(\"genry_chapters\"")) && rm.contains(QStringLiteral("Show(\"genry_gallery\"")) &&
                  rm.contains(QStringLiteral("ShowMenu(\"preferences\")")) && rm.contains(QStringLiteral("Return(\"__exit\")")) &&
                  rm.count(QStringLiteral("action Return(\"genry_t__start\")")) == 1 && !rm.contains(QString::fromUtf8("автор: ")),
              "менюмода: Дни/Фотографии/Выселиться understood, «автор нет» hides the author");
        const QString ra = compileText(QString::fromUtf8("@mod_id genry_t\n@author Вахтёр\nменюмода\nавтор Студия «Общага»\nкнопка Начать\n"
                                                         "конецменюмода\n: start\nтекст а\n"), es);
        check(ra.contains(QString::fromUtf8("автор: Студия «Общага»")) && !ra.contains(QString::fromUtf8("автор: Вахтёр")),
              "менюмода: «автор Имя» renames the author line");
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
                                              "\"mods/genry_ovt/genry/overlays/dv_smile_pioneer__blush_sweat.png\", fit_first=True)")) &&
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
        check(prpy.contains(QStringLiteral("\"mods/genry_pt/genry/patch/d6_dv_hentai.jpg\"")) &&
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
    {
        // «Текст на экране»: a line break the writer made stays one (old: the two lines were glued with a space)
        const QString line = forms.build(QStringLiteral("note"), {{QStringLiteral("how"), QStringLiteral("note")},
                                                                   {QStringLiteral("text"), QString::fromUtf8("Так я и нашёл то, что искал!\nМод был сделан за пару минут")}});
        CompileState nst;
        const QString rpy = compileLine(line, QStringLiteral("genry_t"), nst, CompileOptions{}).join(QLatin1Char('\n'));
        check(line == QString::fromUtf8("заметка Так я и нашёл то, что искал!\\nМод был сделан за пару минут") &&
                  rpy.contains(QString::fromUtf8("искал!\\nМод")),
              "Текст на экране keeps the writer's line break: " + line);
    }
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
    {
        // «Новая сцена» with its start: only what was picked goes in; a transition without a background adds nothing
        const QString full = forms.build(QStringLiteral("label"), {{QStringLiteral("name"), QStringLiteral("beach")}, {QStringLiteral("t"), QString::fromUtf8("вечер")},
                                                                   {QStringLiteral("bg"), QStringLiteral("ext_beach_sunset")}, {QStringLiteral("music"), QStringLiteral("sunny_day")}});
        const QString bare = forms.build(QStringLiteral("label"), {{QStringLiteral("name"), QStringLiteral("beach")}, {QStringLiteral("fx"), QStringLiteral("dissolve")}});
        const QVariantMap back = forms.parse(QStringLiteral(": beach"));
        check(full == QString::fromUtf8(": beach\nвремя вечер\nфон ext_beach_sunset fade\nмузыка sunny_day") && bare == QStringLiteral(": beach") &&
                  back.value(QStringLiteral("id")) == QLatin1String("label") && back.value(QStringLiteral("values")).toMap().value(QStringLiteral("name")) == QLatin1String("beach"),
              "«Новая сцена»: время / фон / музыка at the start only when picked, «: сцена» still opens in the form\n" + full + "\n" + bare);
    }
    {
        // «имяигрока»: the player types the hero's name - Семён's lines and «[имя]» carry it; [Слово] stays text
        const QString rpy = compileText(QString::fromUtf8("@mod_id genry_t\n@mod_name t\n\n: start\nимяигрока Как зовут? | Вася\n"
                                                          "текст [имя] проснулся. На двери было написано [Алиса].\n[имя]: Где я?\nЯ: Ну и дела.\n[имя] открыл глаза.\nконецигры\n"), v1);
        check(rpy.contains(QString::fromUtf8("call screen genry_ask_name(u\"Как зовут?\", persistent.genry_t__hero or u\"Вася\", \"genry_t__hero\")")) &&
                  rpy.contains(QStringLiteral("label genry_t:\n    $ genry_hero_apply(genry_t__hero, genry_t__hero_she)")) &&
                  rpy.contains(QStringLiteral("default genry_t__hero = (persistent.genry_t__hero or None)")) &&
                  rpy.contains(QStringLiteral("    def genry_hero_apply(name, she=False):")) && rpy.contains(QStringLiteral("add get_image(\"gui/o_rly/base.png\")")) &&
                  rpy.contains(QStringLiteral("screen genry_ask_name(ask_text, start_name, hero_var, shown=False):")) &&
                  rpy.contains(QString::fromUtf8("\"[me_name] проснулся. На двери было написано [[Алиса].\"")) &&
                  rpy.contains(QString::fromUtf8("me \"Где я?\"")) && rpy.contains(QString::fromUtf8("me \"Ну и дела.\"")) && rpy.contains(QString::fromUtf8("\"[me_name] открыл глаза.\"")) && !rpy.contains(QStringLiteral("genry_sp_")),
              "«имяигрока»: the input screen, me_name + names['me'], «[имя]» -> [me_name], «[имя]:» = Семён, [Алиса] stays text\n" + rpy.right(1400));
        const QString plain = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nтекст просто\nконецигры\n"), v1);
        check(!plain.contains(QStringLiteral("genry_ask_name")) && !plain.contains(QStringLiteral("genry_hero_apply")), "no hero name anywhere - nothing of it in the mod");
        // the author's hero (@hero_name, «Название мода» → Герой) and the player's own one from the mod menu's «Имя»
        const QString rh = compileText(QString::fromUtf8("@mod_id genry_t\n@hero_name Вася\nменюмода\nкнопка Начать\nкнопка Имя: [имя]\nкнопка Выход\n"
                                                         "конецменюмода\n: start\nЯ: Привет.\nконецигры\n"), v1);
        check(rh.contains(QString::fromUtf8("default genry_t__hero = (persistent.genry_t__hero or u\"Вася\")")) &&
                  rh.contains(QStringLiteral("label genry_t__start:\n    $ genry_hero_apply(genry_t__hero, genry_t__hero_she)")) &&
                  rh.contains(QString::fromUtf8("Show(\"genry_ask_name\", ask_text=u\"Как тебя зовут?\", start_name=(genry_t__hero or me_name), hero_var=\"genry_t__hero\", shown=True)")) &&
                  rh.contains(QString::fromUtf8("u\"Имя: [me_name]\"")),
              "@hero_name = the hero from the start; the menu's «Имя: [имя]» opens the name plate and shows the name\n" + rh.right(1600));
        // chosen in the app: «Игрок вводит имя» - в меню мода (the button comes by itself) / в начале (the plate first)
        const QString rm = compileText(QString::fromUtf8("@mod_id genry_t\n@hero_ask menu\nменюмода\nкнопка Начать\nкнопка Выход\nконецменюмода\n"
                                                         ": start\nЯ: Привет.\nконецигры\n"), v1);
        const int nameAt = int(rm.indexOf(QString::fromUtf8("u\"Имя: [me_name]\""))), exitAt = int(rm.indexOf(QString::fromUtf8("u\"Выход\"")));
        check(nameAt > 0 && exitAt > nameAt && rm.count(QStringLiteral("Show(\"genry_ask_name\"")) == 1,
              "@hero_ask menu: «Имя: …» appears on the mod menu by itself, before «Выход»\n" + rm.right(1500));
        const QString rs = compileText(QString::fromUtf8("@mod_id genry_t\n@hero_ask start\n: start\nЯ: Привет.\nконецигры\n"), v1);
        check(rs.contains(QString::fromUtf8("label genry_t:\n    $ genry_hero_apply(genry_t__hero, genry_t__hero_she)\n    window auto\n    window auto hide\n"
                                            "    call screen genry_ask_name(u\"Как тебя зовут?\", persistent.genry_t__hero or me_name, \"genry_t__hero\")\n")) &&
                  rs.contains(QStringLiteral("def genry_hero_apply(name, she=False):")),
              "@hero_ask start: the name plate before the first line\n" + rs.right(1500));
        // «время вечер» mid-scene: the day square becomes the evening one behind the heroine (not a day picture under «вечер»)
        const QString rt = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nфон ext_square_day\nпоказать sl smile pioneer\nСлавя: Привет.\n"
                                                         "время вечер\nСлавя: Уже вечер.\nконецигры\n"), v1);
        const SceneState stt = sceneAt(QString::fromUtf8("@mod_id m\n: start\nфон ext_square_day\nпоказать sl smile pioneer\nвремя вечер\nСлавя: Уже вечер.\nконецигры\n"), 6);
        check(rt.contains(QStringLiteral("    $ sunset_time()\n    show bg ext_square_sunset\n    with dissolve\n")) &&
                  stt.bg == QStringLiteral("bg ext_square_sunset") && stt.sprites.size() == 1,
              "«время вечер» after a day «фон»: the evening square melts in, the heroine stays: " + stt.bg + "\n" + rt.right(700));
        {
            // the project's own files: a broken picture = the game dies, «лже-буквы», a giant sprite, a photo background
            const QString dir = QDir::tempPath() + QStringLiteral("/gb_assets_test");
            QDir(dir).removeRecursively();
            QDir().mkpath(dir + QStringLiteral("/images"));
            QDir().mkpath(dir + QStringLiteral("/audio"));
            auto put = [&](const QString& rel, const QByteArray& data) {
                QFile f(dir + QLatin1Char('/') + rel);
                f.open(QIODevice::WriteOnly);
                f.write(data);
            };
            put(QStringLiteral("images/bg broken.png"), QByteArray("not a picture at all"));
            QImage(3000, 2000, QImage::Format_RGB32).save(dir + QString::fromUtf8("/images/bg фото.jpg"));
            QImage(800, 2600, QImage::Format_ARGB32).save(dir + QStringLiteral("/images/giant smile.png"));
            QImage(10, 10, QImage::Format_RGB32).save(dir + QString::fromUtf8("/images/bg lеs.png"));      // Cyrillic «е» in «les»
            put(QStringLiteral("audio/voice.m4a"), QByteArray("x"));
            const QVector<LintIssue> iss = build::checkAssets(dir);
            auto has = [&](int level, const char* part) {
                for (const LintIssue& i : iss) if (i.level == level && i.msg.contains(QString::fromUtf8(part)) && !i.file.isEmpty()) return true;
                return false;
            };
            check(has(LintIssue::Error, "bg broken.png") && has(LintIssue::Info, "3000×2000") && has(LintIssue::Warning, "800×2600") &&
                      has(LintIssue::Warning, "лже-буквы") && has(LintIssue::Warning, ".m4a"),
                  "checkAssets: broken picture = error, photo bg = fitted, giant sprite, mixed Latin/Cyrillic name, unplayable audio");
            QDir(dir).removeRecursively();
        }
        {
            // the hero's name in cases + «он/она» forms: the mod gets the tags, the preview shows the result
            const QStringList want = QString::fromUtf8("Семёна Семёну Семёна Семёном Семёне|Васи Васе Васю Васей Васе|Саши Саше Сашу Сашей Саше|"
                                                       "Никиты Никите Никиту Никитой Никите|Марии Марии Марию Марией Марии|Андрея Андрею Андрея Андреем Андрее|"
                                                       "Игоря Игорю Игоря Игорем Игоре|Павла Павлу Павла Павлом Павле|Кэт Кэт Кэт Кэт Кэт").split(QLatin1Char('|'));
            const QStringList names = QString::fromUtf8("Семён Вася Саша Никита Мария Андрей Игорь Павел Кэт").split(QLatin1Char(' '));
            QString bad;
            for (int k = 0; k < names.size(); ++k) {
                QStringList got;
                for (int c = 1; c <= 5; ++c) got << declineName(names[k], c, k >= names.size() - 1 || k == 4);
                if (got.join(QLatin1Char(' ')) != want[k]) bad += got.join(QLatin1Char(' ')) + QStringLiteral(" | ");
            }
            check(bad.isEmpty(), "declineName: Семён/Вася/Саша/Никита/Мария/Андрей/Игорь/Павел/Кэт " + bad);
            const QString rg = compileText(QString::fromUtf8("@mod_id genry_t\n@hero_gender она\n: start\nтекст Я [проснулся/проснулась]. Ольга звала [имя кого].\n"
                                                             "Алиса: Эй, [имя кому] привет!\nконецигры\n"), v1);
            check(rg.contains(QString::fromUtf8("\"Я {genry_g=проснулся/проснулась}. Ольга звала {genry_n=3}.\"")) &&
                      rg.contains(QString::fromUtf8("\"Эй, {genry_n=2} привет!\"")) &&
                      rg.contains(QStringLiteral("default genry_t__hero_she = (persistent.genry_t__hero_she if persistent.genry_t__hero_she is not None else True)")) &&
                      rg.contains(QStringLiteral("default genry_t__hero_genders = True")) &&
                      rg.contains(QStringLiteral("config.self_closing_custom_text_tags[\"genry_g\"] = genry_g_tag")) &&
                      rg.contains(QStringLiteral("$ genry_hero_apply(genry_t__hero, genry_t__hero_she)")),
                  "«[проснулся/проснулась]» / «[имя кого]» -> text tags, the hero's gender default + «Парень/Девушка» on the plate\n" + rg.right(900));
            const SceneState sg = sceneAt(QString::fromUtf8("@mod_id m\n@hero_name Катя\n@hero_gender она\n: start\nАлиса: [имя], ты [пришёл/пришла] к [имя кому]?\nконецигры\n"), 5);
            check(sg.text == QString::fromUtf8("Катя, ты пришла к Кате?"), "the preview: gender forms + cases: " + sg.text);
            // a bare «кого»: «искала Катю», «у Кати»
            const SceneState sk2 = sceneAt(QString::fromUtf8("@mod_id m\n@hero_name Катя\n@hero_gender она\n: start\nСлавя: Ольга искала [имя кого], а у [имя кого] всё хорошо?\nконецигры\n"), 5);
            const QString rk = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nСлавя: Ольга искала [имя кого], а у [имя кого] всё хорошо?\nконецигры\n"), v1);
            check(sk2.text == QString::fromUtf8("Ольга искала Катю, а у Кати всё хорошо?") &&
                      rk.contains(QString::fromUtf8("\"Ольга искала {genry_n=3}, а у {genry_n=1} всё хорошо?\"")),
                  "«кого» by the word before it: винительный after a verb, родительный after «у»: " + sk2.text);
            // a mod's own background with no time: the heroines are day ones, not the last game's night
            const QString rd = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nфон my_room\nСлавя: Привет.\nконецигры\n"), v1);
            check(rd.contains(QStringLiteral("$ persistent.sprite_time = \"day\"")), "a timeless background at the start = day");
        }
        {
            // «менюмода <стиль>»: every style is ONE menu (ES's board and 7ДЛ used to get the panel's column on top)
            const QList<QPair<const char*, const char*>> styles{
                {"панель", "genry_menu_btn"}, {"бл", "mainmenu_"}, {"7дл", "genry_waifu_in"}, {"тетрадь", "preferences_bg.jpg"},
                {"дневник", "history_bg.jpg"}, {"доска", "ingame_menu/sunset/ingame_menu.png"}, {"монитор", "anim/backdrop/back.jpg"},
                {"нуар", "SaturationMatrix(0.0)"}, {"живое", "genry_clock.localtime()"}, {"кино", "ypos 940"}, {"карта", "genry_map_pic(\"available\")"}};
            QString bad;
            for (const auto& st : styles) {
                const QString rpy = compileText(QString::fromUtf8("@mod_id genry_t\n@mod_name Лето\nменюмода %1\nфон ext_beach_sunset\n"
                                                                  "герои sl smile dress\nкнопка Начать -> start\nкнопка Галерея\nкнопка Выход\n"
                                                                  "конецменюмода\n: start\nтекст а\nконецигры\n").arg(QString::fromUtf8(st.first)), v1);
                const int screenAt = int(rpy.indexOf(QStringLiteral("screen genry_t__main_menu():")));
                const QString screen = screenAt < 0 ? QString() : rpy.mid(screenAt, rpy.indexOf(QStringLiteral("\n\n"), screenAt) - screenAt);
                const bool es = QByteArray(st.first) == QByteArray("бл");
                const int starts = int(screen.count(QStringLiteral("Return(\"genry_t__start\")")));
                const bool column = screen.contains(QStringLiteral("#0b0f0cd8"));
                const bool ok = screen.contains(QString::fromUtf8(st.second)) && starts == 1 &&
                                (column == (QByteArray(st.first) == QByteArray("панель") || QByteArray(st.first) == QByteArray("живое"))) && rpy.count(QStringLiteral("screen genry_t__main_menu():")) == 1 &&
                                (!QString::fromUtf8(st.first).startsWith(QString::fromUtf8("карт")) || rpy.contains(QStringLiteral("def genry_map_pic(kind):")));
                if (!ok) bad += QString::fromUtf8(st.first) + QStringLiteral(" (starts=%1 column=%2 es=%3) ").arg(starts).arg(column).arg(es);
            }
            check(bad.isEmpty(), "every «менюмода» style is one menu of its own look: " + bad);
            check(menuStyleKey(QString::fromUtf8("Карта лагеря")) == QLatin1String("map") && menuStyleKey(QString::fromUtf8("ЧБ")) == QLatin1String("noir") &&
                      menuStyleKey(QString()) == QLatin1String("panel"),
                  "menuStyleKey: Russian words and the default");
            {
                // splitForBox: whole sentences, never inside a {tag}…{/tag} or [x], no box over the limit
                QString t;
                for (int k = 1; k <= 9; ++k) t += QString::fromUtf8("Предложение %1 {i}с курсивом внутри, который нельзя резать{/i} и [me_name] рядом. ").arg(k);
                const QStringList boxes = splitForBox(t.trimmed(), 250);
                bool ok = boxes.size() >= 3 && boxes.join(QLatin1Char(' ')) == t.trimmed();
                for (const QString& b : boxes) {
                    int vis = 0;
                    for (int i = 0; i < b.size(); ++i) { if (b.at(i) == QLatin1Char('{')) { i = int(b.indexOf(QLatin1Char('}'), i)); continue; } ++vis; }
                    ok = ok && vis <= 250 && b.count(QStringLiteral("{i}")) == b.count(QStringLiteral("{/i}")) && b.endsWith(QLatin1Char('.'));
                }
                check(ok, QStringLiteral("splitForBox: %1 boxes of whole sentences, tags kept whole").arg(boxes.size()));
                const QString rl = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nСлавя: %1\n+ А это дописано.\nконецигры\n").arg(t.trimmed()), v1);
                check(rl.count(QStringLiteral("    sl \"")) == boxes.size() && rl.contains(QString::fromUtf8("    extend \" А это дописано.\"")) && !rl.contains(QStringLiteral("extend u\"")),
                      "a long line = one «sl» line per box; «+ …» = ES extend with its space");
                const QString rm = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nмузыка free_love\nмузыка kostry\nтекст а\nконецигры\n"), v1);
                check(rm.contains(QStringLiteral("play music \"zhenya/sounds/free_love.mp3\"")) && rm.contains(QStringLiteral("play music \"sound/music/kostry.ogg\"")),
                      "the game's tracks outside music_list (es-doc: Free Love, Kostry…) play by their files");
            }
            const QStringList places = menuMapPlaces({QString(), QString::fromUtf8("галерея"), QString::fromUtf8("выход"), QString()});
            check(places == QStringList{QStringLiteral("square"), QStringLiteral("library"), QStringLiteral("camp_entrance"), QStringLiteral("beach")},
                  "«карта»: the start on the square, the gallery in the library, the exit at the gates: " + places.join(QLatin1Char(' ')));
        }
        const SceneState sh = sceneAt(QString::fromUtf8("@mod_id m\n@hero_name Вася\n: start\nЯ: Меня зовут [имя].\nконецигры\n"), 4);
        check(sh.playerName == QString::fromUtf8("Вася") && sh.text == QString::fromUtf8("Меня зовут Вася."), "the preview knows @hero_name: " + sh.text);
        const SceneState sa = sceneAt(QString::fromUtf8("@mod_id m\n: start\nимяигрока | Вася\nЯ: Меня зовут [имя].\nконецигры\n"), 4);
        check(sa.playerName == QString::fromUtf8("Вася") && sa.text == QString::fromUtf8("Меня зовут Вася.") && sa.cardKind.isEmpty(),
              "the preview: after «имяигрока» Семён is the typed name, «[имя]» too: " + sa.speakerName + " / " + sa.text);
        const SceneState sk = sceneAt(QString::fromUtf8("@mod_id m\n: start\nимяигрока | Вася\nконецигры\n"), 3);
        check(sk.cardKind == QLatin1String("askname") && sk.cardText == QString::fromUtf8("Как тебя зовут?") && sk.cardSub == QString::fromUtf8("Вася"),
              "the preview on «имяигрока»: the input panel with the default question and name");
    }
    {
        // no @author = no «автор:» line on the mod menu (it used to say GenryTheFox for everybody)
        const QString rpy = compileText(QString::fromUtf8("@mod_id genry_t\n@mod_name Общага\nменюмода\nкнопка Начать\nконецменюмода\n: start\nтекст а\n"), v1);
        const SceneState sm = sceneAt(QString::fromUtf8("@mod_id genry_t\n@author Вахтёр\nменюмода\nкнопка Начать\nконецменюмода\n: start\nтекст а\n"), 4);
        check(!rpy.contains(QString::fromUtf8("автор: ")) && sm.modMenuAuthor == QString::fromUtf8("Вахтёр"),
              "no @author - no author line; the preview shows the story's own author");
    }
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
              wr.contains(QStringLiteral("image genry_w__weather_snow_3 = Fixed(Solid(\"#dfe8f41a\"), SnowBlossom(Transform(\"mods/genry_w/genry/fx/snow.png\"")) &&
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
              ph3.contains(QStringLiteral("image genry_ph body = \"mods/genry_ph3/genry/phone/body.png\"")) &&
              ph3.contains(QString::fromUtf8("u\"алиса\": \"genry_chibi dv\"")) && ph3.contains(QStringLiteral("screen genry_phone_home(")) &&
              !ph3.contains(QStringLiteral("genry_ph_photos + [(u\"bg ext_beach_day\", u\"Забудь\")]")) && orphanIndent(ph3).isEmpty(),
          "V1 Телефон 3.0: push banner, one-time photo, replies inside the phone, feed comments, home screen, generated pictures + avatars");
    CompileState mst;
    const QString mp = compileLine(QString::fromUtf8("карта square: square @sl, beach: beach"), QStringLiteral("genry_m"), mst, v1).join(QLatin1Char('\n'));
    check(mp.contains(QStringLiteral("(\"square\", \"genry_m__square\", \"mods/genry_m/genry/chibi/sl.png\", u\"")) &&
              mp.contains(QStringLiteral("(\"beach\", \"genry_m__beach\", None, u\"")) &&
              mp.contains(QStringLiteral("    call screen genry_camp_map(_genry_map)\n    jump expression _return[1]")) &&
              !mp.contains(QStringLiteral("set_zone")),
          "V1 map: our own map screen with the mod's own faces (old: ES's map - dead from «Моды», no icons in the Steam build)");
    {
        // «обход» = ES day 2's walk-around list; Russian place names and heroine names
        CompileState tst;
        const QString tp = compileLine(QString::fromUtf8("карта обход площадь: a @Славя, Пляж: b @dv, готово: c"), QStringLiteral("genry_m"), tst, v1)
                               .join(QLatin1Char('\n'));
        check(tp.contains(QStringLiteral("(\"square\", \"genry_m__a\", \"mods/genry_m/genry/chibi/sl.png\"")) &&
                  tp.contains(QStringLiteral("(\"beach\", \"genry_m__b\", \"mods/genry_m/genry/chibi/dv.png\"")) &&
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
    const QString casualBody = QStringLiteral("\"mods/genry_wrt/genry/wardrobe/2519236508/normal/dv/dv_4_body.png\"");
    check(rpy.contains(QStringLiteral("    image dv smile casual = ConditionSwitch(\"persistent.sprite_time=='sunset'\", im.MatrixColor(im.Composite((900,1080), "
                                      "(0,0), ") + casualBody) &&
              rpy.contains(QStringLiteral("show dv smile casual at left")) && rpy.contains(QStringLiteral("    image dv grin casual = ConditionSwitch(")) &&
              rpy.contains(QStringLiteral("image dv grin casual genry_ov_blush = Fixed(\"dv grin casual\"")) &&
              !wr.readModFile(QStringLiteral("2519236508/normal/dv/dv_4_casual.png")).isEmpty(),
          "compiled like ES's own sprites (tint switch), layers under mods/<id>/genry/wardrobe, overlays ride on it");
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
              wr.definition(QStringLiteral("dv smile casual"), QStringLiteral("m")).contains(QStringLiteral("(7,-3), \"mods/m/genry/wardrobe/2519236508/normal/dv/dv_4_smile.png\"")) &&
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
    check(c.first == QStringLiteral("7:0 8:1 11:6") && c.second.note.contains(QString::fromUtf8("nowhere")),
          "a jump to a missing scene ends with the reason, at the option that leads there: " + c.first);
    const auto d = walk({}, 16);
    check(d.first.startsWith(QStringLiteral("16:0 ")), "from the cursor line: the story goes on right there: " + d.first);
    Cinema e;
    e.load(story, &es);
    const CinemaStop first = e.start(0);
    check(first.scene.bg == QStringLiteral("bg ext_camp_entrance_day") && first.scene.speakerName == QString::fromUtf8("Славя"),
          "the frame of a stop is the game's picture of that route");
    {
        // a line longer than the box: the cinema turns its boxes one per click, like the game, then goes on
        QString longLine;
        for (int k = 1; k <= 12; ++k) longLine += QString::fromUtf8("Это предложение номер %1, и оно довольно длинное для проверки. ").arg(k);
        Cinema p;
        p.load(QString::fromUtf8("@mod_id genry_t\n: start\nфон ext_square_day\nСлавя: %1\nСлавя: Всё.\nконецигры\n").arg(longLine.trimmed()), &es);
        CinemaStop s1 = p.start(0);
        QStringList seen{s1.scene.text};
        int pages = s1.scene.textPages;
        CinemaStop s2 = p.next();
        while (s2.scene.textPage > 1 && seen.size() < 10) { seen << s2.scene.text; s2 = p.next(); }
        check(pages >= 3 && seen.size() == pages && s2.scene.text == QString::fromUtf8("Всё.") && seen.join(QLatin1Char(' ')) == longLine.trimmed() &&
                  s1.scene.speakerName == QString::fromUtf8("Славя"),
              QStringLiteral("the cinema turns the boxes of a long line: %1 boxes, then the next line").arg(pages));
    }
}

// [9] Выбор 2.0: the lines under an option are its branch, points / locks / conditions in brackets, «по кругу», «наугад»,
// a choice under an option; the same in the compiler, the preview, the cinema and the lint
static void testChoice2()
{
    out << "[9] choice 2.0\n";
    CompileOptions v1;
    v1.knownSpeakers = {QStringLiteral("sl"), QStringLiteral("dv"), QStringLiteral("me"), QStringLiteral("un")};
    const QString story = QString::fromUtf8(
        "@mod_id genry_t\n"                              // 1
        "шкала славя | Славя | #ff7a00 | 0 | 10\n"       // 2
        ": start\n"                                      // 3
        "фон ext_square_day\n"                           // 4
        "выбор\n"                                        // 5
        "Славя: Куда пойдём?\n"                          // 6
        "- На площадь [+1 Славя] [запомнит Славя]\n"     // 7
        "    Славя: Отлично!\n"                          // 8
        "- Поцеловать [нужно славя 5]\n"                 // 9
        "    Славя: Ой.\n"                               // 10
        "- Секрет [если ключ]\n"                         // 11
        "- На пляж -> beach\n"                           // 12
        "конецвыбора\n"                                  // 13
        "Славя: Идём дальше.\n"                          // 14
        "выбор по кругу\n"                               // 15
        "- Про лагерь\n"                                 // 16
        "    Славя: Это Совёнок.\n"                      // 17
        "- Про тебя [всегда]\n"                          // 18
        "    Славя: Я Славя.\n"                          // 19
        "- Хватит [выход]\n"                             // 20
        "конецвыбора\n"                                  // 21
        "выбор наугад\n"                                 // 22
        "- Дождь\n"                                      // 23
        "- Солнце\n"                                     // 24
        "конецвыбора\n"                                  // 25
        "выбор кнопки\n"                                 // 26
        "- Спросить\n"                                   // 27
        "    выбор\n"                                    // 28
        "    - Да\n"                                     // 29
        "        Славя: Да.\n"                           // 30
        "    - Нет\n"                                    // 31
        "    конецвыбора\n"                              // 32
        "- Молчать\n"                                    // 33
        "конецвыбора\n"                                  // 34
        "Славя: Всё.\n"                                  // 35
        "конецигры\n"                                    // 36
        ": beach\n"                                      // 37
        "фон ext_beach_day\n"                            // 38
        "выбор\n"                                        // 39
        "- Купаться\n"                                   // 40
        "    Славя: Холодно!\n"                          // 41
        "- Уйти -> start\n"                              // 42
        ": after\n"                                      // 43
        "текст Конец.\n");                               // 44
    QString err;
    const QString rpy = compileText(story, v1, &err);
    check(err.isEmpty() && !rpy.isEmpty(), "the whole story compiles: " + err);
    const QString var = v1.legacy ? QString() : QStringLiteral("genry_t_") + slugOf(QString::fromUtf8("славя"), QStringLiteral("value"), v1);
    check(rpy.contains(QStringLiteral("    menu (screen=\"genry_choice_es\"):\n        sl \"")) && rpy.contains(QStringLiteral("screen genry_choice_es(items):")),
          "a locked option: ES's own menu look (genry_choice_es), the question stays on screen as the menu's line");
    check(rpy.contains(QStringLiteral("        \"") + QString::fromUtf8("На площадь") + QStringLiteral("\":\n            $ ") + var + QStringLiteral(" += 1")) &&
              rpy.contains(QStringLiteral("genry_remember(")) && rpy.contains(QString::fromUtf8("            sl \"Отлично!\"")),
          "[+1 Славя] [запомнит Славя]: points, the popup, then the option's own lines - all inside the option");
    check(rpy.contains(QString::fromUtf8("        \"Поцеловать\" (ok=%1 >= 5, hint=u\"нужно: Славя 5\"):").arg(var)),
          "[нужно славя 5]: locked with the meter's own title as the hint");
    const QString key = QStringLiteral("genry_t_") + slugOf(QString::fromUtf8("ключ"), QStringLiteral("value"), v1);
    check(rpy.contains(QString::fromUtf8("        \"Секрет\" if %1:\n            pass").arg(key)) && rpy.contains(QStringLiteral("default ") + key + QStringLiteral(" = 0")),
          "[если ключ]: hidden until true; ключ exists (0) even though nothing sets it - no NameError");
    check(rpy.contains(QString::fromUtf8("        \"На пляж\":\n            jump genry_t__beach")) &&
              rpy.contains(QString::fromUtf8("\n    sl \"Идём дальше.\"")),
          "an option with a scene jumps; the others go on after the choice");
    const QString seen = QStringLiteral("genry_t__seen2"), again = QStringLiteral("genry_t__again2");
    check(rpy.contains(QStringLiteral("    $ %1 = set()\n    $ %2 = True\n    while %2:\n        $ %2 = False\n        menu:\n            set %1\n").arg(seen, again)) &&
              rpy.contains(QStringLiteral("                $ %1.discard(u\"%2\")\n                $ %3 = True").arg(seen, QString::fromUtf8("Про тебя"), again)) &&
              rpy.contains(QString::fromUtf8("            \"Хватит\":\n                pass")),
          "«по кругу»: asked questions go away, [всегда] stays, [выход] ends it");
    check(rpy.contains(QStringLiteral("    menu (screen=\"genry_choice_random\"):")) && rpy.contains(QStringLiteral("screen genry_choice_random(items):")),
          "«наугад»: the game picks");
    check(rpy.contains(QString::fromUtf8("        \"Спросить\":\n            menu:\n                \"Да\":\n                    sl \"Да.\"")),
          "a choice under an option nests inside it");
    const int beach = int(rpy.indexOf(QStringLiteral("label genry_t__beach:"))), after = int(rpy.indexOf(QStringLiteral("label genry_t__after:")));
    check(beach > 0 && after > beach && rpy.mid(beach, after - beach).contains(QStringLiteral("\n    return\n")),
          "a choice not closed before the next scene closes itself; a scene that ends with it still ends (no run into the next label)");
    // lint: a lock nothing opens, a condition nothing sets
    EsAssets es;
    if (!es.load(QStringLiteral(GB_SOURCE_DIR "/data/es_catalog.json"), esRootForTests(), &err)) {
        check(false, "ES for choice 2.0 " + err);
        return;
    }
    LintContext ctx;
    ctx.es = &es;
    ctx.opt = v1;
    bool keyWarn = false, bad = false;
    for (const LintIssue& i : lintStory(story, ctx)) {
        if (i.msg.contains(QString::fromUtf8("«ключ» нигде не задаётся"))) keyWarn = true;
        else if (i.level == LintIssue::Error) { bad = true; out << "    lint: " << i.line << " " << i.msg << "\n"; }
    }
    check(keyWarn && !bad, "lint: the lines under options are fine; «ключ» that nothing sets is named");
    {
        // a mistake in an option's brackets is shown under its own words, once
        const QString line = QString::fromUtf8("- Пойти [нужно Слава много] [нужн Славя 3]");
        const QString st2 = QString::fromUtf8("@mod_id genry_t\n: start\nвыбор\n") + line + QString::fromUtf8("\n- Уйти\nконецвыбора\nконецигры\n");
        int numErr = 0, almost = 0, extra = 0;
        for (const LintIssue& i : lintStory(st2, ctx)) {
            if (i.line != 4) continue;
            if (i.msg.contains(QString::fromUtf8("нужно число")) && i.col == line.indexOf(QString::fromUtf8("много")) && i.len == 5) ++numErr;
            else if (i.msg.contains(QString::fromUtf8("не команда")) && i.col == line.indexOf(QString::fromUtf8("нужн ")) && i.len == 4) ++almost;
            else { ++extra; out << "    lint: " << i.msg << " @" << i.col << "\n"; }
        }
        check(numErr == 1 && almost == 1, QStringLiteral("lint: «нужно Слава много» under «много», «[нужн …]» under «нужн» (%1 %2 %3)").arg(numErr).arg(almost).arg(extra));
    }
    // the preview: the cursor on an option lights it; in its lines - that branch
    const SceneState onItem = sceneAt(story, 9, &es);
    check(onItem.choices.size() == 4 && onItem.choiceHover == 1 && !onItem.choiceHints.value(1).isEmpty() && onItem.text == QString::fromUtf8("Куда пойдём?"),
          "preview: the cursor on «Поцеловать» - the menu with it lit and locked, the question under it");
    const SceneState inBranch = sceneAt(story, 8, &es);
    check(inBranch.choices.isEmpty() && inBranch.text == QString::fromUtf8("Отлично!"), "preview: the cursor in an option's lines - that branch");
    const SceneState past = sceneAt(story, 14, &es);
    check(past.choices.isEmpty() && past.text == QString::fromUtf8("Идём дальше."), "preview: after the choice - no branch leaks into it");
    // the cinema: the branch, then on; «по кругу» comes back without the asked one
    Cinema c;
    c.load(story, &es);
    CinemaStop s = c.start(0);                        // 6: the question with the menu
    const bool locked = s.kind == CinemaStop::Choice && s.options.size() == 3 && !s.optionHints.value(1).isEmpty();   // «Секрет» hidden
    s = c.next(1);                                    // locked: nothing happens
    const bool stays = s.kind == CinemaStop::Choice;
    s = c.next(0);                                    // «На площадь»
    const bool branch = s.kind == CinemaStop::Say && s.scene.text == QString::fromUtf8("Отлично!") && !s.popups.isEmpty();
    s = c.next();
    const bool onward = s.scene.text == QString::fromUtf8("Идём дальше.");
    s = c.next();                                     // «по кругу»
    const int first = int(s.options.size());
    s = c.next(0);                                    // «Про лагерь»
    s = c.next();                                     // back to the menu
    const bool back = s.kind == CinemaStop::Choice && s.options.size() == first - 1 && !s.options.contains(QString::fromUtf8("Про лагерь"));
    s = c.next(int(s.options.indexOf(QString::fromUtf8("Хватит"))));
    const bool out2 = s.kind == CinemaStop::Say || s.kind == CinemaStop::Choice;      // «наугад» picks by itself -> «выбор кнопки»
    check(locked && stays && branch && onward && first == 3 && back && out2 && s.kind == CinemaStop::Choice && s.options.size() == 2,
          QStringLiteral("cinema: locked/hidden options, the branch then on, «по кругу» comes back without the asked one, «наугад» picks itself (%1 %2 %3 %4 %5 %6 %7)")
              .arg(locked).arg(stays).arg(branch).arg(onward).arg(first).arg(back).arg(out2));
    s = c.next(0);                                    // «Спросить» -> the nested choice
    const bool nested = s.kind == CinemaStop::Choice && s.options == QStringList{QString::fromUtf8("Да"), QString::fromUtf8("Нет")};
    s = c.next(0);
    const bool nestedSay = s.scene.text == QString::fromUtf8("Да.");
    s = c.next();
    check(nested && nestedSay && s.scene.text == QString::fromUtf8("Всё."), "cinema: a choice under an option, then out of both");
}

// [10] Достижения 2.0: ES's plate, the list as ES's gallery, hidden / «сначала» / платина, one entry per key
static void testAchievements2()
{
    out << "[10] achievements 2.0\n";
    CompileOptions v1;
    v1.knownSpeakers = {QStringLiteral("sl"), QStringLiteral("me")};
    const QString story = QString::fromUtf8(
        "@mod_id genry_t\n"
        ": start\n"                                                                                   // 2
        "ачивка help | Добрая душа | описание=Помог Славе | раздел=Славя | картинка=sl smile pioneer\n"   // 3
        "ачивка secret | Тайна | скрытое | нужно=help\n"                                             // 4
        "ачивка all | Все достижения | платина\n"                                                     // 5
        "ачивка help | Добрая душа\n"                                                                 // 6
        "достижения\n"                                                                                // 7
        "конецигры\n");
    QString err;
    const QString rpy = compileText(story, v1, &err);
    const QString help = persistentKey(QStringLiteral("genry_t"), QStringLiteral("help"), QStringLiteral("ach"), v1);
    check(err.isEmpty() && rpy.contains(QString::fromUtf8("    $ genry_ach_unlock(genry_t__achievements, \"genry_t__ach_dates\", \"%1\", u\"Добрая душа\")").arg(help)),
          "«ачивка» opens it with ES's plate, the day it came is kept");
    check(rpy.contains(QString::fromUtf8("# «Все достижения» — платина")) && !rpy.contains(QString::fromUtf8("u\"Все достижения\")")),
          "«платина» is never opened by its line");
    check(rpy.count(QStringLiteral("{\"k\": ")) == 3 && rpy.contains(QStringLiteral("\"h\": True, \"n\": [\"%1\"]").arg(help)) &&
              rpy.contains(QStringLiteral("\"ik\": \"sprite\"")) && rpy.contains(QStringLiteral("\"p\": True}")) &&
              rpy.contains(QString::fromUtf8("\"d\": u\"Помог Славе\", \"s\": u\"Славя\"")),
          "one entry per key: description, section, hidden, «сначала», picture, платина");
    check(rpy.contains(QStringLiteral("screen genry_achievements(achs, called=False, dates=None):")) &&
              rpy.contains(QStringLiteral("call screen genry_achievements(genry_t__achievements, called=True, dates=\"genry_t__ach_dates\")")) &&
              rpy.contains(QStringLiteral("at_list=[achievement_trans], layer=\"overlay\"")) && !rpy.contains(QStringLiteral("default genry_ach")),
          "the list as ES's gallery, the plate on ES's achievement_trans, nothing two mods would both `default`");
    const SceneState at = sceneAt(story, 3, nullptr);
    const SceneState all = sceneAt(story, 7, nullptr);            // both opened -> «платина» came by itself
    const QStringList head = story.split(QLatin1Char('\n')).mid(0, 3);
    const SceneState early = sceneAt(head.join(QLatin1Char('\n')) + QString::fromUtf8("\nдостижения\nачивка secret | Тайна | скрытое\nачивка all | Все | платина\n"), 4, nullptr);
    check(at.achievementPlate == QString::fromUtf8("Добрая душа") &&
              all.achievements == QStringList{QString::fromUtf8("Добрая душа|1||sl smile pioneer"), QString::fromUtf8("Тайна|1|h|"), QString::fromUtf8("Все достижения|1||")} &&
              early.achievements.value(1) == QString::fromUtf8("Тайна|0|h|") && early.achievements.value(2) == QString::fromUtf8("Все|0||"),
          "preview: ES's plate slides in; hidden until it comes; «платина» when the rest are there: [" + at.achievementPlate + "] " +
              all.achievements.join(QStringLiteral(" / ")) + " || " + early.achievements.join(QStringLiteral(" / ")));
    LintContext ctx;
    ctx.opt = v1;
    bool unknownNeed = false;
    for (const LintIssue& i : lintStory(story + QString::fromUtf8("ачивка x | X | нужно=nothing\n"), ctx))
        if (i.msg.contains(QString::fromUtf8("«nothing» нигде нет")) && i.col > 0) unknownNeed = true;
    check(unknownNeed, "lint: «нужно=» names an achievement that is nowhere");

    // «менюмода свой»: the menu from its parts
    const QString menu = QString::fromUtf8("@mod_id genry_t\nменюмода свой\nзаголовок Лето\nкнопки справа\nвид таблички\nцвет #8be9fd\n"
                                           "частицы снег\nпоявление снизу\nфон ext_square_night\nгерои sl smile pioneer\nкнопка Начать\nкнопка Выход\n"
                                           "конецменюмода\n: start\nтекст а\nконецигры\n");
    const QString mr = compileText(menu, v1);
    check(mr.contains(QStringLiteral("xanchor 1.0")) && mr.contains(QStringLiteral("hover_background Solid(\"#8be9fd55\")")) &&
              mr.contains(QStringLiteral("genry_menu_rise(")) && mr.contains(QStringLiteral("image genry_t__weather_snow_1 = ")) &&
              mr.contains(QStringLiteral("xalign 0.2 yalign 1.0 at genry_waifu_in")) && !mr.contains(QString::fromUtf8("Unknown command")),
          "«свой» menu: right, plates in its colour, rising in, snow, the heroine on the free side");
    const QString mes = compileText(QString(menu).replace(QString::fromUtf8("вид таблички"), QString::fromUtf8("вид бл")), v1);
    check(mes.contains(QStringLiteral("background Frame(\"images/gui/choice/night/choice_box.png\", 50, 50)")) && mes.contains(QStringLiteral("hover_color \"#3ccfa2\"")),
          "«свой», «вид бл»: ES's choice box and colours of the menu's hour (night)");
    const SceneState ms = sceneAt(menu, 12, nullptr);
    check(ms.modMenuOpen && ms.modMenuStyle == QStringLiteral("custom") && ms.modMenuParts.layout == QStringLiteral("right") &&
              ms.modMenuParts.look == QStringLiteral("plates") && ms.modMenuParts.accent == QStringLiteral("#8be9fd"),
          "preview knows the «свой» parts");
    bool badWord = false;
    for (const LintIssue& i : lintStory(QString(menu).replace(QString::fromUtf8("вид таблички"), QString::fromUtf8("вид блестящий")), ctx))
        if (i.msg.contains(QString::fromUtf8("не знаю — можно: текст, таблички, бл, неон"))) badWord = true;
    check(badWord, "lint: a word of «свой» it does not know says what it knows");

    // «кодовыйзамок» and «фонарик»
    const QString mech = QString::fromUtf8("@mod_id genry_t\n: start\nфон ext_square_night\nфонарик\nФонарик погас.\n"
                                           "кодовыйзамок 1968 | Год, когда открыли лагерь? | верно -> safe | неверно -> lost | попыток=3\n"
                                           "фонарик выкл\n: safe\nтекст Открыто.\nконецигры\n: lost\nтекст Не вышло.\nконецигры\n");
    const QString mk = compileText(mech, v1);
    check(mk.contains(QString::fromUtf8("    call screen genry_codelock(u\"1968\", u\"Год, когда открыли лагерь?\", 3)\n    if _return:\n        jump genry_t__safe\n    jump genry_t__lost")) &&
              mk.contains(QStringLiteral("screen genry_codelock(code, hint=u\"\", tries=0):")),
          "«кодовыйзамок»: ES's o_rly plate, the right / wrong code go to their scenes");
    check(mk.contains(QStringLiteral("    show screen genry_flashlight(\"mods/genry_t/genry/fx/flashlight.png\", \"#050810\", 1.5)")) &&
              mk.contains(QStringLiteral("    hide screen genry_flashlight")) && mk.contains(QString::fromUtf8("\"Фонарик погас.\"")),
          "«фонарик» on / off; «Фонарик погас.» stays a sentence");
    const SceneState dark = sceneAt(mech, 4, nullptr), lock = sceneAt(mech, 6, nullptr);
    check(dark.flashlight && lock.codeLock == QString::fromUtf8("Год, когда открыли лагерь?|4|3"), "preview: the dark with the light, the code plate");
    bool notDigits = false;
    for (const LintIssue& i : lintStory(QString(mech).replace(QStringLiteral("1968"), QStringLiteral("19a8")), ctx))
        if (i.msg.contains(QString::fromUtf8("только цифры")) && i.len == 4) notDigits = true;
    check(notDigits, "lint: a code the digits cannot type");
}

static void testHistory()
{
    out << "[11] history of versions\n";
    QTemporaryDir tmp;
    const QString dir = tmp.path() + QStringLiteral("/history");
    const QDateTime t0 = QDateTime::fromString(QStringLiteral("2026-09-30T12:00:00"), Qt::ISODate);
    QString story;
    for (int i = 1; i <= 40; ++i) story += QStringLiteral("line %1\n").arg(i);
    check(history::snapshot(dir, story, QStringLiteral("open"), t0) && !history::snapshot(dir, story, QStringLiteral("open"), t0.addSecs(5)),
          "a snapshot on open; the same text twice is one version");
    // a few minutes of typing: no new version until the gap, then one
    const QString typed = story + QStringLiteral("line 41\n");
    check(history::onSave(dir, story, typed, t0.addSecs(60)) == 0 && history::onSave(dir, story, typed, t0.addSecs(300)) == 1,
          "ordinary work: one version every few minutes");
    // Ctrl+A + a letter: the text BEFORE the cut is kept, right then
    const QString more = typed + QStringLiteral("line 42\n");
    const int quiet = history::onSave(dir, typed, more, t0.addSecs(310));
    const int kept = history::onSave(dir, more, QStringLiteral("x\n"), t0.addSecs(320));
    const QVector<history::Version> v = history::list(dir);
    check(quiet == 0 && kept == 1 && v.size() == 3 && v.first().tag == QStringLiteral("cut") && history::read(dir, v.first().file) == more,
          "a big cut keeps the text as it was before it");
    // the diff: what bringing a version back does
    const QString a = QStringLiteral("a\nb\nc\nd\ne\nf\ng\nh\n"), b = QStringLiteral("a\nb\nX\nd\ne\nf\ng\nh\nY\n");
    const QVector<history::DiffLine> d = history::diff(a, b, 1);
    QString shape;
    for (const history::DiffLine& l : d) shape += QLatin1Char(l.kind) + l.text + QLatin1Char(' ');
    check(shape == QStringLiteral("~ =b -c +X =d ~ =h +Y "), "diff: changed lines, context, folded runs: " + shape);
    check(history::diffCount(a, b) == qMakePair(2, 1) && history::diffCount(a, a) == qMakePair(0, 0), "diff counts");
    // thinning: the newest are all kept, older ones one a day
    for (int i = 0; i < 90; ++i) history::snapshot(dir, story + QString::number(i), QString(), t0.addDays(-(i / 3)).addSecs(-i * 60));
    const QVector<history::Version> after = history::list(dir);
    QSet<QString> days;
    int oldOnes = 0;
    for (int i = 60; i < after.size(); ++i) { days.insert(after[i].when.date().toString()); ++oldOnes; }
    check(after.size() >= 60 && after.size() < 93 && days.size() == oldOnes, "old versions thin out to one a day");
}

// [12] the doctor (fixes the lint offers) and the story map
static void testDoctor()
{
    out << "[12] doctor + story map\n";
    CompileOptions v1;
    v1.knownSpeakers = {QStringLiteral("sl"), QStringLiteral("dv"), QStringLiteral("me")};
    v1.esMusic = {QStringLiteral("sunny_day")};
    v1.esSounds = {QStringLiteral("sfx_door_open")};
    v1.audioKinds = {{QStringLiteral("door.ogg"), QStringLiteral("sfx")}};
    // «сам расставит»: the audio plays as what it is, whatever the command
    check(rightAudioKind(QString::fromUtf8("звук sunny_day"), v1) == QString::fromUtf8("музыка sunny_day") &&
              rightAudioKind(QString::fromUtf8("  музыка sfx_door_open fadein 2"), v1) == QString::fromUtf8("  звук sfx_door_open") &&
              rightAudioKind(QString::fromUtf8("музыкафайл audio/door.ogg"), v1) == QString::fromUtf8("звукфайл audio/door.ogg") &&
              rightAudioKind(QString::fromUtf8("музыка audio/door.ogg"), v1) == QString::fromUtf8("звукфайл audio/door.ogg") &&
              rightAudioKind(QString::fromUtf8("музыка sunny_day"), v1) == QString::fromUtf8("музыка sunny_day"),
          "audio in the right command: music / sound / the project's files by what they were imported as");
    const QString rpy = compileText(QString::fromUtf8("@mod_id genry_t\n: start\nзвук sunny_day\nпоказать\nубрать\nпауза abc\nконецигры\n"), v1);
    check(rpy.contains(QStringLiteral("genry_resolve_music(\"sunny_day\")")) && !rpy.contains(QStringLiteral("\n    show \n")) &&
              !rpy.contains(QStringLiteral("\n    hide \n")) && !rpy.contains(QStringLiteral("\n    show with")) &&
              rpy.contains(QStringLiteral("renpy.pause(1.0, hard=True)")),
          "compiled: the track plays as music, «показать» / «убрать» with nobody and «пауза abc» break nothing");
    const QString story = QString::fromUtf8(
        "@mod_id genry_t\n"
        ": start\n"                      // 2
        "покзать dv smile pioneer\n"     // 3  a slip of a command
        "переход finsh\n"                // 4  a slip of a scene
        ": finish\n"                     // 5
        "звук sunny_day\n"               // 6  music as a sound
        "убрать\n"                       // 7  nobody
        "конецвыбора\n"                  // 8  stray
        "текст всё\n"                    // 9  ends without a way on
        ": lost\n"                       // 10 nobody leads here
        "текст никто\n"
        "конецигры\n"
        ": finish\n"                     // 13 twice
        "переход nowhere_at_all\n");     // 14 no such scene, nothing like it
    LintContext ctx;
    ctx.opt = v1;
    const QVector<LintIssue> is = lintStory(story, ctx);
    auto fixAt = [&](int line) { for (const LintIssue& i : is) if (i.line == line && i.fixMode) return i; return LintIssue{}; };
    check(fixAt(3).fix == QString::fromUtf8("показать dv smile pioneer"), "doctor: a command with a slip -> «" + fixAt(3).fix + "»");
    check(fixAt(4).fix == QString::fromUtf8("переход finish"), "doctor: a scene with a slip -> «" + fixAt(4).fix + "»");
    check(fixAt(6).fix == QString::fromUtf8("музыка sunny_day"), "doctor: music written as a sound");
    check(fixAt(7).fix == QString::fromUtf8("убратьвсех"), "doctor: «убрать» with nobody");
    check(fixAt(8).fixMode == 3, "doctor: a stray «конецвыбора» goes");
    check(fixAt(13).fix == QString::fromUtf8(": finish_2"), "doctor: a scene twice gets a name of its own -> «" + fixAt(13).fix + "»");
    check(fixAt(14).fixMode == 4 && fixAt(14).fix.startsWith(QString::fromUtf8(": nowhere_at_all")), "doctor: a scene nothing is like is made");
    bool lostSaid = false;
    for (const LintIssue& i : is) if (i.line == 10 && i.msg.contains(QString::fromUtf8("игрок не увидит"))) lostSaid = true;
    check(lostSaid, "lint: a scene nobody reaches is said");
    const QString fixed = applyFixes(story, is);
    check(fixed.contains(QString::fromUtf8("показать dv smile pioneer\nпереход finish\n: finish\nмузыка sunny_day\nубратьвсех\nтекст всё\n")) &&
              fixed.contains(QString::fromUtf8(": finish_2\n")) && fixed.contains(QString::fromUtf8(": nowhere_at_all\nтекст ")),
          "«Починить всё» makes every fix at once:\n" + fixed);
    // the story map
    const StoryGraph g = storyGraph(story, v1);
    auto node = [&](const QString& n) { for (const GraphNode& x : g.nodes) if (x.name == n) return x; return GraphNode{}; };
    check(node(QStringLiteral("start")).start && !node(QStringLiteral("finish")).reachable && !node(QStringLiteral("lost")).reachable &&
              node(QStringLiteral("finsh")).kind == QStringLiteral("missing") && node(QStringLiteral("lost")).ending == QStringLiteral("end"),
          "map: the start, reached / lost scenes, a way into nowhere, how scenes end");
    const QString menu = QString::fromUtf8("@mod_id genry_t\nменюмода бл\nкнопка Начать -> start\nкнопка Бонус -> bonus\nкнопка Главы\nконецменюмода\n"
                                           ": start\nвыбор\n- Да -> yes\n- Нет -> no\nконецвыбора\n: yes\nноваяглава Вторая\nконецигры\n: no\nконецигры\n"
                                           ": bonus\nтекст б\nконецигры\n");
    const StoryGraph mg = storyGraph(menu, v1);
    int reachable = 0;
    for (const GraphNode& x : mg.nodes) reachable += x.reachable;
    check(mg.nodes.value(0).kind == QStringLiteral("menu") && mg.nodes.value(0).start && reachable == 5 && mg.edges.size() >= 5,
          QStringLiteral("map: the mod menu is the start, its buttons, the options, chapters (%1 reached, %2 ways)").arg(reachable).arg(mg.edges.size()));
}

// [13] the 7ДЛ mechanics of V2.1: the heroine not met yet, the save's name, «сейчас играет», the screensaver, statistics,
//      the terminal, rock-paper-scissors, the farewell screen, the streamer mode
static void test7dl()
{
    out << "[13] 7DL mechanics\n";
    CompileOptions v1;
    v1.knownSpeakers = {QStringLiteral("sl"), QStringLiteral("dv"), QStringLiteral("us"), QStringLiteral("me")};
    const QString story = QString::fromUtf8(
        "@mod_id genry_t\n@mod_name Лето\n"
        ": start\n"
        "прозвище Славя | Блондинка\n"
        "Славя: Привет! Ты новенький?\n"
        "Славя: Меня Славя зовут.\n"
        "Славя: Пошли.\n"
        "сейчасиграет вкл\n"
        "музыка sunny_day\n"
        "заставкаафк 45 | cg d2_sovenok\n"
        "экранвыхода bg ext_camp_entrance_night | Пока!\n"
        "музыкафайл audio/song.ogg | стрим=everlasting_summer\n"
        "терминал Компьютер | help | help: Доступно: 1968 | 1968 -> secret | exit -> выход\n"
        "кнб Ульяна | победа -> secret | поражение -> secret | раунды=3\n"
        "статистика\n"
        ": secret\n"
        "новаяглава Тайна\n"
        "конецигры\n");
    QString err;
    const QString rpy = compileText(story, v1, &err);
    check(err.isEmpty() && rpy.contains(QString::fromUtf8("    genry_t__stranger_sl_blondinka \"Привет! Ты новенький?\"")) &&
              rpy.contains(QString::fromUtf8("    genry_t__stranger_sl_blondinka \"Меня Славя зовут.\"")) && rpy.contains(QString::fromUtf8("    sl \"Пошли.\"")) &&
              rpy.contains(QString::fromUtf8("$ genry_t__stranger_sl_blondinka = Character(u\"Блондинка\", kind=sl)")),
          "«прозвище»: «Блондинка» until she says her name, then Славя");
    check(rpy.contains(QString::fromUtf8("    $ save_name = u\"Лето\"")) && rpy.contains(QString::fromUtf8("    $ save_name = u\"Лето: Тайна\"")),
          "the save is called by the mod and its chapter");
    check(rpy.contains(QStringLiteral("    $ genry_now_playing(u\"Sunny Day\")")) && rpy.contains(QStringLiteral("screen genry_now_playing(title):")),
          "«сейчас играет»: the track's name");
    check(rpy.contains(QStringLiteral("    show screen genry_afk_watch(45, \"cg d2_sovenok\")")) && rpy.contains(QStringLiteral("screen genry_afk(img):")),
          "«заставкаафк»: after 45 s of standing still");
    check(rpy.contains(QString::fromUtf8("    $ store.genry_farewell_now = (\"bg ext_camp_entrance_night\", u\"Пока!\")")) &&
              rpy.contains(QStringLiteral("config.quit_action = genry_quit")),
          "«экранвыхода»: the farewell before quitting");
    check(rpy.contains(QStringLiteral("    if persistent.genry_streamer:")) && rpy.contains(QStringLiteral("        play music \"mods/genry_t/audio/song.ogg\"")),
          "the streamer mode swaps the mod's own song");
    check(rpy.contains(QString::fromUtf8("genry_terminal_run(u\"Компьютер\", u\"help\", [(u\"help\", u\"Доступно: 1968\", None), (u\"1968\", u\"\", \"genry_t__secret\"), (u\"exit\", u\"\", u\"\")])")),
          "«терминал»: answers, a word into a scene, a word out");
    check(rpy.contains(QString::fromUtf8("    $ _genry_rps = genry_rps_run(u\"Ульяна\", 3)")) && rpy.contains(QStringLiteral("    if _genry_rps == \"win\":")),
          "«кнб»: the game and its results");
    check(rpy.contains(QStringLiteral("    call screen genry_stats(genry_t__scenes, called=True)")) &&
              rpy.contains(QString::fromUtf8("define genry_t__scenes = [(\"genry_t\", u\"start\", False), (\"genry_t__secret\", u\"secret\", True)]")),
          "«статистика»: every scene, the endings marked");
    // the preview knows the stranger too
    const SceneState a = sceneAt(story, 5, nullptr), b = sceneAt(story, 6, nullptr), c = sceneAt(story, 7, nullptr);
    check(a.speakerName == QString::fromUtf8("Блондинка") && b.speakerName == QString::fromUtf8("Блондинка") && c.speakerName != QString::fromUtf8("Блондинка"),
          "preview: " + a.speakerName + " / " + b.speakerName + " / " + c.speakerName);
    // the menu: statistics and the streamer mode as buttons
    const QString menu = compileText(QString::fromUtf8("@mod_id genry_t\nменюмода панель\nкнопка Начать -> start\nкнопка Прогресс\nкнопка Для стрима\nконецменюмода\n: start\nтекст а\nконецигры\n"), v1);
    check(menu.contains(QStringLiteral("Show(\"genry_stats\", scenes=genry_t__scenes)")) && menu.contains(QStringLiteral("ToggleField(persistent, \"genry_streamer\")")) &&
              menu.contains(QStringLiteral("define genry_t__scenes")),
          "menu buttons: «Прогресс» = statistics, «Для стрима» = the streamer mode");
}

// [14] the timeline and the Android export's pixel scaling
static void testTimelineAndroid()
{
    out << "[14] timeline + android\n";
    const QString story = QString::fromUtf8(
        "@mod_id genry_t\n"
        ": start\n"                                  // 2
        "фон ext_square_day\n"                       // 3  beat 0
        "музыка sunny_day\n"                         // 4  beat 1
        "показать sl smile pioneer center\n"         // 5  beat 2
        "Славя: Привет!\n"                           // 6  beat 3
        "показать sl happy pioneer center\n"         // 7  beat 4
        "убрать sl\n"                                // 8  beat 5
        "фон ext_beach_day\n"                        // 9  beat 6
        "текст Пляж.\n"                              // 10 beat 7
        "переход next\n"                             // 11 beat 8
        ": next\n"
        "текст Дальше.\n");
    const SceneTimeline t = sceneTimeline(story, 6);
    auto trackOf = [&](const QString& id) { for (const TimelineTrack& x : t.tracks) if (x.id == id) return x; return TimelineTrack{}; };
    const TimelineTrack bg = trackOf(QStringLiteral("bg")), sl = trackOf(QStringLiteral("char:sl")), say = trackOf(QStringLiteral("say")),
                        mus = trackOf(QStringLiteral("music")), flow = trackOf(QStringLiteral("flow"));
    check(t.scene == QStringLiteral("start") && t.beatLines.size() == 9 && t.beatLines.first() == 3 && t.beatLines.last() == 11,
          QStringLiteral("timeline: the scene under the cursor, a beat a line (%1 beats)").arg(t.beatLines.size()));
    check(bg.clips.size() == 2 && bg.clips[0].from == 0 && bg.clips[0].to == 6 && bg.clips[1].from == 6 && bg.clips[1].to == 9,
          "timeline: a background runs until the next one");
    check(sl.clips.size() == 2 && sl.clips[0].from == 2 && sl.clips[0].to == 4 && sl.clips[1].from == 4 && sl.clips[1].to == 6 &&
              sl.clips[1].label == QStringLiteral("sl happy pioneer"),
          "timeline: a character from «показать» to «убрать», cut where the emotion changes");
    check(say.clips.size() == 2 && mus.clips.size() == 1 && mus.clips[0].to == 9 && flow.clips.size() == 1 && flow.clips[0].line == 11,
          "timeline: lines, music to the scene's end, the way out");
    // the Android export: pixels two thirds, shares of the screen as they are
    check(build::scalePixels(QStringLiteral("    text \"x\" xpos 300 ypos 90 size 36 xalign 0.5"), 2.0 / 3.0) == QStringLiteral("    text \"x\" xpos 200 ypos 60 size 24 xalign 0.5") &&
              build::scalePixels(QStringLiteral("        xysize (1920, 1080)"), 2.0 / 3.0) == QStringLiteral("        xysize (1280, 720)") &&
              build::scalePixels(QStringLiteral("        hotspot (439, 265, 318, 621) action Return(1)"), 2.0 / 3.0) == QStringLiteral("        hotspot (293, 177, 212, 414) action Return(1)") &&
              build::scalePixels(QStringLiteral("    $ renpy.pause(1.0)"), 2.0 / 3.0) == QStringLiteral("    $ renpy.pause(1.0)"),
          "android: xpos / size / xysize / hotspot scaled, the rest untouched: " + build::scalePixels(QStringLiteral("    text \"x\" xpos 300 ypos 90 size 36 xalign 0.5"), 2.0 / 3.0));
}

// [15] «Сломай мой мод»: what only a playthrough shows; the cinema's timed choice and «обход» map as the game has them
static void testBreakMod()
{
    out << "[15] break my mod\n";
    static EsAssets esa;
    QString err;
    if (!esa.load(QStringLiteral(GB_SOURCE_DIR "/data/es_catalog.json"), esRootForTests(), &err)) {
        check(false, "ES for the break test " + err);
        return;
    }
    const QString story = QString::fromUtf8(
        "@mod_id genry_f\n"                              // 1
        "шкала славя | Славя | #ff7a00 | 0 | 10\n"       // 2
        ": start\n"                                      // 3
        "фон ext_square_day\n"                           // 4
        "выбор\n"                                        // 5
        "Славя: Куда?\n"                                 // 6
        "- На площадь [+1 Славя]\n"                      // 7
        "    Славя: Пошли.\n"                            // 8
        "- Поцеловать [нужно славя 5]\n"                 // 9
        "    Славя: Ой.\n"                               // 10
        "- Секрет [если ключ]\n"                         // 11
        "    Славя: Тсс.\n"                              // 12
        "- На пляж -> beach\n"                           // 13
        "- В лес -> forest\n"                            // 14
        "конецвыбора\n"                                  // 15
        "если славя >= 3 -> good_end\n"                  // 16
        "переход ordinary\n"                             // 17
        ": beach\n"                                      // 18
        "текст Пляж.\n"                                  // 19
        "переход nowhere\n"                              // 20
        ": forest\n"                                     // 21
        "выбор\n"                                        // 22
        "- Дальше [нужно славя 9]\n"                     // 23
        "конецвыбора\n"                                  // 24
        "текст Лес.\n"                                   // 25
        "конецигры\n"                                    // 26
        ": ordinary\n"                                   // 27
        "текст Обычный конец.\n"                         // 28
        "конецигры\n"                                    // 29
        ": good_end\n"                                   // 30
        "текст Хороший конец.\n"                         // 31
        "конецигры\n");                                  // 32
    const FuzzReport r = breakMod(story, &esa, 300, 2000);
    auto has = [](const QVector<FuzzHit>& v, const char* kind, int line) {
        for (const FuzzHit& h : v) if (h.kind == QLatin1String(kind) && h.line == line) return &h;
        return static_cast<const FuzzHit*>(nullptr);
    };
    check(r.runs >= 24 && r.endings.size() == 1 && has(r.endings, "finale", 29),
          QStringLiteral("break: the one ending the walks reach (%1 walks, %2 endings)").arg(r.runs).arg(r.endings.size()));
    const FuzzHit* miss = has(r.problems, "missing", 20);
    check(miss && miss->detail == QStringLiteral("nowhere") && !miss->route.isEmpty(), "break: a way into a scene that is not there, with its route");
    check(has(r.problems, "stuck", 22) != nullptr, "break: a choice with every option locked and no timer - the player is stuck");
    const FuzzHit* kiss = nullptr;
    for (const FuzzHit& h : r.locked) if (h.detail == QString::fromUtf8("Поцеловать")) kiss = &h;
    bool hidden = false;
    for (const FuzzHit& h : r.locked) if (h.kind == QLatin1String("hidden") && h.detail == QString::fromUtf8("Секрет")) hidden = true;
    check(kiss && kiss->kind == QLatin1String("lock") && kiss->hint.contains(QString::fromUtf8("5")) && hidden,
          "break: a lock that never opens (with its «нужно»), an «если» option never offered");
    check(r.unseen.size() == 1 && r.unseen[0].scene == QStringLiteral("good_end") && r.unseen[0].kind == QLatin1String("blocked"),
          "break: an ending the points never let anyone into");
    check(r.never.size() == 1 && r.never[0].line == 26, "break: «конецигры» after the stuck choice is never reached");
    // the route replays: the cinema walked along it stops right before the missing scene
    Cinema replay;
    replay.load(story, &esa);
    CinemaStop st = replay.start(1);
    for (int x : miss ? miss->route : QVector<int>()) st = replay.next(x);
    check(st.line == 19 && replay.next(-1).kind == CinemaStop::End, QStringLiteral("break: the route leads to that very moment (line %1)").arg(st.line));

    // the cinema's own fixes: the last option above «время вышло» goes to ITS scene; «обход»: a visited place goes out
    Cinema c;
    c.load(QString::fromUtf8("@mod_id genry_f\n: start\nвыбор на время 5\n- А -> a\n- Б -> b\nвремя вышло -> t\nконецвыбора\n"
                             ": a\nтекст А.\nконецигры\n: b\nтекст Б.\nконецигры\n: t\nтекст Время.\nконецигры\n"), &esa);
    st = c.start(1);
    const bool timed = st.kind == CinemaStop::Choice && st.seconds == 5;
    st = c.next(1);
    check(timed && st.line == 12, QStringLiteral("cinema: the last option of a timed choice goes to its scene (line %1)").arg(st.line));
    c.load(QString::fromUtf8("@mod_id genry_f\n: start\nкарта обход площадь: sq, пляж: be, готово: fin\n"
                             ": sq\nтекст Площадь.\nпереход start\n: be\nтекст Пляж.\nпереход start\n: fin\nтекст Финал.\nконецигры\n"), &esa);
    st = c.start(1);
    const QStringList first = st.options;
    st = c.next(0);                                            // the square
    st = c.next(-1);                                           // back on the map
    const QStringList second = st.options;
    st = c.next(0);                                            // the beach
    st = c.next(-1);                                           // all walked: «готово» by itself
    check(first == QStringList{QString::fromUtf8("Площадь"), QString::fromUtf8("Пляж")} && second == QStringList{QString::fromUtf8("Пляж")} &&
              st.line == 11,
          QStringLiteral("cinema: the «обход» map - places go out, «готово» when all are walked (%1 | %2 | line %3)")
              .arg(first.join(','), second.join(',')).arg(st.line));
}

// [16] the game fell: which mod, which scene, which line of the story, in plain words
static void testCrash()
{
    out << "[16] crash -> story line\n";
    CompileOptions opt;
    opt.knownSpeakers = {QStringLiteral("sl"), QStringLiteral("dv"), QStringLiteral("me")};
    const QString story = QString::fromUtf8(
        "@mod_id genry_crash\n"                          // 1
        ": start\n"                                      // 2
        "фон ext_square_day\n"                           // 3
        "Славя: Привет!\n"                               // 4
        "переход park\n"                                 // 5
        ": park\n"                                       // 6
        "фон ext_park_day\n"                             // 7
        "показать dv smile pioneer left\n"               // 8
        "Алиса: Ну?\n"                                   // 9
        "показать sl smle pioneer center\n"              // 10
        "Славя: Ой.\n"                                   // 11
        "конецигры\n");                                  // 12
    const QString rpy = compileText(story, opt);
    const QStringList rl = rpy.split(QLatin1Char('\n'));
    int at = -1;
    for (int i = 0; i < rl.size() && at < 0; ++i) if (rl[i].contains(QStringLiteral("show sl smle pioneer"))) at = i;
    QTemporaryDir root;
    QDir().mkpath(root.path() + QStringLiteral("/game/mods/genry_crash"));
    auto write = [](const QString& path, const QString& text) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8());
    };
    write(root.path() + QStringLiteral("/game/mods/genry_crash/genry_crash.rpy"), rpy);
    const qint64 before = QDateTime::currentMSecsSinceEpoch() - 5000;
    write(root.path() + QStringLiteral("/traceback.txt"),
          QStringLiteral("I'm sorry, but an uncaught exception occurred.\r\n\r\nWhile running game code:\r\n"
                         "  File \"game/mods/genry_crash/genry_crash.rpy\", line %1, in script\r\n    %2\r\n"
                         "Exception: Image 'sl smle pioneer' not found.\r\n\r\n-- Full Traceback --\r\n  File \"renpy/ast.py\", line 1, in x\r\n")
              .arg(at + 1).arg(pyStrip(rl.value(at))));
    CrashReport c = readCrash(root.path(), before, QStringLiteral("genry_crash"), story, opt);
    check(at >= 0 && c.found && c.kind == QLatin1String("runtime") && c.ours && c.what == QLatin1String("image") &&
              c.arg == QStringLiteral("sl smle pioneer"),
          QStringLiteral("crash: our mod, a missing image named (%1 / %2)").arg(c.what, c.arg));
    check(c.scene == QStringLiteral("park") && c.storyLine == 10,
          QStringLiteral("crash: the scene by the label, the line by its words (scene %1, line %2)").arg(c.scene).arg(c.storyLine));
    check(!readCrash(root.path(), QDateTime::currentMSecsSinceEpoch() + 60000, QStringLiteral("genry_crash"), story, opt).found,
          "crash: a report older than the run is not this run's");
    // somebody else's Workshop mod
    write(root.path() + QStringLiteral("/traceback.txt"),
          QStringLiteral("I'm sorry, but an uncaught exception occurred.\n\nWhile running game code:\n"
                         "  File \"../../workshop/content/331470/1234567/mods/foo/foo.rpy\", line 7, in script\n    $ foo_bar()\n"
                         "NameError: name 'foo_bar' is not defined\n"));
    c = readCrash(root.path(), before, QStringLiteral("genry_crash"), story, opt);
    check(c.found && !c.ours && c.workshopId == QStringLiteral("1234567") && c.mod == QStringLiteral("foo") && c.what == QLatin1String("name") &&
              c.arg == QStringLiteral("foo_bar") && c.storyLine == 0,
          "crash: a Workshop mod's fall is named as somebody else's");
    // a script the game could not read (errors.txt, newer than the traceback)
    QThread::msleep(20);
    write(root.path() + QStringLiteral("/errors.txt"),
          QStringLiteral("I'm sorry, but errors were detected in your script.\n\n\nFile \"game/mods/bar/bar.rpy\", line 430: expected ':' not found.\n    label foo\n         ^\n"));
    c = readCrash(root.path(), before, QStringLiteral("genry_crash"), story, opt);
    check(c.found && c.kind == QLatin1String("parse") && c.what == QLatin1String("syntax") && c.mod == QStringLiteral("bar") && c.rpyLine == 430 &&
              c.error == QStringLiteral("expected ':' not found."),
          "crash: errors.txt - the mod whose script the game could not read");
}

// [18] the list of deleted sprites that ships with GenryBL (a developer build writes its «Удалить» there): only the
// workshop wardrobe's faces - never the game's own sprites (it once took every emotion but «smile» off Ольга Дмитриевна)
static void testShippedHidden()
{
    out << "[18] shipped hidden list: never the game's own sprites\n";
    QFile cf(QStringLiteral(GB_SOURCE_DIR "/data/es_catalog.json"));
    check(cf.open(QIODevice::ReadOnly), "hidden: the catalog opens");
    const QJsonObject sprites = QJsonDocument::fromJson(cf.readAll()).object().value(QStringLiteral("sprites")).toObject();
    QSet<QString> game;                                           // "mt|angry pioneer"
    for (auto it = sprites.begin(); it != sprites.end(); ++it) {
        QStringList w = it.key().split(QLatin1Char(' '));
        if (w.size() > 1 && (w.last() == QLatin1String("close") || w.last() == QLatin1String("far"))) w.removeLast();
        game.insert(w.first() + QLatin1Char('|') + w.mid(1).join(QLatin1Char(' ')));
    }
    check(game.contains(QStringLiteral("mt|angry pioneer")), "hidden: the catalog knows «mt angry pioneer»");
    QFile hf(QStringLiteral(GB_SOURCE_DIR "/data/wardrobe_hidden.txt"));
    check(hf.open(QIODevice::ReadOnly | QIODevice::Text), "hidden: the shipped list opens");
    QStringList bad;
    for (const QString& raw : QString::fromUtf8(hf.readAll()).split(QLatin1Char('\n'))) {
        const QString l = raw.trimmed();
        const int bar = int(l.indexOf(QLatin1Char('|')));
        if (bar > 0 && l.mid(bar + 1).startsWith(QLatin1String("look:")) && game.contains(l.left(bar + 1) + l.mid(bar + 6))) bad << l;
    }
    check(bad.isEmpty(), "hidden: no game sprite in the shipped list" + (bad.isEmpty() ? QString() : " - " + bad.mid(0, 5).join(QStringLiteral(", "))));
    // the reading of the keys: a look, a face on every outfit, an outfit
    const QSet<QString> keys{QStringLiteral("mt|look:angry pioneer"), QStringLiteral("us|face:laugh"), QStringLiteral("un|outfit:coat_bitd")};
    check(Wardrobe::hiddenIn(keys, QStringLiteral("mt"), QStringLiteral("angry"), QStringLiteral("pioneer")) &&
              !Wardrobe::hiddenIn(keys, QStringLiteral("mt"), QStringLiteral("angry"), QStringLiteral("dress")) &&
              Wardrobe::hiddenIn(keys, QStringLiteral("us"), QStringLiteral("laugh"), QStringLiteral("sport")) &&
              Wardrobe::hiddenIn(keys, QStringLiteral("un"), QStringLiteral("smile"), QStringLiteral("coat_bitd")),
          "hidden: look / face / outfit keys read as the wardrobe reads them");
}

// [17] the Center: Discord over its pipe (a stand-in Discord here), the game's mods, the catalog's archive, «Играть»
static bool waitFor(const std::function<bool()>& done, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

static void testHub()
{
    out << "[17] center: discord, mods, catalog\n";
    // ---- Discord: the handshake with the app id, READY, then SET_ACTIVITY - the newest one only, one per gap
    const QString pipe = QStringLiteral("genry-selftest-ipc-%1").arg(QCoreApplication::applicationPid());
    QLocalServer server;
    QLocalServer::removeServer(pipe);
    check(server.listen(pipe), "discord: the stand-in pipe listens");
    QLocalSocket* peer = nullptr;
    QByteArray got;
    QList<QPair<quint32, QJsonObject>> frames;
    auto reply = [&](quint32 op, const QJsonObject& o) {
        const QByteArray j = QJsonDocument(o).toJson(QJsonDocument::Compact);
        QByteArray h(8, '\0');
        qToLittleEndian<quint32>(op, h.data());
        qToLittleEndian<quint32>(quint32(j.size()), h.data() + 4);
        peer->write(h + j);
        peer->flush();
    };
    QObject::connect(&server, &QLocalServer::newConnection, [&] {
        peer = server.nextPendingConnection();
        QObject::connect(peer, &QLocalSocket::readyRead, [&] {
            got += peer->readAll();
            while (got.size() >= 8) {
                const quint32 op = qFromLittleEndian<quint32>(got.constData()), len = qFromLittleEndian<quint32>(got.constData() + 4);
                if (quint32(got.size()) < 8 + len) break;
                frames << qMakePair(op, QJsonDocument::fromJson(got.mid(8, int(len))).object());
                got.remove(0, int(8 + len));
                if (op == 0) reply(1, {{QStringLiteral("cmd"), QStringLiteral("DISPATCH")}, {QStringLiteral("evt"), QStringLiteral("READY")}});
            }
        });
    });
    DiscordPresence d;
    d.setMinGap(400);
    d.setActivity({{QStringLiteral("details"), QStringLiteral("first")}});
    d.setClientId(QStringLiteral("1234567890"), pipe);
    check(waitFor([&] { return d.isReady() && frames.size() >= 2; }), "discord: handshake, READY, the waiting activity goes out");
    check(!frames.isEmpty() && frames[0].first == 0 && frames[0].second.value(QStringLiteral("client_id")).toString() == QStringLiteral("1234567890") &&
              frames[0].second.value(QStringLiteral("v")).toInt() == 1,
          "discord: the handshake carries v=1 and the app id");
    const QJsonObject set = frames.value(1).second;
    check(set.value(QStringLiteral("cmd")).toString() == QLatin1String("SET_ACTIVITY") &&
              set.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject().value(QStringLiteral("details")).toString() == QStringLiteral("first") &&
              set.value(QStringLiteral("args")).toObject().value(QStringLiteral("pid")).toInteger() == QCoreApplication::applicationPid(),
          "discord: SET_ACTIVITY with our pid");
    d.setActivity({{QStringLiteral("details"), QStringLiteral("second")}});
    d.setActivity({{QStringLiteral("details"), QStringLiteral("third")}});
    waitFor([&] { return frames.size() >= 3; }, 3000);
    QCoreApplication::processEvents();
    check(frames.size() == 3 && frames[2].second.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject()
                                    .value(QStringLiteral("details")).toString() == QStringLiteral("third"),
          QStringLiteral("discord: two changes inside the gap - only the newest is sent (%1 frames)").arg(frames.size()));
    // a wrong app id: Discord closes (4000) and nobody knocks again with it
    reply(2, {{QStringLiteral("code"), 4000}, {QStringLiteral("message"), QStringLiteral("Invalid Client ID")}});
    check(waitFor([&] { return !d.isReady() && d.error() == QStringLiteral("Invalid Client ID"); }), "discord: «Invalid Client ID» is heard");

    // ---- the game's mods: the Workshop and game/mods, their Mods-menu entries, the cover, the same entry twice
    QTemporaryDir root;
    const QString es = root.path() + QStringLiteral("/steamapps/common/Everlasting Summer");
    const QString ws = root.path() + QStringLiteral("/steamapps/workshop/content/331470");
    auto put = [](const QString& path, const QByteArray& data) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(data);
    };
    put(es + QStringLiteral("/Everlasting Summer.exe"), "x");
    put(ws + QStringLiteral("/111/mods/foo/foo.rpy"), "init python:\n    mods[\"foo_start\"] = u\"{b}Фу-мод{/b}\"\n");
    put(ws + QStringLiteral("/111/preview.jpg"), "jpg");
    put(es + QStringLiteral("/game/mods/bar/bar.rpy"), "init:\n    $ mods['bar'] = 'Бар'\n    $ mods[\"foo_start\"] = u\"Фу-мод копия\"\n");
    put(es + QStringLiteral("/game/mods/_genry_v1_preview/x.rpy"), "label x:\n    return\n");
    const QVector<HubMod> mods = scanGameMods(es);
    const HubMod* foo = nullptr;
    const HubMod* bar = nullptr;
    for (const HubMod& m : mods) {
        if (m.id == QStringLiteral("111")) foo = &m;
        if (m.id == QStringLiteral("bar")) bar = &m;
    }
    check(mods.size() == 2 && foo && bar, QStringLiteral("hub: a Workshop item and a game/mods folder, not GenryBL's own hooks (%1)").arg(mods.size()));
    check(foo && foo->source == QLatin1String("workshop") && foo->title == QString::fromUtf8("Фу-мод") && foo->preview.endsWith(QStringLiteral("preview.jpg")) &&
              foo->entries.size() == 1 && foo->entries[0].label == QStringLiteral("foo_start"),
          "hub: its name from the game's Mods menu, text tags gone, its cover");
    check(bar && bar->entries.size() == 2 && bar->twins == QStringList{QStringLiteral("workshop:111")} && foo && foo->twins == QStringList{QStringLiteral("local:bar")},
          "hub: the same entry in two mods is seen from both sides");
    // «▶ Играть»: the game's own way - persistent.jump_to + Start, the hook takes itself away
    QString err;
    check(writePlayHook(es, QStringLiteral("foo_start"), &err), "hub: the play hook is written");
    const QString hook = QString::fromUtf8(readAll(es + QStringLiteral("/game/mods/_genry_play/_genry_play.rpy")));
    check(hook.contains(QStringLiteral("persistent.jump_to = \"foo_start\"")) && hook.contains(QStringLiteral("renpy.jump_out_of_context(\"start\")")) &&
              hook.contains(QStringLiteral("os.remove")) && !writePlayHook(es, QStringLiteral("x\"; import os"), &err),
          "hub: the hook starts the mod like the Mods menu and deletes itself; a strange label is refused");
    removePlayHook(es);

    // ---- «Мастерская GenryBL»: a players' archive into game/mods; an update of its own folder; a stranger's folder stays
    const QString src = root.path() + QStringLiteral("/src/genry_cat");
    put(src + QStringLiteral("/genry_cat.rpy"), "label genry_cat:\n    return\n");
    put(src + QStringLiteral("/images/a.png"), "png");
    const QString zip = root.path() + QStringLiteral("/genry_cat.zip");
    check(build::exportZip(src, QStringLiteral("genry_cat"), QStringLiteral("Cat"), zip, &err), "catalog: a players' archive to install (" + err + ")");
    QString folder;
    err.clear();
    const QString modsDir = es + QStringLiteral("/game/mods");
    const bool installed = build::installModArchive(zip, modsDir, QStringLiteral("genry_cat"), &folder, &err);
    check(installed && folder == QStringLiteral("genry_cat") &&
              QFileInfo::exists(modsDir + QStringLiteral("/genry_cat/images/a.png")) && QFileInfo::exists(modsDir + QStringLiteral("/genry_cat/.genrybl_catalog")),
          "catalog: installed into game/mods with its mark (" + err + ")");
    check(build::installModArchive(zip, modsDir, QStringLiteral("genry_cat"), &folder, &err), "catalog: installed again = updated");
    put(modsDir + QStringLiteral("/genry_cat2/keep.txt"), "mine");
    const QString zip2 = root.path() + QStringLiteral("/genry_cat2.zip");
    QDir(root.path() + QStringLiteral("/src/genry_cat2")).removeRecursively();
    put(root.path() + QStringLiteral("/src/genry_cat2/genry_cat2.rpy"), "label genry_cat2:\n    return\n");
    build::exportZip(root.path() + QStringLiteral("/src/genry_cat2"), QStringLiteral("genry_cat2"), QStringLiteral("Cat2"), zip2, &err);
    check(!build::installModArchive(zip2, modsDir, QStringLiteral("genry_cat2"), &folder, &err) && QFileInfo::exists(modsDir + QStringLiteral("/genry_cat2/keep.txt")),
          "catalog: a folder the catalog did not put there is never overwritten");
}

// [19] V2.1.2 Filmora's keyframes («ключ») and the transitions: the game's own, Ren'Py's, GenryBL's from the mods
static void testKeyframes()
{
    out << "[19] keyframes and transitions\n";
    CompileState st;
    const CompileOptions v1;
    const QString k = compileLine(QString::fromUtf8("ключ dv smile pioneer | x 0.3 | масштаб 120% | поворот 10 | прозрачность 0,8 | кувырок | время 0.6"),
                                  QStringLiteral("m"), st, v1).join(QLatin1Char('\n'));
    check(k.startsWith(QStringLiteral("    show dv smile pioneer:\n")) &&
              k.endsWith(QStringLiteral("        ease 0.60 xpos 0.30 xanchor 0.5 zoom 1.20 rotate 10.0 alpha 0.80 xzoom -1.0")),
          "ключ: an ATL block that eases from where it stood\n" + k);
    const QString k0 = compileLine(QString::fromUtf8("ключ dv smile pioneer | y 0.1"), QStringLiteral("m"), st, v1).join(QLatin1Char('\n'));
    check(k0.endsWith(QStringLiteral("\n        ypos 0.10 yanchor 0.0")) && !k0.contains(QStringLiteral("ease")), "ключ without время: at once\n" + k0);
    check(compileLine(QString::fromUtf8("ключ dv smile pioneer"), QStringLiteral("m"), st, v1).join(QLatin1Char('\n')).trimmed() == QStringLiteral("show dv smile pioneer") ||
              compileLine(QString::fromUtf8("ключ dv smile pioneer"), QStringLiteral("m"), st, v1).join(QLatin1Char('\n')).endsWith(QStringLiteral("matrixcolor genry_sprite_time_matrix()")),
          "ключ with nothing to change: no empty ATL block");
    const Keyframe pk = parseKeyframe(QString::fromUtf8("mt angry pioneer | х 0.7 | у -0.05 | кувырок нет | 1.5"));
    check(pk.image == QStringLiteral("mt angry pioneer") && pk.hasX && qAbs(pk.x - 0.7) < 1e-9 && pk.hasY && qAbs(pk.y + 0.05) < 1e-9 &&
              pk.hasFlip && !pk.flip && qAbs(pk.seconds - 1.5) < 1e-9 && !pk.hasZoom,
          "ключ reads the Cyrillic х/у, «кувырок нет» and a bare number as the seconds");
    // the preview stands where the animation ends; only the given ones change
    const SceneState sk = sceneAt(QString::fromUtf8("@mod_id m\n: start\nфон ext_square_day\nпоказать sl smile pioneer left\n"
                                                    "ключ sl smile pioneer | масштаб 1.3 | поворот -8 | прозрачность 0.5 | кувырок | время 0.6\n"
                                                    "ключ sl smile pioneer | x 0.62\nконецигры\n"), 6);
    check(sk.sprites.size() == 1 && qAbs(sk.sprites[0].xpos - 0.62) < 1e-9 && qAbs(sk.sprites[0].zoom - 1.3) < 1e-9 &&
              qAbs(sk.sprites[0].rotate + 8) < 1e-9 && qAbs(sk.sprites[0].alpha - 0.5) < 1e-9 && sk.sprites[0].mirror,
          "ключ: the preview keeps every earlier keyframe and moves only x");
    const SceneState sb = sceneAt(QString::fromUtf8("@mod_id m\n: start\nпоказать sl smile pioneer left\nключ sl | x 0.4 | время 0.5\nконецигры\n"), 4);
    check(sb.sprites.size() == 1 && sb.sprites[0].image == QStringLiteral("sl smile pioneer") && qAbs(sb.sprites[0].xpos - 0.4) < 1e-9,
          "ключ by the tag alone keeps the face: " + (sb.sprites.isEmpty() ? QString() : sb.sprites[0].image));
    const QString kb = compileLine(QString::fromUtf8("ключ sl | x 0.4 | время 0.5"), QStringLiteral("m"), st, v1).join(QLatin1Char('\n'));
    check(kb.startsWith(QStringLiteral("    show sl:\n")) && kb.endsWith(QStringLiteral("        ease 0.50 xpos 0.40 xanchor 0.5")), "ключ by the tag alone compiles to «show sl:»\n" + kb);
    const SceneState sr = sceneAt(QString::fromUtf8("@mod_id m\n: start\nпоказать sl smile pioneer left\nключ sl smile pioneer | поворот 20\n"
                                                    "показать sl smile pioneer right\nконецигры\n"), 5);
    check(sr.sprites.size() == 1 && sr.sprites[0].rotate == 0.0, "a new place resets the turn as it resets the size");
    // the game's own transitions and Ren'Py's end a line now
    for (const char* e : {"flash_red", "fade3", "dissolve_long", "pushleft", "irisout", "blinds", "genry_heart"}) {
        const QString line = QString::fromUtf8("показать dv smile pioneer center ") + QString::fromLatin1(e);
        check(compileLine(line, QStringLiteral("m"), st, v1).join(QLatin1Char('\n')).endsWith(QStringLiteral(" with ") + QString::fromLatin1(e)),
              "transition " + QString::fromLatin1(e) + " ends a «показать»");
    }
    const QString story = QString::fromUtf8("@mod_id genry_tr\n: start\nфон ext_square_day genry_heart\nпоказать dv smile pioneer genry_circle\n"
                                            "Алиса: Привет.\nконецигры\n");
    const QString rpy = compileText(story, v1);
    check(rpy.contains(QStringLiteral("define genry_heart = ImageDissolve(\"mods/genry_tr/genry/fx/genry_heart.png\", 1.0, 48)")) &&
              rpy.contains(QStringLiteral("define genry_circle = ImageDissolve(\"mods/genry_tr/genry/fx/genry_circle.png\", 1.0, 48)")) &&
              !rpy.contains(QStringLiteral("define genry_clock")),
          "a transition from the mods: defined only when used, its mask in genry/fx\n" + rpy.right(600));
    for (const QString& t : genryTransitionNames()) {
        const QImage m = transitionMask(t).convertToFormat(QImage::Format_Grayscale8);
        // where it opens first and where last: the middle and a corner; the curtains from the left; the clock just
        // after twelve and just before it
        const bool fromLeft = t == QStringLiteral("genry_soft") || t == QStringLiteral("genry_diamond");
        const bool clock = t == QStringLiteral("genry_clock");
        const int first = qGray(m.pixel(clock ? 1010 : fromLeft ? 30 : 960, clock ? 120 : 540));
        const int last = qGray(m.pixel(clock ? 910 : 1890, clock ? 120 : 1050));
        check(m.size() == QSize(1920, 1080) && first > last + 60, "mask " + t + QStringLiteral(": white goes first (%1 > %2)").arg(first).arg(last));
    }
}

// V2.1.2 two players' reports: a save made in a mod died after the mod's update («Couldn't find a place to stop rolling
// back»), and a sentence opening with a command word («Музыка - …») went to the command
static void testSavesAndProse()
{
    out << "[20] saves across mod updates, sentences over command words\n";
    const CompileOptions v1;
    const QString r = compileText(QString::fromUtf8("@mod_id genry_t\n@mod_name Тест\n: start\n"
                                                    "Музыка - смысл моей жизни. Я хочу посвятить себя ей.\nЗвук шагов приближался.\n"
                                                    "Пауза затянулась, и никто не говорил ни слова\nмузыка everlasting_summer\nПауза 2\n"
                                                    "текст Музыка играла\nФон ext_square_day fade\n"),
                                  v1);
    check(r.contains(QString::fromUtf8("    \"Музыка - смысл моей жизни. Я хочу посвятить себя ей.\"\n")) &&
              r.contains(QString::fromUtf8("    \"Звук шагов приближался.\"\n")) && r.contains(QString::fromUtf8("    \"Пауза затянулась, и никто")),
          "a sentence that opens with a command word is the story's text");
    check(r.contains(QStringLiteral("genry_resolve_music(\"everlasting_summer\")")) && r.contains(QStringLiteral("    $ renpy.pause(2, hard=True)\n")) &&
              r.contains(QStringLiteral("    scene bg ext_square_day with fade\n")) && r.contains(QString::fromUtf8("    \"Музыка играла\"\n")),
          "commands stay commands: a small first letter, a capital one with arguments only, «текст …» = text");
    check(storyCommandLine(QString::fromUtf8("Музыка everlasting_summer")) && !storyCommandLine(QString::fromUtf8("Музыка - смысл моей жизни.")) &&
              storyCommandLine(QString::fromUtf8("музыка - смысл моей жизни.")) && storyCommandWord(QString::fromUtf8("Звук шагов.")) == QString::fromUtf8("Звук"),
          "the highlighter and the right click read lines the way the compiler does");
    check(r.contains(QStringLiteral("    config.load_failed_label = _genry_load_failed_genry_t\n")) &&
              r.contains(QStringLiteral("    config.label_callback = _genry_scene_cb_genry_t\n")) && r.contains(QStringLiteral("label genry_t__genry_reload:\n")) &&
              r.contains(QStringLiteral("_prev() if callable(_prev) else _prev")) && orphanIndent(r).isEmpty(),
          "a save the update broke restarts its scene; other failed loads go to the handler before ours " + orphanIndent(r));

    // the Builder keeps the .rpyc: the source of the statement names for the next compile, and the project's copy
    const QString root = QDir::tempPath() + QStringLiteral("/genrybl_selftest_names");
    QDir(root).removeRecursively();
    QDir().mkpath(root + QStringLiteral("/es/game/mods"));
    QDir().mkpath(root + QStringLiteral("/proj/assets"));
    QFile exe(root + QStringLiteral("/es/Everlasting Summer.exe"));
    exe.open(QIODevice::WriteOnly);
    exe.close();
    BuildEnv env;
    env.esRoot = root + QStringLiteral("/es");
    env.dataDir = QStringLiteral(GB_SOURCE_DIR "/data");
    env.assetsDir = root + QStringLiteral("/proj/assets");
    const QString story = QString::fromUtf8("@mod_id genry_names\n@mod_name Имена\n: start\nтекст а\n");
    const BuildReport b1 = build::install(env, story, v1, {});
    const QString modDir = root + QStringLiteral("/es/game/mods/genry_names");
    // what the game does on its first run: a .rpyc of this very text (its md5 in the last 16 bytes)
    auto fakeCompile = [&](const QByteArray& tag) {
        QFile rpy(modDir + QStringLiteral("/genry_names.rpy"));
        rpy.open(QIODevice::ReadOnly);
        QByteArray text = rpy.readAll();
        text.replace("\r\n", "\n");
        QFile c(modDir + QStringLiteral("/genry_names.rpyc"));
        c.open(QIODevice::WriteOnly | QIODevice::Truncate);
        c.write("RENPY RPC2" + tag + QCryptographicHash::hash(text, QCryptographicHash::Md5));
    };
    fakeCompile("names-v1");
    QFile orphan(modDir + QStringLiteral("/gone.rpyc"));
    orphan.open(QIODevice::WriteOnly);
    orphan.close();
    const BuildReport b2 = build::install(env, story + QString::fromUtf8("текст б\n"), v1, {});
    QFile kept(modDir + QStringLiteral("/genry_names.rpyc"));
    const bool stays = kept.open(QIODevice::ReadOnly) && kept.readAll().contains("names-v1");
    kept.close();
    check(b1.ok && b2.ok && stays && !QFileInfo::exists(modDir + QStringLiteral("/gone.rpyc")) &&
              QFileInfo::exists(root + QStringLiteral("/proj/genry_names/genry_names.rpyc")),
          "a rebuild keeps the game's .rpyc (the names of the old saves), drops a lone one, the project keeps a copy " + b1.error + b2.error);
    QDir(modDir).removeRecursively();
    const BuildReport b3 = build::install(env, story, v1, {});
    QFile back(modDir + QStringLiteral("/genry_names.rpyc"));
    check(b3.ok && back.open(QIODevice::ReadOnly) && back.readAll().contains("names-v1"),
          "the mod folder gone (a player's copy from the Workshop only): the next build takes the names from the project " + b3.error);
    back.close();
    QDir(root).removeRecursively();
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
    testChoice2();
    testAchievements2();
    testHistory();
    testDoctor();
    test7dl();
    testTimelineAndroid();
    testBreakMod();
    testCrash();
    testHub();
    testShippedHidden();
    testKeyframes();
    testSavesAndProse();
    out << "\nRESULT: " << g_ok << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
