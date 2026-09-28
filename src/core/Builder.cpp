#include "Builder.h"
#include "EsAssets.h"
#include "Overlays.h"
#include "Py.h"
#include "PhoneAssets.h"
#include "Text.h"
#include "Wardrobe.h"
#include "Weather.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gb {
namespace build {

namespace {

const char* const kManaged[] = {"images", "audio", "video", "fonts", nullptr};

// the game gets this computer's environment minus the Qt settings of whoever started it (gb_cli draws «offscreen»):
// the game passes its environment on to what a mod runs - GenryBL's own installer is a Qt program too
QProcessEnvironment gameEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& k : env.keys())
        if (k.startsWith(QLatin1String("QT_"), Qt::CaseInsensitive) || k.startsWith(QLatin1String("QML"), Qt::CaseInsensitive)) env.remove(k);
    return env;
}

void say(const BuildLog& log, const QString& s)
{
    if (log) log(s);
}

bool writeUtf8(const QString& path, const QString& text, QString* err)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("не могу записать %1: %2").arg(QDir::toNativeSeparators(path), f.errorString());
        return false;
    }
    const QByteArray b = text.toUtf8();
    return f.write(b) == b.size();
}

bool sameFile(const QString& a, const QString& b)
{
    const QFileInfo fa(a), fb(b);
    return fb.exists() && fa.size() == fb.size() && fb.lastModified() >= fa.lastModified().addSecs(-1);
}

bool copyFile(const QString& src, const QString& dst, QString* err)
{
    if (sameFile(src, dst)) return true;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    QFile::remove(dst);
    if (!QFile::copy(src, dst)) {
        if (err) *err = QStringLiteral("не скопировать %1").arg(QDir::toNativeSeparators(src));
        return false;
    }
    return true;
}

// dst/<sub> mirrors src/<sub>: copy new/changed files, drop ones the project no longer has
int syncTree(const QString& src, const QString& dst, QString* err, bool* ok)
{
    int copied = 0;
    QSet<QString> want;
    if (QDir(src).exists()) {
        QDirIterator it(src, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            const QString rel = QDir(src).relativeFilePath(f);
            want.insert(rel.toLower());
            if (!sameFile(f, dst + QLatin1Char('/') + rel)) {
                if (!copyFile(f, dst + QLatin1Char('/') + rel, err)) { *ok = false; return copied; }
                ++copied;
            }
        }
    }
    if (QDir(dst).exists()) {
        QDirIterator it(dst, QDir::Files, QDirIterator::Subdirectories);
        QStringList stale;
        while (it.hasNext()) {
            const QString f = it.next();
            if (!want.contains(QDir(dst).relativeFilePath(f).toLower())) stale << f;
        }
        for (const QString& f : stale) QFile::remove(f);
    }
    return copied;
}

} // namespace

bool isEsRoot(const QString& dir)
{
    return !dir.isEmpty() && QFileInfo::exists(esExe(dir)) && QFileInfo(dir + QStringLiteral("/game")).isDir();
}

QString esExe(const QString& esRoot) { return esRoot + QStringLiteral("/Everlasting Summer.exe"); }

QString findEsRoot(const QStringList& candidates)
{
    for (const QString& c : candidates)
        if (isEsRoot(c)) return QDir::cleanPath(c);
    return {};
}

