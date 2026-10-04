#include "Builder.h"
#include "Tr.h"
#include "EsAssets.h"
#include "Overlays.h"
#include "Py.h"
#include "PhoneAssets.h"
#include "Text.h"
#include "Wardrobe.h"
#include "Weather.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <cmath>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QBuffer>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <private/qzipreader_p.h>
#include <private/qzipwriter_p.h>

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

// where the project keeps the statement names of its mod (the game's last .rpyc of it): <project>/genry_names/
QString namesKeep(const BuildEnv& env, const QString& modId)
{
    if (env.assetsDir.isEmpty()) return {};
    return QFileInfo(env.assetsDir).absolutePath() + QStringLiteral("/genry_names/") + modId + QStringLiteral(".rpyc");
}

// the .rpyc is the game's compile of this very .rpy: Ren'Py's own test (renpy/script.py load_appropriate_file) - the
// md5 of the text read with universal newlines sits in the last 16 bytes of the .rpyc
bool rpycMatchesRpy(const QString& rpy, const QString& rpyc)
{
    QFile a(rpy), b(rpyc);
    if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly) || b.size() < 16) return false;
    QByteArray text = a.readAll();
    text.replace("\r\n", "\n").replace('\r', '\n');
    b.seek(b.size() - 16);
    return QCryptographicHash::hash(text, QCryptographicHash::Md5) == b.read(16);
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

// «bg x» / «cg x» of the project: the game shows a picture at its own size (a 4000×3000 photo = its top-left
// corner, 20+ MB for Ren'Py to decode), the preview fits it - so the mod gets the preview's 1920×1080 picture
bool isScenePicture(const QString& file)
{
    const QString base = QFileInfo(file).completeBaseName().toLower();
    return base.startsWith(QLatin1String("bg ")) || base.startsWith(QLatin1String("cg "));
}

