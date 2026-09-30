// gb_cli - GenryBL V1 without the UI.
//   gb_cli compile <story.txt> [--legacy]          print the generated .rpy
//   gb_cli install <story.txt>                     put the mod into Everlasting Summer
//   gb_cli lint    <story.txt>                     install, then run ES's own Ren'Py lint on it
//   gb_cli check   <story.txt>                     GenryBL's own story checks (no game start)
//   gb_cli run     <story.txt> [label]             install and start ES at the label
//   gb_cli frame   <story.txt> <line> <out.png>    render what the player sees at that line
//   gb_cli chibis  <any story.txt>                 (re)draw data/mod_assets/chibi/*.png from the ES sprites
//   gb_cli wardrobe <any story.txt> <tag | "image"> [out.png]   the workshop wardrobe (--no-wardrobe skips loading it)
//   gb_cli gate [--keep] [--only <word>]           «замок»: every story GenryBL must build (tests/gate, the golden stories,
//                                                  the lab, every command form, the mod menu in every style with every
//                                                  particle…) installed as mods at once and checked by the game itself -
//                                                  each screen built, each python name, the game's lint. Exit 1 = broken.
#include "Builder.h"
#include "Cinema.h"
#include "Fuzz.h"
#include "EsAssets.h"
#include "Forms.h"
#include "Library.h"
#include "Lint.h"
#include "Renderer.h"
#include "Scene.h"
#include "Screenplay.h"
#include "Text.h"
#include "Wardrobe.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

using namespace gb;

static QTextStream out(stdout);

static int fail(const QString& m)
{
    out << "ERROR: " << m << "\n";
    return 1;
}