QString detectEsRoot()
{
    QStringList steams;
#ifdef Q_OS_WIN
    for (const char* key : {"HKEY_CURRENT_USER\\Software\\Valve\\Steam", "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam",
                            "HKEY_LOCAL_MACHINE\\SOFTWARE\\Valve\\Steam"}) {
        QSettings reg(QString::fromLatin1(key), QSettings::NativeFormat);
        for (const char* v : {"SteamPath", "InstallPath"}) {
            const QString p = reg.value(QString::fromLatin1(v)).toString();
            if (!p.isEmpty() && !steams.contains(QDir::cleanPath(p))) steams << QDir::cleanPath(p);
        }
    }
#endif
    // every library Steam knows: steamapps/libraryfolders.vdf  "path"  "E:\\SteamLibrary"
    QStringList libraries = steams;
    static const QRegularExpression pathRe(QStringLiteral("\"path\"\\s*\"([^\"]+)\""));
    for (const QString& s : steams) {
        QFile f(s + QStringLiteral("/steamapps/libraryfolders.vdf"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString vdf = QString::fromUtf8(f.readAll());
        for (auto m = pathRe.globalMatch(vdf); m.hasNext();) {
            const QString p = QDir::cleanPath(m.next().captured(1).replace(QStringLiteral("\\\\"), QStringLiteral("/")));
            if (!libraries.contains(p)) libraries << p;
        }
    }
    QStringList candidates;
    for (const QString& l : libraries) {
        // the app manifest names the folder (a renamed install still works)
        QFile acf(l + QStringLiteral("/steamapps/appmanifest_331470.acf"));
        if (acf.open(QIODevice::ReadOnly)) {
            static const QRegularExpression dirRe(QStringLiteral("\"installdir\"\\s*\"([^\"]+)\""));
            const auto m = dirRe.match(QString::fromUtf8(acf.readAll()));
            if (m.hasMatch()) candidates << l + QStringLiteral("/steamapps/common/") + m.captured(1);
        }
        candidates << l + QStringLiteral("/steamapps/common/Everlasting Summer");
    }
    // no Steam in the registry (a portable Steam, a copied game): the usual places of every drive
    for (const QFileInfo& d : QDir::drives()) {
        const QString r = d.absolutePath();
        for (const char* sub : {"SteamLibrary/steamapps/common/Everlasting Summer", "Steam/steamapps/common/Everlasting Summer",
                                "Program Files (x86)/Steam/steamapps/common/Everlasting Summer", "Program Files/Steam/steamapps/common/Everlasting Summer",
                                "Games/Everlasting Summer", "Everlasting Summer"})
            candidates << r + QLatin1String(sub);
    }
    return findEsRoot(candidates);
}

QVector<CustomImage> customImages(const QString& assetsDir, const QString& modId)
{
    QVector<CustomImage> out;
    const QString root = assetsDir + QStringLiteral("/images");
    if (!QDir(root).exists()) return out;
    QDirIterator it(root, {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.webp")}, QDir::Files,
                    QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext()) files << it.next();
    files.sort();
    QSet<QString> seen;
    for (const QString& f : files) {
        const QString name = QFileInfo(f).completeBaseName().toLower().simplified();
        if (name.isEmpty() || seen.contains(name) || name == QLatin1String("genry_phone_body")) continue;
        seen.insert(name);
        out.push_back({name, QStringLiteral("mods/%1/images/%2").arg(modId, QDir(root).relativeFilePath(f))});
    }
    return out;
}

QHash<QString, QString> customImageFiles(const QString& assetsDir)
{
    QHash<QString, QString> out;
    const QString root = assetsDir + QStringLiteral("/images");
    if (!QDir(root).exists()) return out;
    QDirIterator it(root, {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.webp")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        out.insert(QFileInfo(f).completeBaseName().toLower().simplified(), f);
    }
    return out;
}

BuildReport install(const BuildEnv& env, const QString& storyText, const CompileOptions& opt, const BuildLog& log)
{
    BuildReport rep;
    if (!isEsRoot(env.esRoot)) {
        rep.error = QStringLiteral("не найдено Бесконечное лето: %1").arg(QDir::toNativeSeparators(env.esRoot));
        return rep;
    }
    QStringList body;
    rep.meta = parseMeta(pySplitLines(stripBom(storyText)), &body, opt);
    if (!hasPlayableBody(body, opt)) {
        rep.error = QStringLiteral("в истории нет ни одной сцены или команды — собирать нечего");
        return rep;
    }
    const QString modId = rep.meta.modId;
    rep.modDir = env.esRoot + QStringLiteral("/game/mods/") + modId;
    // a mod another copy of GenryBL built (a developer build and the public one share the game)
    const QString project = QDir::cleanPath(QFileInfo(env.assetsDir).absolutePath());
    const QString owner = modOwner(env.esRoot, modId);
    if (!owner.isEmpty() && owner.compare(project, Qt::CaseInsensitive) != 0 && QFileInfo::exists(owner + QStringLiteral("/story.txt"))) {
        rep.error = QStringLiteral("мод «%1» в игре собран другой копией GenryBL (%2) — её не трогаю. Поменяй @mod_id в начале истории.")
                        .arg(modId, QDir::toNativeSeparators(owner));
        return rep;
    }
    say(log, QStringLiteral("Компилирую %1…").arg(modId));
    QString cerr;
    const QString rpy = compileStory(rep.meta, body, opt, customImages(env.assetsDir, modId), &cerr);
    if (!cerr.isEmpty()) {
        rep.error = cerr;
        return rep;
    }
    say(log, QStringLiteral("Кладу картинки и звук мода…"));
    bool ok = true;
    for (int i = 0; kManaged[i]; ++i) {
        const QString sub = QString::fromLatin1(kManaged[i]);
        rep.copied += syncTree(env.assetsDir + QLatin1Char('/') + sub, rep.modDir + QLatin1Char('/') + sub, &rep.error, &ok);
        if (!ok) return rep;
    }
    // the generated header always references the phone shell
    if (!copyFile(env.dataDir + QStringLiteral("/mod_assets/genry_phone_body.png"), rep.modDir + QStringLiteral("/images/genry_phone_body.png"), &rep.error))
        return rep;
    // V1 weather particles («погода снег»): soft flakes, drops, leaves… drawn by Weather.cpp
    if (!opt.legacy) {
        const QString fx = rep.modDir + QStringLiteral("/images/genry_fx");
        QDir().mkpath(fx);
        for (const QString& n : weatherParticleNames())
            if (!weatherParticle(n).save(fx + QLatin1Char('/') + n + QStringLiteral(".png"), "PNG")) {
                rep.error = QStringLiteral("не удалось записать ") + QDir::toNativeSeparators(fx + QLatin1Char('/') + n) + QStringLiteral(".png");
                return rep;
            }
        // the camp map's chibi faces («карта … @sl»)
        const QString chibiDir = rep.modDir + QStringLiteral("/images/genry_chibi");
        QDir().mkpath(chibiDir);
        for (const QString& f : QDir(env.dataDir + QStringLiteral("/mod_assets/chibi")).entryList({QStringLiteral("*.png")}, QDir::Files))
            if (!copyFile(env.dataDir + QStringLiteral("/mod_assets/chibi/") + f, chibiDir + QLatin1Char('/') + f, &rep.error)) return rep;
        // Телефон 3.0: body, bubbles, icons… (PhoneAssets.cpp)
        const QString phoneDir = rep.modDir + QStringLiteral("/images/genry_phone");
        QDir().mkpath(phoneDir);
        for (const QString& n : phoneAssetNames())
            if (!phoneAsset(n).save(phoneDir + QLatin1Char('/') + n + QStringLiteral(".png"), "PNG")) {
                rep.error = QStringLiteral("не удалось записать ") + QDir::toNativeSeparators(phoneDir + QLatin1Char('/') + n) + QStringLiteral(".png");
                return rep;
            }
        // румянец / пот / слёзы / мокрая: one PNG per sprite + overlay set the story uses (Overlays.cpp)
        static const QRegularExpression ovDef(QStringLiteral("(?m)^\\s*image (.+ genry_ov_[a-z_]+) = Fixed\\("));
        QStringList ovImages;
        for (auto m = ovDef.globalMatch(rpy); m.hasNext();) ovImages << m.next().captured(1);
        if (!ovImages.isEmpty()) {
            say(log, QStringLiteral("Рисую румянец, пот, слёзы…"));
            static EsAssets own;
            const EsAssets* es = env.es;
            if (!es || !es->ready()) {
                QString err;
                if (!own.ready()) own.load(env.dataDir + QStringLiteral("/es_catalog.json"), env.esRoot, &err);
                es = &own;
            }
            const QString ovDir = rep.modDir + QStringLiteral("/images/genry_ov");
            QDir().mkpath(ovDir);
            for (const QString& image : ovImages) {
                QString base;
                QStringList kinds;
                splitOverlays(image, &base, &kinds);
                const QImage ov = makeOverlay(*es, base, kinds);
                if (ov.isNull()) continue;
                const QString file = ovDir + QLatin1Char('/') + overlayFile(image);
                if (!ov.save(file, "PNG")) {
                    rep.error = QStringLiteral("не удалось записать ") + QDir::toNativeSeparators(file);
                    return rep;
                }
            }
        }
        // the 18+ patch's pictures the mod shows (CG, the old cards): into the mod itself, so every player sees them
        // with or without the patch (from GenryBL's own copy or the subscribed one); Ульяна's frames stay out
        static const QRegularExpression ptFile(QStringLiteral("\"mods/[A-Za-z0-9_]+/images/genry_patch/([^\"/]+)\""));
        QSet<QString> patchFiles;
        for (auto m = ptFile.globalMatch(rpy); m.hasNext();) patchFiles.insert(m.next().captured(1));
        if (!patchFiles.isEmpty()) {
            static EsAssets ownEs;
            const EsAssets* es = env.es;
            if (!es || !es->ready()) {
                QString err;
                if (!ownEs.ready()) ownEs.load(env.dataDir + QStringLiteral("/es_catalog.json"), env.esRoot, &err);
                es = &ownEs;
            }
            say(log, QStringLiteral("Кладу CG из 18+ патча (%1)…").arg(patchFiles.size()));
            for (const EsPatchImage& p : esPatchImages()) {
                const QString name = QFileInfo(p.path).fileName();
                if (!patchFiles.contains(name) || !esPatchInFolder(p)) continue;
                const QByteArray data = es->vfs().read(p.path);
                if (data.isEmpty()) continue;                  // not there at all: the mod shows its dark card
                const QString file = rep.modDir + QStringLiteral("/images/genry_patch/") + name;
                QDir().mkpath(QFileInfo(file).absolutePath());
                QFile f(file);
                if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size()) {
                    rep.error = QStringLiteral("не удалось записать ") + QDir::toNativeSeparators(file);
                    return rep;
                }
            }
        }
        // «гардероб мастерской»: the workshop layers the definitions use, straight from the workshop files
        static const QRegularExpression wrFile(QStringLiteral("\"mods/[A-Za-z0-9_]+/images/genry_wardrobe/([^\"]+)\""));
        QSet<QString> rels;
        for (auto m = wrFile.globalMatch(rpy); m.hasNext();) rels.insert(m.next().captured(1));
        if (!rels.isEmpty()) {
            const Wardrobe* wr = wardrobe();
            if (!wr) {
                rep.error = QStringLiteral("гардероб мастерской ещё не загружен");
                return rep;
            }
            say(log, QStringLiteral("Кладу слои гардероба мастерской (%1)…").arg(rels.size()));
            for (const QString& rel : rels) {
                const QByteArray data = wr->readModFile(rel);
                const QString file = rep.modDir + QStringLiteral("/images/genry_wardrobe/") + rel;
                if (data.isEmpty()) {
                    rep.error = QStringLiteral("не прочитать слой мастерской ") + rel;
                    return rep;
                }
                QDir().mkpath(QFileInfo(file).absolutePath());
                QFile f(file);
                if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size()) {
                    rep.error = QStringLiteral("не удалось записать ") + QDir::toNativeSeparators(file);
                    return rep;
                }
            }
        }
    }

    {
        QFile own(modOwnerFile(env.esRoot, modId));
        if (own.open(QIODevice::WriteOnly | QIODevice::Truncate)) own.write(project.toUtf8());
    }
    rep.modFile = rep.modDir + QLatin1Char('/') + modId + QStringLiteral(".rpy");
    if (QFileInfo(rep.modFile).size() > 0 && !env.backupsDir.isEmpty()) {
        QDir().mkpath(env.backupsDir);
        QFile::copy(rep.modFile, env.backupsDir + QLatin1Char('/') + modId + QStringLiteral(".rpy.") +
                                     QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")) + QStringLiteral(".bak"));
    }
    for (const QString& f : QDir(rep.modDir).entryList(QDir::Files))
        if (f.endsWith(QLatin1String(".rpyc")) || f.startsWith(modId + QStringLiteral(".rpyc."))) QFile::remove(rep.modDir + QLatin1Char('/') + f);
    say(log, QStringLiteral("Пишу %1.rpy…").arg(modId));
    if (!writeUtf8(rep.modFile, rpy, &rep.error)) return rep;
    static const QRegularExpression lab(QStringLiteral("(?m)^label\\s+([A-Za-z0-9_]+)\\s*:"));
    for (auto m = lab.globalMatch(rpy); m.hasNext();) rep.labels << m.next().captured(1);
    rep.ok = true;
    return rep;
}

void removeHook(const QString& esRoot)
{
    QDir(esRoot + QStringLiteral("/game/mods/_genry_v1_preview")).removeRecursively();
}

qint64 runAt(const BuildEnv& env, const QString& modId, const QString& label, QString* err)
{
    const QString hookDir = env.esRoot + QStringLiteral("/game/mods/_genry_v1_preview");
    removeHook(env.esRoot);
    const QString target = label.isEmpty() ? modId : label;
    // label_overrides instead of redefining ES labels: no "label defined twice"
    const QString hook = QStringLiteral(
        "# -*- coding: utf-8 -*-\n"
        "# GenryBL V1 test run hook. Temporary: removed when the game window closes.\n\n"
        "init -1400 python:\n"
        "    try:\n"
        "        config.default_fullscreen = False\n"
        "    except Exception:\n"
        "        pass\n\n"
        "init 999 python:\n"
        "    config.label_overrides[\"splashscreen\"] = \"_genry_v1_autostart\"\n"
        "    config.label_overrides[\"main_menu\"] = \"_genry_v1_autostart\"\n"
        "    try:\n"
        "        config.main_menu_music = None\n"
        "    except Exception:\n"
        "        pass\n\n"
        "label _genry_v1_autostart:\n"
        "    python:\n"
        "        try:\n"
        "            _preferences.fullscreen = False\n"
        "        except Exception:\n"
        "            pass\n"
        "        config.label_overrides.pop(\"main_menu\", None)\n"
        "        config.label_overrides.pop(\"splashscreen\", None)\n"
        "    scene black\n"
        "    $ renpy.block_rollback()\n"
        "    if renpy.has_label(\"%1\"):\n"
        "        jump %1\n"
        "    jump %2\n").arg(target, modId);   // a wrong scene name starts the mod instead of an exception
    QString e;
    if (!writeUtf8(hookDir + QStringLiteral("/_genry_v1_preview.rpy"), hook, &e)) {
        if (err) *err = e;
        return 0;
    }
    QDir().mkpath(env.saveDir);
    qint64 pid = 0;
    QProcess game;
    game.setProgram(esExe(env.esRoot));
    game.setArguments({QStringLiteral("--savedir"), QDir::toNativeSeparators(env.saveDir)});
    game.setWorkingDirectory(env.esRoot);
    game.setProcessEnvironment(gameEnvironment());
    if (!game.startDetached(&pid)) {
        removeHook(env.esRoot);
        if (err) *err = QStringLiteral("Everlasting Summer.exe не запустился");
        return 0;
    }
    return pid;
}

bool isRunning(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!h) return false;
    const DWORD r = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return r == WAIT_TIMEOUT;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

QStringList lint(const QString& esRoot, const QString& modId, QString* err, int timeoutMs)
{
    // the report is megabytes (every workshop mod is linted too): straight to a file, no pipe
    const QString reportPath = QDir::tempPath() + QStringLiteral("/genrybl_lint_%1.txt").arg(QCoreApplication::applicationPid());
    QProcess p;
    p.setWorkingDirectory(esRoot);
    p.setProcessEnvironment(gameEnvironment());
    p.setStandardOutputFile(reportPath);
    p.setStandardErrorFile(reportPath, QIODevice::Append);
    p.start(esExe(esRoot), {esRoot, QStringLiteral("lint")});
    if (!p.waitForStarted(15000)) {
        if (err) *err = QStringLiteral("lint не запустился");
        return {};
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        if (err) *err = QStringLiteral("lint думает слишком долго");
        return {};
    }
    QFile rf(reportPath);
    const QString report = rf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf.readAll()) : QString();
    rf.close();
    QFile::remove(reportPath);
    if (!report.contains(QLatin1String("lint report"))) {
        if (err) *err = QStringLiteral("БЛ не выдал отчёт lint");
        return {};
    }
    QStringList hits;
    const QStringList lines = report.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i)
        if (lines[i].contains(QStringLiteral("mods/") + modId) || lines[i].contains(modId + QStringLiteral(".rpy"))) {
            hits << lines[i].trimmed();
            if (i + 1 < lines.size() && !lines[i + 1].trimmed().isEmpty()) hits << QStringLiteral("    ") + lines[i + 1].trimmed();
        }
    return hits;
}

QString modOwnerFile(const QString& esRoot, const QString& modId)
{
    return esRoot + QStringLiteral("/game/mods/") + modId + QStringLiteral("/.genrybl_owner");
}

QString modOwner(const QString& esRoot, const QString& modId)
{
    QFile f(modOwnerFile(esRoot, modId));
    return f.open(QIODevice::ReadOnly) ? QDir::cleanPath(QString::fromUtf8(f.readAll()).trimmed()) : QString();
}

bool zipMod(const QString& modDir, const QString& zipPath, QString* err)
{
    QFile::remove(zipPath);
    QProcess p;
    p.setWorkingDirectory(QFileInfo(modDir).absolutePath());
    // the owner mark is this computer's business (a local path), it stays out of the shared zip
    p.start(QStringLiteral("tar"), {QStringLiteral("-a"), QStringLiteral("-c"), QStringLiteral("--exclude=.genrybl_owner"), QStringLiteral("-f"),
                                    QDir::toNativeSeparators(zipPath), QFileInfo(modDir).fileName()});
    if (!p.waitForStarted(10000) || !p.waitForFinished(-1) || p.exitCode() != 0) {
        if (err) *err = QStringLiteral("tar не собрал ZIP");
        return false;
    }
    return true;
}

} // namespace build
} // namespace gb
