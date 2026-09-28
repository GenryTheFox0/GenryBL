// gb_cli - GenryBL V1 without the UI.
//   gb_cli compile <story.txt> [--legacy]          print the generated .rpy
//   gb_cli install <story.txt>                     put the mod into Everlasting Summer
//   gb_cli lint    <story.txt>                     install, then run ES's own Ren'Py lint on it
//   gb_cli check   <story.txt>                     GenryBL's own story checks (no game start)
//   gb_cli run     <story.txt> [label]             install and start ES at the label
//   gb_cli frame   <story.txt> <line> <out.png>    render what the player sees at that line
//   gb_cli chibis  <any story.txt>                 (re)draw data/mod_assets/chibi/*.png from the ES sprites
//   gb_cli wardrobe <any story.txt> <tag | "image"> [out.png]   the workshop wardrobe (--no-wardrobe skips loading it)
#include "Builder.h"
#include "Cinema.h"
#include "EsAssets.h"
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
#include <QRegularExpression>
#include <QTextStream>

using namespace gb;

static QTextStream out(stdout);

static int fail(const QString& m)
{
    out << "ERROR: " << m << "\n";
    return 1;
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() < 3) {
        out << "usage: gb_cli compile|install|lint|run|frame <story.txt> ...\n";
        return 2;
    }
    QFile f(a[2]);
    if (!f.open(QIODevice::ReadOnly)) return fail("cannot read " + a[2]);
    const QString story = QString::fromUtf8(f.readAll());
    const QString root = QStringLiteral(GB_SOURCE_DIR);
    static EsAssets es;
    QString err;
    const QString esRoot = qEnvironmentVariable("GENRYBL_ES").isEmpty() ? build::detectEsRoot() : qEnvironmentVariable("GENRYBL_ES");
    Vfs::setBundled(root + "/data/patch/i8_data.rpa", QLatin1String(EsAssets::kHentaiPatchId));
    const bool esOk = es.load(root + "/data/es_catalog.json", esRoot, &err);
    CompileOptions opt;
    opt.legacy = a.contains("--legacy");
    if (esOk) for (auto it = es.characters.begin(); it != es.characters.end(); ++it) opt.knownSpeakers.insert(it.key());
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
    env.assetsDir = QFileInfo(a[2]).absolutePath() + "/assets";
    env.backupsDir = root + "/work/backups";
    env.saveDir = root + "/work/es_saves";
    if (cmd == "install" || cmd == "lint" || cmd == "run" || cmd == "export") {
        const BuildReport r = build::install(env, story, opt, [](const QString& s) { out << "  " << s << "\n"; out.flush(); });
        if (!r.ok) return fail(r.error);
        out << "installed " << r.modFile << " labels: " << r.labels.join(' ') << "\n";
        if (cmd == "lint") {
            out << "running Everlasting Summer lint...\n";
            out.flush();
            const QStringList hits = build::lint(esRoot, r.meta.modId, &err, 600000);
            if (!err.isEmpty()) return fail(err);
            if (hits.isEmpty()) out << "LINT CLEAN: no report lines mention " << r.meta.modId << "\n";
            for (const QString& h : hits) out << "  " << h << "\n";
            return hits.isEmpty() ? 0 : 1;
        }
        if (cmd == "export") {
            // gb_cli export <story.txt> zip|workshop <out .zip | out folder>: what the «Экспорт» button does
            out << "running Everlasting Summer lint...\n";
            out.flush();
            const QStringList hits = build::lint(esRoot, r.meta.modId, &err, 600000);
            if (!err.isEmpty()) return fail(err);
            if (!hits.isEmpty()) {
                for (const QString& h : hits) out << "  " << h << "\n";
                return fail("the game's lint is not clean - no export");
            }
            const QStringList missing = build::missingModFiles(r.modDir, r.meta.modId);
            if (!missing.isEmpty()) return fail("not inside the mod: " + missing.join(", "));
            const QString kind = a.value(3), dest = a.value(4);
            const bool ok = kind == "workshop" ? build::exportWorkshopFolder(r.modDir, r.meta.modId, r.meta.modName, dest, &err)
                                               : build::exportZip(r.modDir, r.meta.modId, r.meta.modName, dest, &err);
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