namespace {

struct GateStory { QString name, text, assets; };

QString readText(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? stripBom(QString::fromUtf8(f.readAll())) : QString();
}

// the story under another mod name: the gate's own, so nothing of the maker's is touched
QString withModId(const QString& text, const QString& modId)
{
    static const QRegularExpression line(QStringLiteral("(?m)^@mod_id[ \\t].*$"));
    QString out = text;
    if (out.contains(line)) out.replace(line, QStringLiteral("@mod_id ") + modId);
    else out.prepend(QStringLiteral("@mod_id ") + modId + QLatin1Char('\n'));
    return out;
}

QVector<GateStory> gateStories(const QString& root, const QString& only)
{
    QVector<GateStory> all;
    auto addDir = [&](const QString& dir, const QString& prefix) {
        for (const QFileInfo& fi : QDir(dir).entryInfoList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name)) {
            if (QFileInfo::exists(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".error"))) continue;
            QString text = readText(fi.absoluteFilePath());
            // a lab piece is a piece of a scene: it plays as the whole little mod the lab shows (Engine::labStory)
            if (prefix == QLatin1String("lab/")) text = QStringLiteral("@mod_name Lab\n: start\n") + text + QStringLiteral("\nконецигры\n");
            if (!text.trimmed().isEmpty()) all.push_back({prefix + fi.completeBaseName(), text, fi.absolutePath() + QStringLiteral("/assets")});
        }
    };
    addDir(root + QStringLiteral("/tests/gate"), QStringLiteral("gate/"));
    addDir(root + QStringLiteral("/data/lab"), QStringLiteral("lab/"));
    addDir(root + QStringLiteral("/tests/golden/stories"), QStringLiteral("golden/"));
    // the maker's showcase mods, when this is the developer's tree
    for (const QString& p : {QStringLiteral("genrybl_demo"), QStringLiteral("genrybl_workshop")}) {
        const QString dir = root + QStringLiteral("/projects/") + p;
        const QString text = readText(dir + QStringLiteral("/story.txt"));
        if (!text.isEmpty()) all.push_back({QStringLiteral("project/") + p, text, dir + QStringLiteral("/assets")});
    }
    // every command form, every variant, with its default values: one scene each
    Forms forms;
    QString err;
    if (forms.load(root + QStringLiteral("/data/forms.json"), &err)) {
        QString story = QStringLiteral("@mod_name Gate forms\n\n");
        int n = 0;
        QStringList menus;
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
            for (const QString& v : variants) {
                QVariantMap preset;
                if (!by.isEmpty()) preset.insert(by, v);
                const QString line = forms.build(id, forms.defaults(id, preset));
                if (id == QLatin1String("modmenu")) { menus << line; continue; }
                story += QStringLiteral(": f%1\nфон ext_square_day\n%2\nтекст ок\nпереход f%3\n\n").arg(n).arg(line).arg(n + 1);
                ++n;
            }
        }
        story += QStringLiteral(": f%1\nтекст конец\nконецигры\n").arg(n);
        // the forms' own example files (audio/theme.ogg, images/…): there, empty - the game only asks if they exist
        const QString fake = QDir::tempPath() + QStringLiteral("/genrybl_gate_forms/assets");
        QDir(fake).removeRecursively();
        static const QRegularExpression file(QStringLiteral("\\b((?:audio|images|video|fonts)/[^\\s|\"]+\\.[A-Za-z0-9]+)"));
        for (auto m = file.globalMatch(story); m.hasNext();) {
            const QString rel = m.next().captured(1);
            QDir().mkpath(QFileInfo(fake + QLatin1Char('/') + rel).absolutePath());
            QFile f(fake + QLatin1Char('/') + rel);
            if (f.open(QIODevice::WriteOnly)) f.write("0");
        }
        all.push_back({QStringLiteral("forms/all"), story, fake});
        // the mod's main menu: every style with every particle; «свой» in every layout and look
        static const char* const styles[] = {"свой", "бл", "7дл", "панель", "тетрадь", "дневник", "доска", "монитор", "нуар", "живое", "кино", "карта"};
        static const char* const fxs[] = {"по часам", "пыль", "листья", "светлячки", "дождь", "снег", "сердца", "нет"};
        static const char* const layouts[] = {"слева", "справа", "по центру", "снизу"};
        static const char* const looks[] = {"текст", "таблички", "бл", "неон"};
        static const char* const enters[] = {"выезд", "проявление", "снизу", "печать"};
        const QVariantList buttons{
            QVariantMap{{QStringLiteral("cap"), QStringLiteral("Заселиться")}, {QStringLiteral("scene"), QStringLiteral("start")}},
            QVariantMap{{QStringLiteral("cap"), QStringLiteral("Продолжить")}, {QStringLiteral("scene"), QStringLiteral("загрузить")}},
            QVariantMap{{QStringLiteral("cap"), QStringLiteral("Дни лагеря")}, {QStringLiteral("scene"), QStringLiteral("главы")}},
            QVariantMap{{QStringLiteral("cap"), QStringLiteral("Фото")}, {QStringLiteral("scene"), QStringLiteral("галерея")}},
            QVariantMap{{QStringLiteral("cap"), QStringLiteral("Значки")}, {QStringLiteral("scene"), QStringLiteral("достижения")}},
            QVariantMap{{QStringLiteral("cap"), QStringLiteral("Дружба")}, {QStringLiteral("scene"), QStringLiteral("отношения")}},
            QVariantMap{{QStringLiteral("cap"), QStringLiteral("Опции")}, {QStringLiteral("scene"), QStringLiteral("настройки")}},
            QVariantMap{{QStringLiteral("cap"), QStringLiteral("Как звать")}, {QStringLiteral("scene"), QStringLiteral("имя")}},
            QVariantMap{{QStringLiteral("cap"), QStringLiteral("Бонус")}, {QStringLiteral("scene"), QStringLiteral("bonus")}},
            QVariantMap{{QStringLiteral("cap"), QStringLiteral("Выселение")}, {QStringLiteral("scene"), QStringLiteral("выход")}}};
        const QString tail = QStringLiteral("\n\n: start\nфон ext_square_day\nтекст начало\nконецигры\n: bonus\nтекст бонус\nконецигры\n");
        int k = 0;
        for (const char* st : styles)
            for (const char* fx : fxs) {
                QVariantMap v = forms.defaults(QStringLiteral("modmenu"), {{QStringLiteral("style"), QString::fromUtf8(st)}});
                v.insert(QStringLiteral("fx"), QString::fromUtf8(fx));
                v.insert(QStringLiteral("buttons"), buttons);
                all.push_back({QStringLiteral("menu/%1+%2").arg(QString::fromUtf8(st), QString::fromUtf8(fx)),
                               QStringLiteral("@mod_name Gate menu\n") + forms.build(QStringLiteral("modmenu"), v) + tail, QString()});
                ++k;
            }
        for (int l = 0; l < 4; ++l)
            for (int o = 0; o < 4; ++o) {
                QVariantMap v = forms.defaults(QStringLiteral("modmenu"), {{QStringLiteral("style"), QString::fromUtf8("свой")}});
                v.insert(QStringLiteral("layout"), QString::fromUtf8(layouts[l]));
                v.insert(QStringLiteral("look"), QString::fromUtf8(looks[o]));
                v.insert(QStringLiteral("enter"), QString::fromUtf8(enters[(l + o) % 4]));
                v.insert(QStringLiteral("fx"), QString::fromUtf8(fxs[(l * 4 + o) % 8]));
                v.insert(QStringLiteral("accent"), QStringLiteral("#8be9fd"));
                v.insert(QStringLiteral("buttons"), buttons);
                all.push_back({QStringLiteral("menu/свой %1 %2").arg(QString::fromUtf8(layouts[l]), QString::fromUtf8(looks[o])),
                               QStringLiteral("@mod_name Gate menu\n") + forms.build(QStringLiteral("modmenu"), v) + tail, QString()});
            }
        for (const QString& m : menus) all.push_back({QStringLiteral("forms/menu"), QStringLiteral("@mod_name Gate menu\n") + m + tail, QString()});
    }
    if (only.isEmpty()) return all;
    QVector<GateStory> some;
    for (const GateStory& s : all) if (s.name.contains(only, Qt::CaseInsensitive)) some.push_back(s);
    return some;
}