bool copySceneFitted(const QString& src, const QString& dst, QString* err)
{
    const QSize want(1920, 1080);
    QImageReader r(src);
    const QSize size = r.size();
    if (!size.isValid() || size == want) return copyFile(src, dst, err);        // unreadable: copied as is (the check says so)
    const QFileInfo fs(src), fd(dst);
    if (fd.exists() && fd.lastModified() >= fs.lastModified() && QImageReader(dst).size() == want) return true;
    QImage img = r.read();
    if (img.isNull()) return copyFile(src, dst, err);
    img = img.scaled(want, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    img = img.copy((img.width() - want.width()) / 2, (img.height() - want.height()) / 2, want.width(), want.height());
    QDir().mkpath(fd.absolutePath());
    const QString ext = fs.suffix().toLower();
    const bool jpg = ext == QLatin1String("jpg") || ext == QLatin1String("jpeg");
    if (!img.save(dst, jpg ? "JPG" : nullptr, jpg ? 92 : -1)) {
        if (err) *err = QStringLiteral("не сохранить %1").arg(QDir::toNativeSeparators(dst));
        return false;
    }
    return true;
}

// dst/<sub> mirrors src/<sub>: copy new/changed files, drop ones the project no longer has
int syncTree(const QString& src, const QString& dst, QString* err, bool* ok, bool fitScenes = false)
{
    int copied = 0;
    QSet<QString> want;
    if (QDir(src).exists()) {
        QDirIterator it(src, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            const QString rel = QDir(src).relativeFilePath(f);
            want.insert(rel.toLower());
            if (fitScenes && isScenePicture(f)) {
                if (!copySceneFitted(f, dst + QLatin1Char('/') + rel, err)) { *ok = false; return copied; }
                continue;
            }
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
        // and the folders that emptied (images/genry_* of builds before GenryBL's files moved to genry/)
        QStringList dirs;
        QDirIterator d(dst, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (d.hasNext()) dirs << d.next();
        std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
        for (const QString& x : dirs) QDir().rmdir(x);
    }
    return copied;
}

} // namespace

bool isEsRoot(const QString& dir)
{
    return !dir.isEmpty() && QFileInfo::exists(esExe(dir)) && QFileInfo(dir + QStringLiteral("/game")).isDir();
}

QString esExe(const QString& esRoot) { return esRoot + QStringLiteral("/Everlasting Summer.exe"); }

QString esRenpyVersion(const QString& esRoot)
{
    // Ren'Py 8 ships Python 3 (lib/py3-*, lib/python3.x); its renpy/__init__.py still names both versions
    const bool py3 = !QDir(esRoot + QStringLiteral("/lib")).entryList({QStringLiteral("py3-*"), QStringLiteral("python3*")}, QDir::Dirs).isEmpty();
    QFile f(esRoot + QStringLiteral("/renpy/__init__.py"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    static const QRegularExpression vt(QStringLiteral("version_tuple\\s*=\\s*\\((\\d+),\\s*(\\d+),\\s*(\\d+)"));
    QString best;
    for (auto m = vt.globalMatch(QString::fromUtf8(f.readAll())); m.hasNext();) {
        const auto x = m.next();
        const bool eight = x.captured(1).toInt() >= 8;
        if (eight == py3) best = x.captured(1) + QLatin1Char('.') + x.captured(2) + QLatin1Char('.') + x.captured(3);
    }
    return best;
}

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

QVector<LintIssue> checkAssets(const QString& assetsDir)
{
    QVector<LintIssue> out;
    auto add = [&](int level, const QString& file, const QString& msg) {
        LintIssue i;
        i.level = level;
        i.file = file;
        i.msg = msg;
        out << i;
    };
    static const QRegularExpression latin(QStringLiteral("[A-Za-z]")), cyr(QStringLiteral("[\\x{0400}-\\x{04FF}]"));
    static const QRegularExpression word(QStringLiteral("[^\\s_.\\-()]+"));
    const QString root = QDir(assetsDir).absolutePath();
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QFileInfo fi(f);
        const QString rel = QDir(root).relativeFilePath(f);
        const QString ext = fi.suffix().toLower();
        // ES Doc «Ren'Py не находит файл»: one Cyrillic «а» inside a Latin word looks right and is not found
        for (auto m = word.globalMatch(fi.completeBaseName()); m.hasNext();) {
            const QString w = m.next().captured(0);
            if (w.contains(latin) && w.contains(cyr)) {
                add(LintIssue::Warning, f, QString::fromUtf8("«%1»: в слове «%2» смешаны латиница и кириллица (лже-буквы) — "
                                                             "игра может не найти файл. Переименуй одними буквами").arg(rel, w));
                break;
            }
        }
        if (rel.startsWith(QLatin1String("images/"))) {
            if (ext != QLatin1String("png") && ext != QLatin1String("jpg") && ext != QLatin1String("jpeg") && ext != QLatin1String("webp")) continue;
            QImageReader r(f);
            const QSize size = r.size();
            if (!r.canRead() || !size.isValid()) {
                add(LintIssue::Error, f, QString::fromUtf8("«%1» не открывается как картинка (битая или не то расширение) — "
                                                           "на ней игра вылетит. Пересохрани в PNG/JPG").arg(rel));
                continue;
            }
            const bool scene = isScenePicture(f);
            if (scene && size != QSize(1920, 1080))
                add(LintIssue::Info, f, QString::fromUtf8("«%1» — %2×%3: в мод пойдёт подогнанной под экран 1920×1080, как в превью")
                                          .arg(rel).arg(size.width()).arg(size.height()));
            else if (!scene && (size.height() > 1500 || size.width() > 3000))
                add(LintIssue::Warning, f, QString::fromUtf8("«%1» — %2×%3: в игре картинка показывается в своём размере и не влезет в экран "
                                                             "(спрайты БЛ — около 1080 в высоту). Уменьши")
                                             .arg(rel).arg(size.width()).arg(size.height()));
            if (fi.size() > 25ll * 1024 * 1024)
                add(LintIssue::Warning, f, QString::fromUtf8("«%1» весит %2 МБ — игра будет долго грузить сцену").arg(rel).arg(fi.size() / (1024 * 1024)));
        } else if (rel.startsWith(QLatin1String("audio/"))) {
            static const QSet<QString> ok{QStringLiteral("ogg"), QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("opus"), QStringLiteral("flac")};
            if (!ok.contains(ext))
                add(LintIssue::Warning, f, QString::fromUtf8("«%1»: игра не играет .%2 — добавь его через «＋ Свой», GenryBL переделает в ogg").arg(rel, ext));
        }
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
        rep.copied += syncTree(env.assetsDir + QLatin1Char('/') + sub, rep.modDir + QLatin1Char('/') + sub, &rep.error, &ok,
                               sub == QLatin1String("images"));
        if (!ok) return rep;
    }
    // GenryBL's own files (genry/) are written anew by every build: nothing stale stays from the last one
    if (!opt.legacy) QDir(rep.modDir + QStringLiteral("/genry")).removeRecursively();
    // the generated header always references the phone shell: V1 keeps it with GenryBL's own files (genry/), an old
    // GenryModEngine project where it always was
    if (!copyFile(env.dataDir + QStringLiteral("/mod_assets/genry_phone_body.png"),
                  rep.modDir + (opt.legacy ? QStringLiteral("/images/genry_phone_body.png") : QStringLiteral("/genry/phone_body.png")), &rep.error))
        return rep;
    // V1 weather particles («погода снег»): soft flakes, drops, leaves… drawn by Weather.cpp
    if (!opt.legacy) {
        const QString fx = rep.modDir + QStringLiteral("/genry/fx");
        QDir().mkpath(fx);
        for (const QString& n : weatherParticleNames())
            if (!weatherParticle(n).save(fx + QLatin1Char('/') + n + QStringLiteral(".png"), "PNG")) {
                rep.error = QStringLiteral("не удалось записать ") + QDir::toNativeSeparators(fx + QLatin1Char('/') + n) + QStringLiteral(".png");
                return rep;
            }
        // «фонарик»: a white veil twice the screen with a soft round hole in the middle (the game tints it to the dark);
        // at zoom 1 it still covers the whole screen with the light in a corner
        if (rpy.contains(QLatin1String("genry/fx/flashlight.png"))) {
            QImage mask(3840, 2160, QImage::Format_ARGB32);
            const double cx = 1920, cy = 1080, r0 = 150, r1 = 330;
            for (int y = 0; y < mask.height(); ++y) {
                QRgb* line = reinterpret_cast<QRgb*>(mask.scanLine(y));
                for (int x = 0; x < mask.width(); ++x) {
                    const double d = std::hypot(x - cx, y - cy);
                    double a = d <= r0 ? 0.0 : d >= r1 ? 1.0 : (d - r0) / (r1 - r0);
                    a = a * a * (3 - 2 * a);                           // smoothstep: no hard edge
                    line[x] = qRgba(255, 255, 255, int(a * 245));
                }
            }
            if (!mask.save(fx + QStringLiteral("/flashlight.png"), "PNG")) {
                rep.error = QStringLiteral("не удалось записать ") + QDir::toNativeSeparators(fx + QStringLiteral("/flashlight.png"));
                return rep;
            }
        }
        // V2.1.2 the transitions from the mods: only the masks this mod names
        for (const QString& t : genryTransitionNames())
            if (rpy.contains(QStringLiteral("genry/fx/") + t + QStringLiteral(".png")) && !transitionMask(t).save(fx + QLatin1Char('/') + t + QStringLiteral(".png"), "PNG")) {
                rep.error = QStringLiteral("не удалось записать ") + QDir::toNativeSeparators(fx + QLatin1Char('/') + t) + QStringLiteral(".png");
                return rep;
            }
        // the camp map's chibi faces («карта … @sl»)
        const QString chibiDir = rep.modDir + QStringLiteral("/genry/chibi");
        QDir().mkpath(chibiDir);
        for (const QString& f : QDir(env.dataDir + QStringLiteral("/mod_assets/chibi")).entryList({QStringLiteral("*.png")}, QDir::Files))
            if (!copyFile(env.dataDir + QStringLiteral("/mod_assets/chibi/") + f, chibiDir + QLatin1Char('/') + f, &rep.error)) return rep;
        // Телефон 3.0: body, bubbles, icons… (PhoneAssets.cpp)
        const QString phoneDir = rep.modDir + QStringLiteral("/genry/phone");
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
            const QString ovDir = rep.modDir + QStringLiteral("/genry/overlays");
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
        static const QRegularExpression ptFile(QStringLiteral("\"mods/[A-Za-z0-9_]+/genry/patch/([^\"/]+)\""));
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
                const QString file = rep.modDir + QStringLiteral("/genry/patch/") + name;
                QDir().mkpath(QFileInfo(file).absolutePath());
                QFile f(file);
                if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size()) {
                    rep.error = QStringLiteral("не удалось записать ") + QDir::toNativeSeparators(file);
                    return rep;
                }
            }
        }
        // «гардероб мастерской»: the workshop layers the definitions use, straight from the workshop files
        static const QRegularExpression wrFile(QStringLiteral("\"mods/[A-Za-z0-9_]+/genry/wardrobe/([^\"]+)\""));
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
                const QString file = rep.modDir + QStringLiteral("/genry/wardrobe/") + rel;
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
    const QString keptNames = namesKeep(env, modId);
    if (!keptNames.isEmpty() && rpycMatchesRpy(rep.modFile, rep.modFile + QLatin1Char('c'))) {
        // the names the game gave the version installed now go to the project before the new text replaces it
        QDir().mkpath(QFileInfo(keptNames).absolutePath());
        QFile::remove(keptNames);
        QFile::copy(rep.modFile + QLatin1Char('c'), keptNames);
    }
    if (QFileInfo(rep.modFile).size() > 0 && !env.backupsDir.isEmpty()) {
        QDir().mkpath(env.backupsDir);
        QFile::copy(rep.modFile, env.backupsDir + QLatin1Char('/') + modId + QStringLiteral(".rpy.") +
                                     QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")) + QStringLiteral(".bak"));
    }
    // V2.1.2 saves live through a mod update. Ren'Py names every statement when it compiles the .rpy and gives the
    // statements that did not change the names they had in the .rpyc it finds there (renpy/script.py merge_names); a
    // save stops at such a name. The .rpyc used to be deleted on every build: each update renamed the whole mod and every
    // save made in it died on load («Couldn't find a place to stop rolling back»). Now it stays as the source of the
    // names - a stale one is harmless, its md5 no longer matches the .rpy, so the game compiles again and only borrows
    // the names - and the project keeps the one shipped last (genry_names/), in case the mod folder is gone.
    for (const QString& f : QDir(rep.modDir).entryList(QDir::Files))
        if (f.startsWith(modId + QStringLiteral(".rpyc."))) QFile::remove(rep.modDir + QLatin1Char('/') + f);
    const QString rpycFile = rep.modFile + QLatin1Char('c');
    if (!QFileInfo::exists(rpycFile) && !keptNames.isEmpty() && QFileInfo::exists(keptNames)) {
        QFile::copy(keptNames, rpycFile);
        say(log, QStringLiteral("Имена строк — из прошлой выкладки (сейвы игроков не сломаются)"));
    }
    say(log, QStringLiteral("Пишу %1.rpy…").arg(modId));
    if (!writeUtf8(rep.modFile, rpy, &rep.error)) return rep;
    // a .rpyc whose .rpy is gone would still run (Ren'Py loads a lone .rpyc): an old file name's code stays out
    for (const QString& f : QDir(rep.modDir).entryList({QStringLiteral("*.rpyc")}, QDir::Files))
        if (!QFileInfo::exists(rep.modDir + QLatin1Char('/') + f.chopped(1))) QFile::remove(rep.modDir + QLatin1Char('/') + f);
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
    // RENPY_LESS_UPDATES: Ren'Py skips the presplash (display/presplash.py) - a check no longer pops the game's
    // «Loading…» window over the user's desktop for two minutes
    QProcessEnvironment env = gameEnvironment();
    env.insert(QStringLiteral("RENPY_LESS_UPDATES"), QStringLiteral("1"));
    p.setProcessEnvironment(env);
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

void removeGate(const QString& esRoot)
{
    QDir(esRoot + QStringLiteral("/game/mods/_genry_gate")).removeRecursively();
}

GameCheck gameCheck(const QString& esRoot, const QString& dataDir, const QStringList& modIds, int timeoutMs)
{
    GameCheck res;
    const QString gate = esRoot + QStringLiteral("/game/mods/_genry_gate");
    removeGate(esRoot);
    QDir().mkpath(gate);
    if (!QFile::copy(dataDir + QStringLiteral("/gate/genry_gate_smoke.rpy"), gate + QStringLiteral("/genry_gate_smoke.rpy"))) {
        res.error = gbTr("не положить проверку в игру: ") + QDir::toNativeSeparators(gate);
        removeGate(esRoot);
        return res;
    }
    const QString tag = QStringLiteral("%1_%2").arg(QCoreApplication::applicationPid()).arg(QDateTime::currentMSecsSinceEpoch());
    const QString lintPath = QDir::tempPath() + QStringLiteral("/genrybl_lint_") + tag + QStringLiteral(".txt");
    const QString smokePath = QDir::tempPath() + QStringLiteral("/genrybl_smoke_") + tag + QStringLiteral(".txt");
    QProcess p;
    p.setWorkingDirectory(esRoot);
    QProcessEnvironment env = gameEnvironment();
    env.insert(QStringLiteral("RENPY_LESS_UPDATES"), QStringLiteral("1"));      // no «Loading…» window for two minutes
    env.insert(QStringLiteral("GENRY_SMOKE_OUT"), QDir::toNativeSeparators(smokePath));
    env.insert(QStringLiteral("GENRY_SMOKE_MODS"), modIds.join(QLatin1Char(',')));
    p.setProcessEnvironment(env);
    p.setStandardOutputFile(lintPath);
    p.setStandardErrorFile(lintPath, QIODevice::Append);
    p.start(esExe(esRoot), {esRoot, QStringLiteral("genry_smoke")});
    const bool started = p.waitForStarted(15000);
    const bool finished = started && p.waitForFinished(timeoutMs);
    if (started && !finished) p.kill();
    removeGate(esRoot);
    auto slurp = [](const QString& path) {
        QFile f(path);
        const QString s = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        f.close();
        QFile::remove(path);
        return s;
    };
    const QString console = slurp(lintPath), smoke = slurp(smokePath);
    res.console = console;
    if (!started) { res.error = gbTr("игра не запустилась для проверки"); return res; }
    if (!finished) { res.error = gbTr("игра проверяет слишком долго"); return res; }
    // «genry_smoke»: ERROR <file> <line> <what> | LINT <file>:<line> <what> (+ LINT+ <the why>) | SMOKE DONE …
    static const QRegularExpression err(QStringLiteral("^ERROR\\s+'?([^'\\s]+)'?\\s+(\\d+)\\s+(.*)$"));
    static const QRegularExpression done(QStringLiteral("^SMOKE DONE (\\d+) statements, (\\d+) screens"));
    bool smokeDone = false;
    for (const QString& raw : smoke.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        const QRegularExpressionMatch m = err.match(line);
        if (m.hasMatch()) {
            QString file = m.captured(1);
            if (file.startsWith(QLatin1String("game/"))) file = file.mid(5);
            res.hits << QStringLiteral("%1:%2: %3").arg(file, m.captured(2), m.captured(3));
            continue;
        }
        if (line.startsWith(QLatin1String("LINT+ "))) {
            res.hits << QStringLiteral("    ") + line.mid(6);
            res.lintHits << res.hits.last();
            continue;
        }
        if (line.startsWith(QLatin1String("LINT "))) {
            QString l = line.mid(5);
            if (l.startsWith(QLatin1String("game/"))) l = l.mid(5);
            res.hits << l;
            res.lintHits << l;
            continue;
        }
        const QRegularExpressionMatch d = done.match(line);
        if (d.hasMatch()) {
            smokeDone = true;
            res.statements = d.captured(1).toInt();
            res.screens = d.captured(2).toInt();
        }
    }
    if (!smokeDone) {
        // the game died while starting (a broken .rpy of some mod): its traceback is the answer
        QString why = console;
        if (why.trimmed().isEmpty()) {
            QFile tb(esRoot + QStringLiteral("/traceback.txt"));
            if (tb.open(QIODevice::ReadOnly)) why = QString::fromUtf8(tb.readAll());
            res.console = why;
        }
        QStringList tail = why.split(QLatin1Char('\n'));
        tail = tail.mid(qMax(0, int(tail.size()) - 14));
        res.error = gbTr("игра не довела проверку до конца:\n") + tail.join(QLatin1Char('\n')).trimmed();
        return res;
    }
    res.ran = true;
    return res;
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

namespace {

// the files of a mod that travel: everything but this computer's own marks
QStringList shippedFiles(const QString& modDir)
{
    QStringList out;
    QDirIterator it(modDir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString rel = QDir(modDir).relativeFilePath(it.next());
        const QString name = QFileInfo(rel).fileName();
        if (name == QLatin1String(".genrybl_owner") || name.endsWith(QLatin1Char('~')) || name.endsWith(QLatin1String(".bak"))) continue;
        out << rel;
    }
    out.sort();
    return out;
}

QString installText(const QString& modId, const QString& modName)
{
    return QString::fromUtf8(
               "%1\n"
               "Мод для «Бесконечного лета». Сделан в GenryBL — конструкторе модов: https://github.com/GenryTheFox0/GenryBL\n\n"
               "КАК УСТАНОВИТЬ\n"
               "1. Открой папку игры: Steam → Библиотека → «Бесконечное лето» → правой кнопкой → Управление →\n"
               "   Просмотреть локальные файлы.\n"
               "2. Зайди в папку game, потом в mods.\n"
               "3. Распакуй туда папку «%2» из этого архива — чтобы вышло game\\mods\\%2\\…\n"
               "4. Запусти игру → «Моды» → «%1».\n\n"
               "Удалить — просто удали папку game\\mods\\%2.\n")
        .arg(modName.isEmpty() ? modId : modName, modId);
}

}   // namespace

QStringList missingModFiles(const QString& modDir, const QString& modId)
{
    QStringList missing;
    const QRegularExpression ref(QStringLiteral("[\"']mods/%1/([^\"'\\\\]+)[\"']").arg(QRegularExpression::escape(modId)));
    QDirIterator it(modDir, {QStringLiteral("*.rpy")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile f(it.next());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString text = QString::fromUtf8(f.readAll());
        for (auto m = ref.globalMatch(text); m.hasNext();) {
            const QString rel = m.next().captured(1);
            // made at run time («…/%s.png», «[name]»): not a file name
            if (rel.contains(QLatin1Char('%')) || rel.contains(QLatin1Char('[')) || rel.contains(QLatin1Char('{'))) continue;
            if (!QFileInfo::exists(modDir + QLatin1Char('/') + rel) && !missing.contains(rel)) missing << rel;
        }
    }
    missing.sort();
    return missing;
}

bool installModArchive(const QString& zipPath, const QString& modsDir, const QString& expectFolder, QString* folder, QString* err)
{
    auto fail = [err](const QString& m) { if (err) *err = m; return false; };
    QZipReader zip(zipPath);
    if (!zip.isReadable()) return fail(QStringLiteral("the archive does not open"));
    const QVector<QZipReader::FileInfo> all = zip.fileInfoList();
    // the root: «<mod>/…» (the players' archive) or «mods/<mod>/…» (a Workshop folder); files at the top are notes
    QString strip;
    for (const QZipReader::FileInfo& f : all)
        if (QDir::fromNativeSeparators(f.filePath).startsWith(QLatin1String("mods/"))) { strip = QStringLiteral("mods/"); break; }
    QSet<QString> tops;
    for (const QZipReader::FileInfo& f : all) {
        const QString p = QDir::fromNativeSeparators(f.filePath);
        if (p.split(QLatin1Char('/')).contains(QStringLiteral("..")) || p.startsWith(QLatin1Char('/')) || p.contains(QLatin1Char(':')))
            return fail(QStringLiteral("the archive has a bad path: ") + p);
        if (!p.startsWith(strip)) continue;
        const QString rest = p.mid(strip.size());
        if (rest.contains(QLatin1Char('/'))) tops.insert(rest.section(QLatin1Char('/'), 0, 0));
    }
    QString mod = expectFolder;
    if (mod.isEmpty() || !tops.contains(mod)) {
        if (tops.size() != 1) return fail(QStringLiteral("the archive has no single mod folder"));
        mod = *tops.begin();
    }
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    if (!safe.match(mod).hasMatch() || mod.startsWith(QLatin1Char('.'))) return fail(QStringLiteral("a strange mod folder name: ") + mod);
    const QString target = modsDir + QLatin1Char('/') + mod;
    if (QFileInfo::exists(target)) {
        if (!QFileInfo::exists(target + QStringLiteral("/.genrybl_catalog"))) return fail(QStringLiteral("game/mods/") + mod + QStringLiteral(" is there already and not from the catalog"));
        QDir(target).removeRecursively();
    }
    const QString prefix = strip + mod + QLatin1Char('/');
    // the mark goes in first: a install cut short stays the catalog's own, so the next try may replace it
    QDir().mkpath(target);
    QFile mark(target + QStringLiteral("/.genrybl_catalog"));
    if (mark.open(QIODevice::WriteOnly)) mark.write("1\n");
    mark.close();
    for (const QZipReader::FileInfo& f : all) {
        const QString p = QDir::fromNativeSeparators(f.filePath);
        if (!p.startsWith(prefix)) continue;
        const QString out = target + QLatin1Char('/') + p.mid(prefix.size());
        if (f.isDir) { QDir().mkpath(out); continue; }
        if (!f.isFile) continue;
        const QByteArray data = zip.fileData(f.filePath);
        QDir().mkpath(QFileInfo(out).absolutePath());
        QFile o(out);
        if ((data.isEmpty() && f.size > 0) || !o.open(QIODevice::WriteOnly) || o.write(data) < 0) {
            o.close();
            QDir(target).removeRecursively();
            return fail(QStringLiteral("the archive is damaged: ") + p);
        }
    }
    if (folder) *folder = mod;
    return true;
}

bool exportZip(const QString& modDir, const QString& modId, const QString& modName, const QString& zipPath, QString* err)
{
    const QStringList files = shippedFiles(modDir);
    if (files.isEmpty()) {
        if (err) *err = QStringLiteral("папка мода пустая: %1").arg(QDir::toNativeSeparators(modDir));
        return false;
    }
    QDir().mkpath(QFileInfo(zipPath).absolutePath());
    const QString tmp = zipPath + QStringLiteral(".part");
    QFile::remove(tmp);
    {
        QZipWriter zip(tmp);
        if (zip.status() != QZipWriter::NoError) {
            if (err) *err = QStringLiteral("не могу записать %1").arg(QDir::toNativeSeparators(zipPath));
            return false;
        }
        zip.setCompressionPolicy(QZipWriter::AutoCompress);      // pictures/music are stored, text is packed
        zip.addFile(QString::fromUtf8("КАК_УСТАНОВИТЬ.txt"), installText(modId, modName).toUtf8());
        for (const QString& rel : files) {
            QFile f(modDir + QLatin1Char('/') + rel);
            if (!f.open(QIODevice::ReadOnly)) {
                if (err) *err = QStringLiteral("не читается %1").arg(QDir::toNativeSeparators(f.fileName()));
                return false;
            }
            zip.addFile(modId + QLatin1Char('/') + rel, f.readAll());
        }
        zip.close();
        if (zip.status() != QZipWriter::NoError) {
            if (err) *err = QStringLiteral("архив не дописался (место на диске?)");
            return false;
        }
    }
    // read it back: every file there, every byte the same
    {
        QZipReader back(tmp);
        if (!back.isReadable() || back.count() != files.size() + 1) {
            if (err) *err = QStringLiteral("архив не читается после записи");
            return false;
        }
        for (const QString& rel : files) {
            QFile f(modDir + QLatin1Char('/') + rel);
            f.open(QIODevice::ReadOnly);
            if (back.fileData(modId + QLatin1Char('/') + rel) != f.readAll()) {
                if (err) *err = QStringLiteral("в архиве испортился %1").arg(rel);
                return false;
            }
        }
    }
    QFile::remove(zipPath);
    if (!QFile::rename(tmp, zipPath)) {
        if (err) *err = QStringLiteral("не могу переименовать в %1 (открыт в другой программе?)").arg(QDir::toNativeSeparators(zipPath));
        return false;
    }
    return true;
}

QString scalePixels(const QString& line, double k)
{
    auto num = [k](const QString& v) { return QString::number(qRound(v.toInt() * k)); };
    QString out = line;
    // «xpos 300», «size 36» (screen language and ATL): whole numbers only - «xpos 0.5» is a share of the screen
    static const QRegularExpression prop(QStringLiteral(
        "\\b(xpos|ypos|xoffset|yoffset|xsize|ysize|xmaximum|ymaximum|xminimum|yminimum|spacing|size|text_size|xpadding|ypadding|"
        "left_padding|right_padding|top_padding|bottom_padding|xmargin|ymargin|left_margin|right_margin|top_margin|bottom_margin|"
        "xanchor|yanchor|first_spacing|line_spacing|outline|radius)(\\s+|=)(-?\\d+)(?![\\d.])"));
    QString res;
    int last = 0;
    for (auto it = prop.globalMatch(out); it.hasNext();) {
        const auto m = it.next();
        res += out.mid(last, m.capturedStart(3) - last) + num(m.captured(3));
        last = int(m.capturedEnd(3));
    }
    out = res + out.mid(last);
    // «xysize (1920, 1080)», «pos (40, 120)», «padding (22, 12)», «offset (4, 4)», «hotspot (x, y, w, h)»
    static const QRegularExpression tup(QStringLiteral("\\b(xysize|pos|padding|offset|margin|hotspot|xycenter)\\s*\\(([-\\d,\\s]+)\\)"));
    res.clear();
    last = 0;
    for (auto it = tup.globalMatch(out); it.hasNext();) {
        const auto m = it.next();
        QStringList parts = m.captured(2).split(QLatin1Char(','));
        for (QString& p : parts) {
            const QString t = p.trimmed();
            if (!t.isEmpty()) p = (p.startsWith(QLatin1Char(' ')) ? QStringLiteral(" ") : QString()) + num(t);
        }
        res += out.mid(last, m.capturedStart(2) - last) + parts.join(QLatin1Char(','));
        last = int(m.capturedEnd(2));
    }
    return res + out.mid(last);
}

bool exportAndroid(const QString& modDir, const QString& modId, const QString& modName, const QString& zipPath, QString* err, QStringList* notes)
{
    const double k = 2.0 / 3.0;
    // the game compiled .rpyc from the full-size code here: the phone compiles the scaled .rpy itself
    QStringList files;
    for (const QString& rel : shippedFiles(modDir))
        if (!rel.endsWith(QLatin1String(".rpyc"))) files << rel;
    if (files.isEmpty()) {
        if (err) *err = QStringLiteral("папка мода пустая: %1").arg(QDir::toNativeSeparators(modDir));
        return false;
    }
    // Latin names: an old phone's file system and the mod loaders stumble on Cyrillic
    QHash<QString, QString> renamed;
    QSet<QString> taken;
    for (const QString& rel : files) {
        QString to = rel;
        bool ascii = true;
        for (const QChar ch : rel) if (ch.unicode() > 127) { ascii = false; break; }
        if (!ascii) {
            QStringList parts = rel.split(QLatin1Char('/'));
            for (QString& p : parts) {
                const QString suffix = p.contains(QLatin1Char('.')) ? p.mid(p.lastIndexOf(QLatin1Char('.'))) : QString();
                const QString base = suffix.isEmpty() ? p : p.left(p.size() - suffix.size());
                p = translit(base) + suffix;
            }
            to = parts.join(QLatin1Char('/'));
        }
        QString unique = to;
        for (int n = 2; taken.contains(unique.toLower()); ++n) unique = to.left(to.lastIndexOf(QLatin1Char('.'))) + QStringLiteral("_%1").arg(n) + to.mid(to.lastIndexOf(QLatin1Char('.')));
        taken.insert(unique.toLower());
        if (unique != rel) renamed.insert(rel, unique);
    }
    QDir().mkpath(QFileInfo(zipPath).absolutePath());
    const QString tmp = zipPath + QStringLiteral(".part");
    QFile::remove(tmp);
    bool usesWardrobe = false, usesMap = false, usesVideo = false;
    {
        QZipWriter zip(tmp);
        if (zip.status() != QZipWriter::NoError) {
            if (err) *err = QStringLiteral("не могу записать %1").arg(QDir::toNativeSeparators(zipPath));
            return false;
        }
        zip.setCompressionPolicy(QZipWriter::AutoCompress);
        const QString readme = QString::fromUtf8(
            "%1 — версия для Андроида\n"
            "Мод для мобильного «Бесконечного лета». Сделан в GenryBL: https://github.com/GenryTheFox0/GenryBL\n\n"
            "КАК УСТАНОВИТЬ\n"
            "1. Распакуй архив.\n"
            "2. Папку «mods» целиком положи в память телефона:\n"
            "   Android/media/su.sovietgames.everlasting_summer/  (новые версии игры)\n"
            "   или Android/data/su.sovietgames.everlasting_summer/files/  (старые версии)\n"
            "3. В игре: Настройки → Моды и сценарии → «%1».\n\n"
            "Картинки и разметка уменьшены под экран мобильной игры (1280×720).\n").arg(modName.isEmpty() ? modId : modName);
        zip.addFile(QString::fromUtf8("КАК_УСТАНОВИТЬ_ANDROID.txt"), readme.toUtf8());
        for (const QString& rel : files) {
            QFile f(modDir + QLatin1Char('/') + rel);
            if (!f.open(QIODevice::ReadOnly)) {
                if (err) *err = QStringLiteral("не читается %1").arg(QDir::toNativeSeparators(f.fileName()));
                return false;
            }
            QByteArray data = f.readAll();
            const QString ext = QFileInfo(rel).suffix().toLower();
            if (ext == QLatin1String("rpy")) {
                QString text = QString::fromUtf8(data);
                usesWardrobe = usesWardrobe || text.contains(QLatin1String("genry/wardrobe/"));
                usesMap = usesMap || text.contains(QLatin1String("genry_camp_map")) || text.contains(QLatin1String("genry_map_pic("));
                usesVideo = usesVideo || text.contains(QLatin1String("renpy.movie_cutscene")) || text.contains(QLatin1String("Movie("));
                for (auto it = renamed.begin(); it != renamed.end(); ++it)
                    text.replace(QStringLiteral("mods/%1/%2").arg(modId, it.key()), QStringLiteral("mods/%1/%2").arg(modId, it.value()));
                QStringList lines = text.split(QLatin1Char('\n'));
                for (QString& l : lines)
                    if (!l.trimmed().startsWith(QLatin1Char('#'))) l = scalePixels(l, k);
                data = (QStringLiteral("# GenryBL: версия для мобильного Бесконечного лета (1280x720)\n") + lines.join(QLatin1Char('\n'))).toUtf8();
            } else if (ext == QLatin1String("png") || ext == QLatin1String("jpg") || ext == QLatin1String("jpeg") || ext == QLatin1String("webp")) {
                QImage img;
                if (img.loadFromData(data) && img.width() >= 48 && img.height() >= 48) {
                    const QImage shrunk = img.scaled(qMax(1, qRound(img.width() * k)), qMax(1, qRound(img.height() * k)), Qt::IgnoreAspectRatio,
                                                    Qt::SmoothTransformation);
                    QBuffer buf;
                    buf.open(QIODevice::WriteOnly);
                    if (shrunk.save(&buf, ext == QLatin1String("png") ? "PNG" : ext == QLatin1String("webp") ? "WEBP" : "JPG", ext == QLatin1String("png") ? -1 : 90))
                        data = buf.data();
                }
            }
            zip.addFile(QStringLiteral("mods/%1/%2").arg(modId, renamed.value(rel, rel)), data);
        }
        zip.close();
        if (zip.status() != QZipWriter::NoError) {
            if (err) *err = QStringLiteral("архив не дописался (место на диске?)");
            return false;
        }
    }
    {
        QZipReader back(tmp);
        if (!back.isReadable() || back.count() != files.size() + 1) {
            if (err) *err = QStringLiteral("архив не читается после записи");
            return false;
        }
    }
    QFile::remove(zipPath);
    if (!QFile::rename(tmp, zipPath)) {
        if (err) *err = QStringLiteral("не могу переименовать в %1 (открыт в другой программе?)").arg(QDir::toNativeSeparators(zipPath));
        return false;
    }
    if (notes) {
        if (usesWardrobe) *notes << gbTr("В моде одежда из Мастерской: проверь героинь на телефоне — у мобильной игры свои спрайты");
        if (usesMap) *notes << gbTr("В моде карта лагеря: проверь на телефоне, что места нажимаются куда надо");
        if (usesVideo) *notes << gbTr("В моде видео: мобильная игра играет не все форматы — проверь ролики на телефоне");
    }
    return true;
}

bool exportWorkshopFolder(const QString& modDir, const QString& modId, const QString& modName, const QString& dest, QString* err)
{
    const QString target = dest + QStringLiteral("/mods/") + modId;
    QDir(target).removeRecursively();                 // our own export folder: always the mod as it is now
    for (const QString& rel : shippedFiles(modDir)) {
        const QString to = target + QLatin1Char('/') + rel;
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!QFile::copy(modDir + QLatin1Char('/') + rel, to)) {
            if (err) *err = QStringLiteral("не удалось скопировать %1").arg(rel);
            return false;
        }
    }
    // the Workshop wants a .rpyc next to every .rpy (the game's lint made them)
    QStringList noRpyc;
    QDirIterator it(target, {QStringLiteral("*.rpy")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        if (!QFileInfo::exists(f + QLatin1Char('c'))) noRpyc << QDir(target).relativeFilePath(f);
    }
    if (!noRpyc.isEmpty()) {
        if (err) *err = QStringLiteral("нет .rpyc для: %1 — Мастерская такой мод не примет").arg(noRpyc.join(QStringLiteral(", ")));
        return false;
    }
    QFile how(dest + QString::fromUtf8("/КАК_ВЫЛОЖИТЬ.txt"));
    if (how.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        how.write(QString::fromUtf8(
                      "%1 — папка для Мастерской Steam\n\n"
                      "1. Открой загрузчик самой игры: <папка игры>\\game\\mods\\ES_Content_Uploader.exe\n"
                      "   (Steam должен быть запущен под твоим аккаунтом, VPN лучше выключить).\n"
                      "2. «Перейти к списку предметов» → создай новый предмет.\n"
                      "3. Основная папка — ЭТА папка (в ней лежит mods). Обложка подхватится сама из preview.jpg.\n"
                      "4. Название, описание, теги — как хочешь. «Загрузить без исходников» НЕ ставь.\n"
                      "5. Сначала доступность «По ссылке», проверь сам (подпишись, запусти), потом — «Публичный».\n\n"
                      "Не держи копию мода в game\\mods, если подписан на него в Мастерской: одинаковые метки — игра упадёт.\n")
                      .arg(modName.isEmpty() ? modId : modName)
                      .toUtf8());
    }
    return true;
}

} // namespace build
} // namespace gb