void clearGateMods(const QString& esRoot)
{
    for (const QFileInfo& fi : QDir(esRoot + QStringLiteral("/game/mods")).entryInfoList({QStringLiteral("gbgate_*")}, QDir::Dirs | QDir::NoDotAndDotDot))
        QDir(fi.absoluteFilePath()).removeRecursively();
    build::removeGate(esRoot);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    const bool gate = a.value(1) == QLatin1String("gate");
    if (a.size() < 3 && !gate) {
        out << "usage: gb_cli compile|install|lint|run|frame <story.txt> ... | gb_cli gate\n";
        return 2;
    }
    QFile f(a.value(2));
    if (!gate && !f.open(QIODevice::ReadOnly)) return fail("cannot read " + a[2]);
    const QString story = gate ? QString() : QString::fromUtf8(f.readAll());
    const QString root = QStringLiteral(GB_SOURCE_DIR);
    static EsAssets es;
    QString err;
    const QString esRoot = qEnvironmentVariable("GENRYBL_ES").isEmpty() ? build::detectEsRoot() : qEnvironmentVariable("GENRYBL_ES");
    Vfs::setBundled(root + "/data/patch/i8_data.rpa", QLatin1String(EsAssets::kHentaiPatchId));
    const bool esOk = es.load(root + "/data/es_catalog.json", esRoot, &err);
    CompileOptions opt;
    opt.legacy = a.contains("--legacy");
    if (esOk) for (auto it = es.characters.begin(); it != es.characters.end(); ++it) opt.knownSpeakers.insert(it.key());
    if (esOk && !opt.legacy) {
        for (auto it = es.music.begin(); it != es.music.end(); ++it) opt.esMusic.insert(it.key());
        for (auto it = es.sounds.begin(); it != es.sounds.end(); ++it) opt.esSounds.insert(it.key());
        for (auto it = es.ambience.begin(); it != es.ambience.end(); ++it) opt.esAmbience.insert(it.key());
    }
    const QString cmd = a[1];
    // the workshop wardrobe (workshop outfits on ES bodies): compile/lint/preview know its sprites
    static Library wlib;
    static Wardrobe wr;
    if (esOk && cmd != "screenplay" && cmd != "layerscan" && cmd != "adultscan" && cmd != "chibis" && !a.contains("--no-wardrobe")) {
        QElapsedTimer t;
        t.start();
        wlib.scan(Vfs::workshopDirFor(esRoot));
        const qint64 scanMs = t.elapsed();
        wr.build(wlib, es, root + "/work/cache/wardrobe_boxes.tsv");
        setWardrobe(&wr);
        if (cmd == "wardrobe")
            out << "# wardrobe: " << wr.partCount() << " workshop layers, library " << scanMs << " ms, build " << (t.elapsed() - scanMs)
                << " ms, decoded " << wr.decoded() << "\n";
    }
    if (cmd == "wardrobe") {
        //   gb_cli wardrobe <story> <tag>                 the workshop outfits of a character
        //   gb_cli wardrobe <story> "<image>" [out.png]   how an image resolves (+ its picture)
        const QString what = a.value(3);
        if (!what.contains(' ')) {
            for (const Wardrobe::Outfit& o : wr.outfits(what))
                out << (o.adult ? "18+ " : "    ") << (o.body ? "body " : "     ") << o.part << "  poses=" << o.poses << "  src=" << o.source
                    << "  dists=" << (o.dists.contains(QString()) ? QStringLiteral("normal ") : QString()) << (o.dists.contains("close") ? "close " : "")
                    << (o.dists.contains("far") ? "far" : "") << "  looks=" << wr.looks(what, o.part, o.dists.value(0)).size() << "\n";
            return 0;
        }
        WardrobeLook look;
        QString why;
        if (!wr.resolve(what, &look, &why)) return fail("not resolved: " + (why.isEmpty() ? QStringLiteral("(ES sprite or unknown)") : why));
        out << "pose " << look.pose << " canvas " << look.w << "x" << look.h << (look.adult ? " adult" : "") << "\n";
        for (int i = 0; i < look.layers.size(); ++i)
            out << "  " << look.kinds[i] << " " << (look.layers[i].src.isEmpty() ? QStringLiteral("game") : look.layers[i].src) << " " << look.layers[i].path << "\n";
        out << wr.definition(what, "genry_test") << "\n";
        if (a.size() > 4 && !a[4].startsWith("--")) {
            const QImage img = wr.compose(what);
            if (img.isNull() || !img.save(a[4])) return fail("cannot compose/save");
            int opaque = 0;
            for (int y = 0; y < img.height(); y += 4)
                for (int x = 0; x < img.width(); x += 4) opaque += qAlpha(img.pixel(x, y)) > 128;
            out << "saved " << a[4] << " opaque samples " << opaque << "\n";
        }
        return 0;
    }
    if (cmd == "cinema") {
        //   gb_cli cinema <story> [line] [pick pick …]   walk the route like «кино-режим»; picks = option numbers (t = timeout)
        if (!esOk) return fail(err);
        Cinema cin;
        cin.load(story, &es);
        QStringList picks = a.mid(4);
        picks.removeAll("--no-wardrobe");
        static const char* kinds[] = {"SAY", "CHOICE", "NOTE", "CARD", "TIMED", "VIDEO", "END"};
        CinemaStop st = cin.start(a.value(3).toInt());
        for (int n = 0; n < 400; ++n) {
            out << QString::number(st.line).rightJustified(4) << " " << kinds[st.kind];
            if (st.kind == CinemaStop::Say) out << "  " << st.scene.speakerName << ": " << st.scene.text;
            if (st.kind == CinemaStop::Card) out << "  [" << st.scene.cardKind << "] " << st.scene.cardText;
            if (st.kind == CinemaStop::Note) out << "  " << st.scene.nvlText.left(60);
            if (st.kind == CinemaStop::Choice) {
                QStringList o;
                for (int i = 0; i < st.options.size(); ++i) o << QString::number(i) + ":" + st.options[i] + (st.optionOk.value(i, true) ? "" : "(!)");
                out << "  " << o.join("  ") << (st.seconds > 0 ? QStringLiteral("  timer=%1").arg(st.seconds) : QString());
            }
            if (st.kind == CinemaStop::Timed || st.kind == CinemaStop::Card) out << "  " << st.seconds << "s";
            if (st.kind == CinemaStop::Video) out << "  " << st.video;
            out << "  bg=" << st.scene.bg << " spr=" << st.scene.sprites.size();
            if (!st.music.isEmpty()) out << "  music=" << st.music;
            if (!st.ambience.isEmpty()) out << "  amb=" << st.ambience;
            if (!st.sounds.isEmpty()) out << "  snd=" << st.sounds.join(",");
            if (!st.popups.isEmpty()) out << "  pop=" << st.popups.join(" ; ");
            if (!st.moment.isEmpty()) out << "  moment=" << st.moment;
            if (!st.note.isEmpty()) out << "  NOTE: " << st.note;
            out << "\n";
            if (st.kind == CinemaStop::End) break;
            int pick = -1;
            if (st.kind == CinemaStop::Choice) {
                const QString p = picks.isEmpty() ? QStringLiteral("0") : picks.takeFirst();
                pick = p == "t" ? -1 : p.toInt();
                out << "       -> pick " << (pick < 0 ? QStringLiteral("timeout") : st.options.value(pick)) << "\n";
            }
            st = cin.next(pick);
        }
        out << "steps " << cin.steps() << "\n";
        return 0;
    }
    if (cmd == "break") {
        //   gb_cli break <story>   «Сломай мой мод»: hundreds of walks by the cinema, what they found
        if (!esOk) return fail(err);
        QElapsedTimer t;
        t.start();
        const FuzzReport r = breakMod(story, &es);
        out << "walks " << r.runs << " in " << t.elapsed() << " ms, clicks " << r.clicksMin << ".." << r.clicksMax << " (avg " << r.clicksAvg
            << "), reading " << r.minutesMin << ".." << r.minutesMax << " min, scenes " << r.scenesSeen << "/" << r.scenes << ", choices " << r.choices << "\n";
        auto dump = [&](const char* head, const QVector<FuzzHit>& v) {
            for (const FuzzHit& h : v)
                out << head << " " << h.kind << " line " << h.line << " [" << h.scene << "] x" << h.count << " route " << h.route.size()
                    << (h.detail.isEmpty() ? QString() : "  " + h.detail) << (h.hint.isEmpty() ? QString() : "  (" + h.hint + ")") << "\n";
        };
        dump("ENDING ", r.endings);
        dump("PROBLEM", r.problems);
        dump("LOCKED ", r.locked);
        dump("UNSEEN ", r.unseen);
        dump("NEVER  ", r.never);
        return 0;
    }
    if (cmd == "screenplay") {          // pasted text -> story lines (--expand: plain commands)
        QVector<ScreenplayNote> notes;
        for (const QString& l : convertToStory(story, a.contains("--expand"), &notes)) out << l << "\n";
        for (const ScreenplayNote& n : notes) out << (n.warn ? "# WARN " : "# info ") << n.line + 1 << ": " << n.text << "\n";
        return 0;
    }
    if (cmd == "compile") {
        out << compileText(story, opt, &err);
        return err.isEmpty() ? 0 : fail(err);
    }
    if (!esOk) return fail(err);
    if (cmd == "check") {
        LintContext ctx;
        ctx.es = &es;
        ctx.opt = opt;
        const QVector<LintIssue> issues = lintStory(story, ctx);
        static const char* lv[] = {"info", "WARN", "ERROR"};
        for (const LintIssue& i : issues) out << "  " << i.line << " " << lv[qBound(0, i.level, 2)] << ": " << i.msg << "\n";
        if (issues.isEmpty()) out << "no issues\n";
        return 0;
    }
    BuildEnv env;
    env.esRoot = esRoot;
    env.dataDir = root + "/data";
    env.assetsDir = QFileInfo(a.value(2)).absolutePath() + "/assets";
    env.backupsDir = root + "/work/backups";
    env.saveDir = root + "/work/es_saves";
    if (cmd == "install" || cmd == "lint" || cmd == "run" || cmd == "export") {
        const BuildReport r = build::install(env, story, opt, [](const QString& s) { out << "  " << s << "\n"; out.flush(); });
        if (!r.ok) return fail(r.error);
        out << "installed " << r.modFile << " labels: " << r.labels.join(' ') << "\n";
        if (cmd == "lint") {
            // what «Проверить движком» does: the game builds the mod's screens, checks its names, runs its lint on it
            out << "the game checks the mod...\n";
            out.flush();
            const build::GameCheck c = build::gameCheck(esRoot, root + "/data", {r.meta.modId});
            if (!c.ran) return fail(c.error);
            if (c.hits.isEmpty()) out << "LINT CLEAN: " << c.statements << " statements, " << c.screens << " screens built\n";
            for (const QString& h : c.hits) out << "  " << h << "\n";
            return c.hits.isEmpty() ? 0 : 1;
        }
        if (cmd == "export") {
            // gb_cli export <story.txt> zip|workshop <out .zip | out folder>: what the «Экспорт» button does
            out << "the game checks the mod...\n";
            out.flush();
            const build::GameCheck c = build::gameCheck(esRoot, root + "/data", {r.meta.modId});
            if (!c.ran) return fail(c.error);
            if (!c.hits.isEmpty()) {
                for (const QString& h : c.hits) out << "  " << h << "\n";
                return fail("the game found problems - no export");
            }
            const QStringList missing = build::missingModFiles(r.modDir, r.meta.modId);
            if (!missing.isEmpty()) return fail("not inside the mod: " + missing.join(", "));
            const QString kind = a.value(3), dest = a.value(4);
            QStringList notes;
            const bool ok = kind == "workshop" ? build::exportWorkshopFolder(r.modDir, r.meta.modId, r.meta.modName, dest, &err)
                          : kind == "android"  ? build::exportAndroid(r.modDir, r.meta.modId, r.meta.modName, dest, &err, &notes)
                                               : build::exportZip(r.modDir, r.meta.modId, r.meta.modName, dest, &err);
            for (const QString& n : notes) out << "  note: " << n << "\n";
            if (!ok) return fail(err);
            out << "EXPORTED " << dest << "\n";
            return 0;
        }
        if (cmd == "run") {
            // scene names as written in the story ("evening") -> the mod's real label, like the app does
            const QString scene = a.value(3);
            const QString label = scene.isEmpty() ? QString() : sceneLabel(r.meta.modId, scene, opt);
            const qint64 pid = build::runAt(env, r.meta.modId, label, &err);
            if (!pid) return fail(err);
            out << "started ES pid " << pid << "\n";
        }
        return 0;
    }
    if (cmd == "gate") {
        if (!esOk) return fail(err);
        const QString only = a.indexOf("--only") > 0 ? a.value(a.indexOf("--only") + 1) : QString();
        const QVector<GateStory> stories = gateStories(root, only);
        out << "gate: " << stories.size() << " stories\n";
        out.flush();
        clearGateMods(esRoot);
        QHash<QString, QString> nameOf;
        QStringList ids, failed;
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < stories.size(); ++i) {
            const QString modId = QStringLiteral("gbgate_%1").arg(i, 3, 10, QLatin1Char('0'));
            BuildEnv env;
            env.esRoot = esRoot;
            env.dataDir = root + "/data";
            env.assetsDir = stories[i].assets.isEmpty() ? QDir::tempPath() + "/genrybl_gate_noassets/assets" : stories[i].assets;
            env.es = &es;
            const BuildReport r = build::install(env, withModId(stories[i].text, modId), opt, {});
            if (!r.ok) {
                // «нечего собирать» is a story's own business (an empty test), everything else is a failure
                if (!r.error.contains(QString::fromUtf8("собирать нечего"))) failed << stories[i].name + ": BUILD " + r.error;
                continue;
            }
            ids << modId;
            nameOf.insert(modId, stories[i].name);
        }
        out << "installed " << ids.size() << " mods in " << t.elapsed() / 1000 << " s; the game checks them...\n";
        out.flush();
        // Ren'Py 8 («renpy8» branch in Steam, Python 3): every piece of Python the mods carry must compile there too
        {
            QStringList files;
            for (const QString& id : ids) files << QDir::toNativeSeparators(esRoot + "/game/mods/" + id + "/" + id + ".rpy");
            QProcess py;
            py.start(QStringLiteral("python"), QStringList{QDir::toNativeSeparators(root + "/data/gate/py3check.py")} + files);
            if (!py.waitForStarted(10000)) {
                out << "  (no Python 3 here - the Ren'Py 8 check is skipped)\n";
            } else {
                py.waitForFinished(300000);
                static const QRegularExpression modOfPy(QStringLiteral("(gbgate_\\d+)"));
                for (const QString& l : QString::fromUtf8(py.readAllStandardOutput()).split('\n')) {
                    const QString line = l.trimmed();
                    if (line.startsWith(QLatin1String("PY3 CHECKED"))) { out << "  " << line << "\n"; continue; }
                    if (!line.contains(QLatin1String(": Python 3: "))) continue;
                    failed << nameOf.value(modOfPy.match(line).captured(1), QStringLiteral("?")) + ": " + line.section(QStringLiteral("/game/"), -1);
                }
                out.flush();
            }
        }
        if (a.contains("--dry")) {                       // only the builds (no game start)
            for (const QString& fl : failed) out << "  FAIL " << fl << "\n";
            if (!a.contains("--keep")) clearGateMods(esRoot);
            return failed.isEmpty() ? 0 : 1;
        }
        build::GameCheck c;
        for (int round = 0; round < 5; ++round) {
            c = build::gameCheck(esRoot, root + "/data", ids);
            if (c.ran) break;
            // a mod that does not even parse stops the whole game: its errors go to the report, it leaves the run
            static const QRegularExpression parseErr(QStringLiteral("^File \"game/mods/(gbgate_\\d+)/[^\"]+\", line (\\d+): (.*)$"));
            const QStringList con = c.console.split('\n');
            QSet<QString> broken;
            for (int i = 0; i < con.size(); ++i) {
                const QRegularExpressionMatch m = parseErr.match(con[i].trimmed());
                if (!m.hasMatch()) continue;
                broken.insert(m.captured(1));
                failed << nameOf.value(m.captured(1), QStringLiteral("?")) + ": PARSE " + m.captured(1) + ".rpy:" + m.captured(2) + ": " + m.captured(3) +
                              "  |  " + con.value(i + 1).trimmed();
            }
            if (broken.isEmpty()) break;
            for (const QString& id : broken) {
                ids.removeAll(id);
                QDir(esRoot + "/game/mods/" + id).removeRecursively();
            }
            out << "  " << broken.size() << " mods do not parse - out of the run, again...\n";
            out.flush();
        }
        if (!a.contains("--keep")) clearGateMods(esRoot);
        if (!c.ran) {
            for (const QString& fl : failed) out << "  FAIL " << fl << "\n";
            out << "GATE: the game did not finish: " << c.error << "\n";
            return 1;
        }
        out << "the game checked " << c.statements << " statements, built " << c.screens << " screens in " << t.elapsed() / 1000 << " s\n";
        static const QRegularExpression modOf(QStringLiteral("(gbgate_\\d+)"));
        int notes = 0;
        QString lastId;
        for (const QString& h : c.hits) {
            const QString id = h.startsWith(QLatin1String("    ")) ? lastId : modOf.match(h).captured(1);
            lastId = id;
            // the golden stories are garbage on purpose (typos, empty commands): a picture that is not there is the
            // game's red text there, not a crash - only what would stop the game counts for them
            if (nameOf.value(id).startsWith(QLatin1String("golden/")) && c.lintHits.contains(h)) { ++notes; continue; }
            failed << (nameOf.value(id, QStringLiteral("?")) + ": " + h);
        }
        if (notes) out << "  (" << notes << " lint notes on the garbage-on-purpose golden stories - not counted)\n";
        for (const QString& fl : failed) out << "  FAIL " << fl << "\n";
        out << (failed.isEmpty() ? "GATE CLEAN\n" : QStringLiteral("GATE: %1 problems\n").arg(failed.size()));
        return failed.isEmpty() ? 0 : 1;
    }
    if (cmd == "chibis") {
        // data/mod_assets/chibi/<id>.png: round faces for the camp map (the Steam build has no map_icon_nXX.png)
        Renderer r;
        r.setAssets(&es, root + "/data");
        const QString dir = root + "/data/mod_assets/chibi";
        QDir().mkpath(dir);
        for (const QString& id : esChibiIds()) {
            const QStringList names = es.spriteNames(id);
            QString pose;
            for (const QString& n : names) if (n == QLatin1String("normal pioneer")) pose = n;
            for (const QString& n : names) if (pose.isEmpty() && n.startsWith(QLatin1String("normal"))) pose = n;
            if (pose.isEmpty() && !names.isEmpty()) pose = names.first();
            const QImage face = pose.isEmpty() ? QImage() : r.faceThumb(id + QLatin1Char(' ') + pose, 192);
            QImage icon(72, 72, QImage::Format_ARGB32_Premultiplied);
            icon.fill(Qt::transparent);
            QPainter p(&icon);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            const QRectF disc(4, 3, 64, 64);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 90));
            p.drawEllipse(disc.translated(0, 3));
            QPainterPath clip;
            clip.addEllipse(disc);
            p.save();
            p.setClipPath(clip);
            p.fillRect(disc, QColor(250, 244, 226));
            if (!face.isNull()) {
                p.drawImage(disc, face);
            } else {
                QFont fnt(QStringLiteral("Calibri"));
                fnt.setPixelSize(40);
                fnt.setBold(true);
                p.setFont(fnt);
                p.setPen(QColor(90, 70, 50));
                p.drawText(disc, Qt::AlignCenter, id == QLatin1String("?") ? QStringLiteral("?") : esChibiName(id).left(1));
            }
            p.restore();
            QColor ring(es.characterColor(id, QStringLiteral("day")));
            if (!ring.isValid()) ring = QColor(0xff, 0xdd, 0x7d);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(ring, 4));
            p.drawEllipse(disc.adjusted(2, 2, -2, -2));
            p.setPen(QPen(QColor(255, 255, 255, 230), 1.6));
            p.drawEllipse(disc.adjusted(-0.5, -0.5, 0.5, 0.5));
            p.end();
            const QString file = dir + "/" + esChibiFile(id) + ".png";
            if (!icon.save(file)) return fail("cannot save " + file);
            out << id << " " << (pose.isEmpty() ? QStringLiteral("(letter)") : pose) << " -> " << file << "\n";
        }
        return 0;
    }
    if (cmd == "layerscan") {
        // every picture of the workshop laid out like ES's own sprite layers: sprites/<dist>/<tag>/<tag>_<pose>_<part>.png
        Library lib;
        lib.scan(Vfs::workshopDirFor(es.esRoot()));
        static const QRegularExpression layer(QStringLiteral("(?:^|/)sprites?/(normal|close|far)/([a-z]+)/([a-z]+)_(\\d+)_([^/]+)\\.png$"),
                                              QRegularExpression::CaseInsensitiveOption);
        for (const LibItem& it : lib.items())
            for (const QString& vp : it.images) {
                const auto m = layer.match(vp);
                if (m.hasMatch()) out << it.id << '\t' << m.captured(1) << '\t' << m.captured(2) << '\t' << m.captured(4) << '\t' << m.captured(5) << '\t' << vp << "\n";
            }
        return 0;
    }
    if (cmd == "adultscan") {
        // every 18+ picture of the workshop with the heroine it shows and how whole it is (TSV for the wardrobe work)
        Library lib;
        lib.scan(Vfs::workshopDirFor(es.esRoot()));
        static const QRegularExpression adult(QStringLiteral("naked|nude|nud_|_nud|hentai|undress|topless|underwear|lingerie|panties|bra_|_bra|"
                                                             "towel|nsfw|golaya|golaja|nagaya|body_n|_n_|bare|sex|xxx|18\\+|"
                                                             "\\x{0433}\\x{043e}\\x{043b}|\\x{0431}\\x{0435}\\x{043b}\\x{044c}|\\x{043d}\\x{0430}\\x{0433}"),
                                              QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        for (const LibItem& it : lib.items()) {
            for (const QString& vp : it.images) {
                if (!adult.match(vp).hasMatch()) continue;
                const QString ref = it.id + QLatin1Char('/') + vp;
                const QImage img = QImage::fromData(lib.read(ref)).convertToFormat(QImage::Format_ARGB32);
                if (img.isNull()) continue;
                int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
                qint64 opaque = 0;
                for (int y = 0; y < img.height(); y += 2) {
                    const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                    for (int x = 0; x < img.width(); x += 2) {
                        if (qAlpha(row[x]) < 128) continue;
                        ++opaque;
                        x0 = qMin(x0, x); x1 = qMax(x1, x); y0 = qMin(y0, y); y1 = qMax(y1, y);
                    }
                }
                out << it.id << '\t' << img.width() << '\t' << img.height() << '\t' << img.hasAlphaChannel() << '\t' << x0 << ',' << y0 << ','
                    << x1 << ',' << y1 << '\t' << QString::number(4.0 * opaque / qMax(1, img.width() * img.height()), 'f', 3) << '\t' << vp << "\n";
            }
        }
        return 0;
    }
    if (cmd == "frame" && a.size() >= 5) {
        Renderer r;
        r.setAssets(&es, root + "/data");
        r.setCustomImages(build::customImageFiles(env.assetsDir));
        return r.render(sceneAt(story, a[3].toInt(), &es), true).save(a[4]) ? 0 : fail("cannot save " + a[4]);
    }
    return fail("unknown command " + cmd);
}
