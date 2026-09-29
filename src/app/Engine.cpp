#include "Engine.h"
#include "CrashCatcher.h"
#include "Screenplay.h"
#include "Compiler.h"
#include "Lint.h"
#include "Py.h"
#include "Text.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QImage>
#include <QJSEngine>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace gb;

Engine* Engine::s_instance = nullptr;

namespace {

QString U(const char* s) { return QString::fromUtf8(s); }

QByteArray readFile(const QString& p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeFile(const QString& p, const QByteArray& b)
{
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(b) == b.size();
}

QString localPath(const QString& fileUrl)
{
    const QUrl u(fileUrl);
    return u.isLocalFile() ? u.toLocalFile() : fileUrl;
}

QVariantMap issueMap(const LintIssue& i)
{
    return {{QStringLiteral("line"), i.line}, {QStringLiteral("level"), i.level}, {QStringLiteral("msg"), i.msg}, {QStringLiteral("file"), i.file},
            {QStringLiteral("col"), i.col}, {QStringLiteral("len"), i.len}};
}

// the looks of a mod's line in ES «Моды и пользовательские сценарии» (fonts: game/fonts with Cyrillic; kis/kisi have none)
struct TitleLook { const char* name; const char* font; const char* color; int size; const char* style; };
const TitleLook kTitleLooks[] = {
    {"Пионерский", "es:fonts/corbelb.ttf", "#8b1a1a", 40, ""},
    {"Чернила", "es:fonts/timesbi.ttf", "#24407a", 42, ""},
    {"Лагерная табличка", "es:fonts/gothic.TTF", "#1d5f63", 38, ""},
    {"Пиксель", "es:fonts/PressStart2P.ttf", "#5b2a86", 24, ""},
    {"Книжный", "es:fonts/DejaVuSerif.ttf", "#4d2e19", 38, "b"},
    {"Закат", "es:fonts/calibriz.ttf", "#a44a00", 40, ""},
    {"Лес", "es:fonts/kisb.TTF", "#2e5d34", 40, ""},
    {"Нуар", "es:fonts/timesi.ttf", "#1a1a1a", 42, ""},
    {"Горн", "es:fonts/corbelz.ttf", "#b3001b", 40, ""},
    {"Ночь", "es:fonts/DejaVuSansOblique.ttf", "#24407a", 36, ""},
};

QString starter(const QString& root, const QString& id, const QString& name, const QString& author, bool example)
{
    // a clean page with a hint in comments; the Славя / Алиса sample only when asked for
    QString s = QString::fromUtf8(readFile(root + (example ? QStringLiteral("/data/starter_story.txt") : QStringLiteral("/data/starter_blank.txt"))));
    if (s.isEmpty() && !example) s = QString::fromUtf8(readFile(root + QStringLiteral("/data/starter_story.txt")));
    s.replace(QStringLiteral("@@ID@@"), id).replace(QStringLiteral("@@NAME@@"), name);
    // the author the maker gave last time («Название мода» → Автор); nobody yet = no @author line at all
    if (author.isEmpty()) s.replace(QRegularExpression(QStringLiteral("^@author @@AUTHOR@@\r?\n"), QRegularExpression::MultilineOption), QString());
    s.replace(QStringLiteral("@@AUTHOR@@"), author);
    // its own look in the game's mod list from the first minute (the old constructor's mods stood out there too)
    const TitleLook& look = kTitleLooks[qHash(id) % (sizeof(kTitleLooks) / sizeof(kTitleLooks[0]))];
    QString lines = QStringLiteral("@mod_title_font %1\n@mod_title_color %2\n@mod_title_size %3\n").arg(QLatin1String(look.font), QLatin1String(look.color)).arg(look.size);
    if (*look.style) lines += QStringLiteral("@mod_title_style %1\n").arg(QLatin1String(look.style));
    const int nameEnd = int(s.indexOf(QLatin1Char('\n'), s.indexOf(QStringLiteral("@mod_name"))));
    if (nameEnd > 0) s.insert(nameEnd + 1, lines);
    return s;
}

#ifdef Q_OS_WIN
void killPid(qint64 pid)
{
    if (HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(pid))) {
        TerminateProcess(h, 0);
        WaitForSingleObject(h, 3000);
        CloseHandle(h);
    }
}
#else
void killPid(qint64) {}
#endif

} // namespace

// ================================================================== boot

Engine::Engine(QObject* parent)
    : QObject(parent)
    , m_settings(QCoreApplication::applicationDirPath() + QStringLiteral("/../work/settings.ini"), QSettings::IniFormat)
{
    s_instance = this;
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 4 && m_root.isEmpty(); ++i) {
        if (QFileInfo::exists(d.filePath(QStringLiteral("data/es_catalog.json")))) m_root = d.absolutePath();
        else if (!d.cdUp()) break;
    }
    if (m_root.isEmpty()) {
        m_startupError = QStringLiteral("Не найдена папка data рядом с программой.");
        return;
    }
    // the installer / uninstaller: no projects - just the files, dressed in the game's own art when it is found
    if (!mode().isEmpty()) {
        QString es = m_settings.value(QStringLiteral("esRoot")).toString();     // the uninstaller: the installed settings
        if (!build::isEsRoot(es)) es = build::detectEsRoot();
        if (!es.isEmpty()) loadEsArt(es);
        return;
    }
    // --force-setup: the first start as if the game was not found (the UI self-check)
    if (QCoreApplication::arguments().contains(QStringLiteral("--force-setup"))) {
        m_needsEs = true;
        return;
    }
    // the game: where the setup put it, else where Steam has it on this computer, else the first start asks
    QString esRoot = m_settings.value(QStringLiteral("esRoot")).toString();
    if (!build::isEsRoot(esRoot)) esRoot = build::detectEsRoot();
    if (esRoot.isEmpty()) {
        m_needsEs = true;
        return;
    }
    start(esRoot);
}

QString Engine::detectEs() const { return build::detectEsRoot(); }

QString Engine::useEsRoot(const QString& folder)
{
    // the user may point at the game folder, its game/ subfolder or the exe itself
    QString dir = QDir::cleanPath(localPath(folder));
    if (QFileInfo(dir).isFile()) dir = QFileInfo(dir).absolutePath();
    if (!build::isEsRoot(dir) && dir.endsWith(QLatin1String("/game"), Qt::CaseInsensitive)) dir = QFileInfo(dir).absolutePath();
    if (!build::isEsRoot(dir))
        return U("Тут нет «Бесконечного лета»: нужна папка, где лежат Everlasting Summer.exe и папка game");
    m_settings.setValue(QStringLiteral("esRoot"), dir);
    if (m_ready) return {};                   // changed later: the next start takes it
    m_startupError.clear();
    return start(dir) ? QString() : m_startupError;
}

bool Engine::start(const QString& esRoot)
{
    QString err;
    // the 18+ patch ships inside (data/patch): it works without a subscription, a subscribed one wins
    gb::Vfs::setBundled(m_root + QStringLiteral("/data/patch/i8_data.rpa"), QLatin1String(gb::EsAssets::kHentaiPatchId));
    if (!m_es.load(m_root + QStringLiteral("/data/es_catalog.json"), esRoot, &err)) {
        m_startupError = err;
        emit readyChanged();
        return false;
    }
    m_renderer.setAssets(&m_es, m_root + QStringLiteral("/data"));
    if (!m_forms.load(m_root + QStringLiteral("/data/forms.json"), &err)) {
        m_startupError = err;
        emit readyChanged();
        return false;
    }
    m_settings.setValue(QStringLiteral("esRoot"), esRoot);
    m_needsEs = false;
    QDir().mkpath(m_root + QStringLiteral("/projects"));
    m_ready = true;
    build::removeHook(esRoot);            // a leftover from a crashed test run
    m_watch.setInterval(1500);
    connect(&m_watch, &QTimer::timeout, this, [this] {
        if (m_gamePid && !build::isRunning(m_gamePid)) {
            m_gamePid = 0;
            m_watch.stop();
            build::removeHook(m_es.esRoot());
            emit gameRunningChanged();
            emit toast(QStringLiteral("Игра закрыта"), 0);
        }
    });
    refreshProjects();
    {
        auto readHidden = [](const QString& file, QSet<QString>& into) {
            QFile f(file);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                for (const QString& l : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n')))
                    if (!l.trimmed().isEmpty()) into.insert(l.trimmed());
        };
        auto readShifts = [](const QString& file, QHash<QString, QPoint>& into) {
            QFile f(file);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                for (const QString& l : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
                    const QStringList c = l.trimmed().split(QLatin1Char('\t'));
                    if (c.size() == 3) into.insert(c[0], QPoint(c[1].toInt(), c[2].toInt()));
                }
        };
        if (listDir() != m_root + QStringLiteral("/data")) {
            readHidden(m_root + QStringLiteral("/data/wardrobe_hidden.txt"), m_hiddenShipped);
            readShifts(m_root + QStringLiteral("/data/wardrobe_faces.txt"), m_faceShiftsShipped);
        }
        readHidden(listDir() + QStringLiteral("/wardrobe_hidden.txt"), m_hidden);
        readShifts(listDir() + QStringLiteral("/wardrobe_faces.txt"), m_faceShifts);
        m_wardrobe.setHidden(m_hidden + m_hiddenShipped);
        applyFaceShifts();
    }
    libraryScan();                        // the workshop library + wardrobe load in the background
    emit readyChanged();
    emit assetsChanged();
    return true;
}

QVariantList Engine::patchImages() const
{
    QVariantList out;
    for (const EsPatchImage& p : esPatchImages()) {
        if (!esPatchInFolder(p)) continue;
        out << QVariantMap{{QStringLiteral("id"), p.name.mid(3)}, {QStringLiteral("name"), p.name},
                           {QStringLiteral("card"), p.path.contains(QLatin1String("/cards/"))},
                           {QStringLiteral("have"), m_es.images.contains(p.name)}};
    }
    return out;
}

QVariantList Engine::patchSprites() const
{
    // the game's own «body» outfit: with the patch it is the old (uncensored) body
    QVariantList out;
    for (const QString& tag : m_es.spriteTags())
        for (const QString& n : m_es.spriteNames(tag))
            if (n.endsWith(QLatin1String(" body")) && tag != QLatin1String("us"))
                out << QVariantMap{{QStringLiteral("tag"), tag}, {QStringLiteral("name"), n}, {QStringLiteral("image"), tag + QLatin1Char(' ') + n}};
    return out;
}

Engine* Engine::boot() { return s_instance ? s_instance : new Engine; }

Engine* Engine::create(QQmlEngine*, QJSEngine*)
{
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

CompileOptions Engine::options() const
{
    CompileOptions o;
    for (auto it = m_es.characters.begin(); it != m_es.characters.end(); ++it) o.knownSpeakers.insert(it.key());
    o.knownSpeakers << QStringLiteral("narrator") << QStringLiteral("th") << QStringLiteral("genry") << QStringLiteral("scar");
    return o;
}

// ================================================================== tables for QML

// The palette = the command forms (data/forms.json): every command once, merged variants,
// with the line its default values build.
QVariantList Engine::commands() const { return m_forms.paletteRows(); }

QStringList Engine::categories() const
{
    QStringList out;
    for (const QVariant& r : m_forms.paletteRows()) {
        const QString c = r.toMap().value(QStringLiteral("category")).toString();
        if (!out.contains(c)) out << c;
    }
    return out;
}

QVariantMap Engine::storyNames(const QString& text) const
{
    QStringList vars, meters, items;
    auto add = [](QStringList& l, const QString& v) {
        const QString s = pyStrip(v);
        if (!s.isEmpty() && !l.contains(s)) l << s;
    };
    for (const QString& raw : pySplitLines(text)) {
        const QString s = pyStrip(raw);
        if (s.isEmpty() || s.startsWith(QLatin1Char('#'))) continue;
        const QString w = firstWord(s), cmd = normalizeCommand(w), rest = pyStrip(s.mid(w.size()));
        if (cmd == QLatin1String("setvar") || cmd == QLatin1String("addvar") || cmd == QLatin1String("persistentvar") ||
            cmd == QLatin1String("persistentadd") || cmd == QLatin1String("variable")) {
            add(vars, firstWord(rest));
        } else if (cmd == QLatin1String("meter")) {
            add(meters, rest.section(QLatin1Char('|'), 0, 0));
            add(vars, rest.section(QLatin1Char('|'), 0, 0));
        } else if (cmd == QLatin1String("itemget")) {
            add(items, rest.section(QLatin1Char('|'), 0, 0));
        } else if (cmd == QLatin1String("remember")) {
            for (const QString& part : rest.split(QLatin1Char('|'))) {
                const QString p = pyStrip(part);
                if (p.startsWith(U("флаг=")) || p.startsWith(QLatin1String("flag="))) add(vars, p.section(QLatin1Char('='), 1));
            }
        }
    }
    return {{QStringLiteral("scenes"), sceneNames(text)}, {QStringLiteral("vars"), vars}, {QStringLiteral("meters"), meters}, {QStringLiteral("items"), items}};
}

static const QStringList& videoFilters()
{
    static const QStringList f{QStringLiteral("*.ogv"), QStringLiteral("*.webm"), QStringLiteral("*.mp4"), QStringLiteral("*.mkv"),
                               QStringLiteral("*.avi"), QStringLiteral("*.mpg"), QStringLiteral("*.mpeg"), QStringLiteral("*.mov")};
    return f;
}

QVariantList Engine::videos() const
{
    QVariantList out;
    const QString game = m_es.vfs().gameDir() + QStringLiteral("/video");
    for (const QString& f : QDir(game).entryList(videoFilters(), QDir::Files, QDir::Name))
        out << QVariantMap{{QStringLiteral("path"), QStringLiteral("es:video/") + f},
                           {QStringLiteral("title"), f == QLatin1String("opening.ogv") ? U("Заставка БЛ (opening.ogv)") : f},
                           {QStringLiteral("es"), true}, {QStringLiteral("file"), game + QLatin1Char('/') + f}};
    if (!m_current.isEmpty()) {
        const QString dir = assetsDir(m_current) + QStringLiteral("/video");
        for (const QString& f : QDir(dir).entryList(videoFilters(), QDir::Files, QDir::Name))
            out << QVariantMap{{QStringLiteral("path"), QStringLiteral("video/") + f}, {QStringLiteral("title"), f},
                               {QStringLiteral("es"), false}, {QStringLiteral("file"), dir + QLatin1Char('/') + f}};
    }
    return out;
}

void Engine::libraryScan()
{
    if (m_libState != 0) return;
    m_libState = 1;
    emit libraryChanged();
    const QString ws = Vfs::workshopDirFor(m_es.esRoot());
    auto* w = new QFutureWatcher<void>(this);
    connect(w, &QFutureWatcher<void>::finished, this, [this, w] {
        w->deleteLater();
        m_libState = 2;
        emit libraryChanged();
        startWardrobe();
    });
    w->setFuture(QtConcurrent::run([this, ws] { m_library.scan(ws); }));
}

void Engine::startWardrobe()
{
    if (m_wardrobeState.load() != 0) return;
    m_wardrobeState = 1;
    emit wardrobeChanged();
    const QString cache = m_root + QStringLiteral("/work/cache/wardrobe_boxes.tsv");
    auto* w = new QFutureWatcher<void>(this);
    connect(w, &QFutureWatcher<void>::finished, this, [this, w] {
        w->deleteLater();
        gb::setWardrobe(&m_wardrobe);
        m_renderer.dropSpriteCache();
        m_wardrobeState = 2;
        emit wardrobeChanged();
        emit assetsChanged();
    });
    w->setFuture(QtConcurrent::run([this, cache] { m_wardrobe.build(m_library, m_es, cache); }));
}

void Engine::waitForWardrobe(const std::function<void(const QString&)>& log) const
{
    if (gb::wardrobe() || m_wardrobeState.load() == 0) return;
    if (log) log(U("Жду гардероб мастерской…"));
    for (int i = 0; i < 1200 && !gb::wardrobe(); ++i) QThread::msleep(100);
}

QVariantList Engine::wardrobeOutfits(const QString& tag) const
{
    QVariantList out;
    const Wardrobe* wr = gb::wardrobe();
    if (!wr) return out;
    for (const Wardrobe::Outfit& o : wr->outfits(tag))
        out << QVariantMap{{QStringLiteral("id"), o.part}, {QStringLiteral("adult"), o.adult}, {QStringLiteral("body"), o.body}, {QStringLiteral("figure"), o.figure},
                           {QStringLiteral("dists"), o.dists}, {QStringLiteral("source"), o.source},
                           {QStringLiteral("title"), m_library.titleOf(o.source)}};
    return out;
}

QString Engine::listDir() const
{
#ifdef GB_RELEASE
    return m_root + QStringLiteral("/work");
#else
    return m_root + QStringLiteral("/data");        // what gets deleted / fixed here ships with the program
#endif
}

void Engine::applyFaceShifts()
{
    QHash<QString, QPoint> all = m_faceShiftsShipped;
    for (auto it = m_faceShifts.cbegin(); it != m_faceShifts.cend(); ++it) all.insert(it.key(), *it);
    m_wardrobe.setFaceShifts(all);
}

void Engine::setHidden(const QSet<QString>& keys, const QString& toastText)
{
    m_hidden = keys;
    QStringList lines(m_hidden.begin(), m_hidden.end());
    lines.sort();
    writeFile(listDir() + QStringLiteral("/wardrobe_hidden.txt"), (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    m_wardrobe.setHidden(m_hidden + m_hiddenShipped);
    m_renderer.dropSpriteCache();
    emit wardrobeChanged();
    emit assetsChanged();
    if (!toastText.isEmpty()) emit toast(toastText, 0);
}

bool Engine::spriteHidden(const QString& tag, const QString& name) const
{
    if (m_hidden.isEmpty() && m_hiddenShipped.isEmpty()) return false;
    const QString face = name.section(QLatin1Char(' '), 0, 0), outfit = name.section(QLatin1Char(' '), 1);
    return m_wardrobe.isHidden(tag, face, outfit);
}

void Engine::hideSprite(const QString& tag, const QString& name)
{
    QSet<QString> k = m_hidden;
    k.insert(tag + QStringLiteral("|look:") + name.simplified());
    setHidden(k, U("Удалено навсегда: ") + tag + QLatin1Char(' ') + name + U("  (вернуть — «Удалённые»)"));
}

void Engine::hideEmotion(const QString& tag, const QString& emotion)
{
    QSet<QString> k = m_hidden;
    k.insert(tag + QStringLiteral("|face:") + emotion.simplified());
    setHidden(k, U("Эмоция «%1» удалена у всех нарядов").arg(emotion));
}

void Engine::hideOutfit(const QString& tag, const QString& outfit)
{
    QSet<QString> k = m_hidden;
    k.insert(tag + QStringLiteral("|outfit:") + outfit.simplified());
    setHidden(k, U("Наряд «%1» удалён целиком").arg(outfit));
}

QVariantList Engine::hiddenSprites(const QString& tag) const
{
    QStringList keys;
    for (const QString& k : m_hidden) if (k.startsWith(tag + QLatin1Char('|'))) keys << k;
    keys.sort();
    QVariantList out;
    for (const QString& k : keys) {
        const QString rest = k.section(QLatin1Char('|'), 1);
        const QString kind = rest.section(QLatin1Char(':'), 0, 0), what = rest.section(QLatin1Char(':'), 1);
        const QString label = kind == QLatin1String("face") ? U("эмоция %1 (у всех нарядов)").arg(what)
                            : kind == QLatin1String("outfit") ? U("наряд %1 целиком").arg(what) : what;
        out << QVariantMap{{QStringLiteral("key"), k}, {QStringLiteral("label"), label}};
    }
    return out;
}

void Engine::unhideSprite(const QString& key)
{
    QSet<QString> k = m_hidden;
    k.remove(key);
    setHidden(k, U("Вернул: ") + key.section(QLatin1Char('|'), 1).section(QLatin1Char(':'), 1));
}

bool Engine::wardrobeKnows(const QString& image) const
{
    const Wardrobe* wr = gb::wardrobe();
    return wr && wr->resolve(image);
}

QVariantMap Engine::faceShiftOf(const QString& tag, const QString& name, const QString& dist) const
{
    QString scope;
    const QPoint p = m_wardrobe.faceShift(tag, dist, name.section(QLatin1Char(' '), 0, 0), name.section(QLatin1Char(' '), 1), &scope);
    return {{QStringLiteral("dx"), p.x()}, {QStringLiteral("dy"), p.y()}, {QStringLiteral("scope"), scope}};
}

void Engine::setFaceShift(const QString& tag, const QString& name, const QString& dist, const QString& scope, int dx, int dy)
{
    const QString face = name.section(QLatin1Char(' '), 0, 0), outfit = name.section(QLatin1Char(' '), 1);
    const QString d = dist.isEmpty() ? QStringLiteral("normal") : dist;
    const QString key = tag + QLatin1Char('|') + d + QLatin1Char('|') +
                        (scope == QLatin1String("outfit") ? QStringLiteral("outfit:") + outfit
                         : scope == QLatin1String("face") ? QStringLiteral("face:") + face : QStringLiteral("look:") + name.simplified());
    if (dx == 0 && dy == 0) m_faceShifts.remove(key);
    else m_faceShifts.insert(key, QPoint(dx, dy));
    QStringList lines;
    for (auto it = m_faceShifts.cbegin(); it != m_faceShifts.cend(); ++it)
        lines << it.key() + QLatin1Char('\t') + QString::number(it->x()) + QLatin1Char('\t') + QString::number(it->y());
    lines.sort();
    writeFile(listDir() + QStringLiteral("/wardrobe_faces.txt"), (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    applyFaceShifts();
    m_renderer.dropSpriteCache();
    emit wardrobeChanged();
    emit assetsChanged();
    emit toast(dx == 0 && dy == 0 ? U("Лицо на месте (сдвиг убран)") : U("Лицо поправлено: %1, %2").arg(dx).arg(dy), 0);
}

QString Engine::faceFixUrl(const QString& image, int dx, int dy, bool head) const
{
    return QStringLiteral("image://gb/%1/%2|%3|%4|%5")
        .arg(head ? QStringLiteral("wfixhead") : QStringLiteral("wfix"), QString::fromLatin1(QUrl::toPercentEncoding(image)))
        .arg(dx).arg(dy).arg(++m_fixNonce);
}

QStringList Engine::wardrobeLooks(const QString& tag, const QString& outfit, const QString& dist) const
{
    const Wardrobe* wr = gb::wardrobe();
    return wr ? wr->looks(tag, outfit, dist) : QStringList();
}

QVariantList Engine::libraryItems() const
{
    QVariantList out;
    for (const LibItem& it : m_library.items())
        out << QVariantMap{{QStringLiteral("id"), it.id}, {QStringLiteral("title"), it.title}, {QStringLiteral("count"), int(it.images.size())}};
    return out;
}

QVariantMap Engine::libraryList(const QString& id, const QString& folder, const QString& query, int limit) const
{
    const Library::Listing l = m_library.list(id, folder, query, limit);
    QVariantList folders;
    for (const auto& f : l.folders) folders << QVariantMap{{QStringLiteral("name"), f.first}, {QStringLiteral("count"), f.second}};
    return {{QStringLiteral("folders"), folders}, {QStringLiteral("files"), l.files}, {QStringLiteral("total"), l.total}};
}

QString Engine::libraryImport(const QString& ref, const QString& kind, const QString& name)
{
    if (m_current.isEmpty()) return {};
    const QImage img = QImage::fromData(m_library.read(ref));
    if (img.isNull()) { emit toast(U("Картинку не удалось прочитать"), 2); return {}; }
    const QString n = saveProjectImage(img, kind, name);
    if (n.isEmpty()) return {};
    // who made it: the mod's page, so the author can be credited when the mod is published
    QFile credits(assetsDir(m_current) + QStringLiteral("/CREDITS.txt"));
    if (credits.open(QIODevice::Append | QIODevice::Text)) {
        const QString id = ref.section(QLatin1Char('/'), 0, 0);
        credits.write((n + U("  <-  «") + m_library.titleOf(id) + U("», https://steamcommunity.com/sharedfiles/filedetails/?id=") + id +
                       QStringLiteral("  (") + ref.mid(id.size() + 1) + QStringLiteral(")\n"))
                          .toUtf8());
    }
    return n;
}

QVariantList Engine::mapZones() const
{
    QVariantList out;
    for (const EsMapZone& z : esMapZones())
        out << QVariantMap{{QStringLiteral("id"), z.id}, {QStringLiteral("title"), z.title}, {QStringLiteral("x1"), z.x1},
                           {QStringLiteral("y1"), z.y1}, {QStringLiteral("x2"), z.x2}, {QStringLiteral("y2"), z.y2}};
    return out;
}

QVariantList Engine::chibis() const
{
    QVariantList out;
    for (const QString& id : esChibiIds())
        out << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), esChibiName(id)},
                           {QStringLiteral("icon"), QUrl::fromLocalFile(m_root + QStringLiteral("/data/mod_assets/chibi/") + esChibiFile(id) + QStringLiteral(".png")).toString()}};
    return out;
}

QVariantList Engine::projectAudio() const
{
    QVariantList out;
    if (m_current.isEmpty()) return out;
    for (const QString& f : QDir(assetsDir(m_current) + QStringLiteral("/audio")).entryList(QDir::Files, QDir::Name))
        out << QVariantMap{{QStringLiteral("path"), QStringLiteral("audio/") + f}, {QStringLiteral("title"), f}};
    return out;
}

QVariantList Engine::projectFiles(const QString& subdir) const
{
    QVariantList out;
    if (m_current.isEmpty()) return out;
    const QString dir = assetsDir(m_current) + QLatin1Char('/') + subdir;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    QStringList rels;
    while (it.hasNext()) rels << QDir(assetsDir(m_current)).relativeFilePath(it.next());
    rels.sort();
    for (const QString& r : rels)
        out << QVariantMap{{QStringLiteral("path"), r}, {QStringLiteral("title"), QFileInfo(r).fileName()},
                           {QStringLiteral("url"), QUrl::fromLocalFile(assetsDir(m_current) + QLatin1Char('/') + r).toString()}};
    return out;
}

QString Engine::importFile(const QString& fileUrl, const QString& subdir)
{
    if (m_current.isEmpty()) return {};
    const QFileInfo fi(localPath(fileUrl));
    if (!fi.isFile()) return {};
    const QString name = slug(fi.completeBaseName(), QStringLiteral("file"), false) + QLatin1Char('.') + fi.suffix().toLower();
    const QString dst = assetsDir(m_current) + QLatin1Char('/') + subdir + QLatin1Char('/') + name;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (QFileInfo(dst).canonicalFilePath() != fi.canonicalFilePath()) {
        QFile::remove(dst);
        if (!QFile::copy(fi.absoluteFilePath(), dst)) { emit toast(U("Не удалось скопировать ") + fi.fileName(), 2); return {}; }
    }
    if (subdir == QLatin1String("images")) m_renderer.setCustomImages(build::customImageFiles(assetsDir(m_current)));
    emit assetsChanged();
    return subdir + QLatin1Char('/') + name;
}

QString Engine::importVideo(const QString& fileUrl)
{
    if (m_current.isEmpty() || m_busy) return {};
    const QFileInfo fi(localPath(fileUrl));
    if (!fi.isFile()) return {};
    const QString ext = fi.suffix().toLower();
    // Ren'Py 7.4 plays WebM (VP8/VP9) and Ogg Theora; H.264 mp4/mkv/mov get converted to WebM
    if (ext == QLatin1String("webm") || ext == QLatin1String("ogv")) return importFile(fileUrl, QStringLiteral("video"));
    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty()) { emit toast(U("Для mp4/mkv нужен ffmpeg (или сразу .webm)"), 2); return {}; }
    const QString name = slug(fi.completeBaseName(), QStringLiteral("video"), false) + QStringLiteral(".webm");
    const QString dst = assetsDir(m_current) + QStringLiteral("/video/") + name;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    setBusy(true, U("Перегоняю ") + fi.fileName() + U(" в WebM для игры…"));
    const QString src = fi.absoluteFilePath();
    auto* w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, name] {
        const bool ok = w->result();
        w->deleteLater();
        setBusy(false);
        emit assetsChanged();
        emit toast(ok ? U("Видео готово: video/") + name : U("ffmpeg не смог сконвертировать видео"), ok ? 0 : 2);
    });
    w->setFuture(QtConcurrent::run([ffmpeg, src, dst] {
        QProcess p;
        p.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-i"), src, QStringLiteral("-c:v"), QStringLiteral("libvpx"), QStringLiteral("-b:v"),
                         QStringLiteral("4M"), QStringLiteral("-crf"), QStringLiteral("10"), QStringLiteral("-deadline"), QStringLiteral("good"),
                         QStringLiteral("-cpu-used"), QStringLiteral("4"), QStringLiteral("-c:a"), QStringLiteral("libvorbis"), QStringLiteral("-q:a"),
                         QStringLiteral("5"), dst});
        return p.waitForFinished(1800000) && p.exitCode() == 0 && QFileInfo(dst).size() > 0;
    }));
    return QStringLiteral("video/") + name;
}

QVariantList Engine::cast() const
{
    QVariantList out;
    for (const QString& tag : m_es.spriteTags()) {
        const QStringList names = m_es.spriteNames(tag);
        QString def = names.value(0);
        for (const QString& n : names) if (n.startsWith(QLatin1String("normal pioneer")) || n == QLatin1String("smile pioneer")) { def = n; if (n == QLatin1String("smile pioneer")) break; }
        out << QVariantMap{{QStringLiteral("id"), tag}, {QStringLiteral("name"), m_es.characterName(tag).isEmpty() ? tag : m_es.characterName(tag)},
                           {QStringLiteral("color"), m_es.characterColor(tag, QStringLiteral("day"))}, {QStringLiteral("count"), names.size()},
                           {QStringLiteral("pose"), def}};
    }
    // ONE copy: customImages() returns a new hash each call - begin() of one copy and end() of another walked freed
    // memory, and GenryBL fell as soon as a project had a picture of its own (the «свои задники» / Miguel PNG crash)
    const QHash<QString, QString> custom = m_renderer.customImages();
    for (auto it = custom.constBegin(); it != custom.constEnd(); ++it) {
        const QString name = it.key();
        if (name.startsWith(QLatin1String("bg ")) || name.startsWith(QLatin1String("cg "))) continue;
        const QString tag = name.section(QLatin1Char(' '), 0, 0);
        bool have = false;
        for (const QVariant& v : out) if (v.toMap().value(QStringLiteral("id")) == tag) have = true;
        if (!have)
            out << QVariantMap{{QStringLiteral("id"), tag}, {QStringLiteral("name"), tag}, {QStringLiteral("color"), QStringLiteral("#ffdd7d")},
                               {QStringLiteral("count"), 1}, {QStringLiteral("pose"), name.section(QLatin1Char(' '), 1)}, {QStringLiteral("custom"), true}};
    }
    return out;
}

QString Engine::esName(const QString& kind, const QString& key) const
{
    if (!m_esNamesRead) {
        // es-doc's captions of the game's resources (GPL-3.0, github.com/sovue/es-doc-assets descriptions.yaml)
        m_esNamesRead = true;
        const QJsonObject o = QJsonDocument::fromJson(readFile(m_root + QStringLiteral("/data/es_doc/descriptions.json"))).object();
        for (auto k = o.begin(); k != o.end(); ++k) {
            if (!k.value().isObject()) continue;
            const QJsonObject names = k.value().toObject();
            QHash<QString, QString>& dst = m_esNames[k.key()];
            for (auto n = names.begin(); n != names.end(); ++n) dst.insert(n.key(), n.value().toString());
        }
    }
    const auto kindIt = m_esNames.constFind(kind);
    if (kindIt == m_esNames.constEnd()) return {};
    const QString t = kindIt->value(key);
    return t.isEmpty() ? kindIt->value(key + U(" (файл)")) : t;
}

QVariantMap Engine::diceForm(const QString& id, const QVariantMap& values) const
{
    QVariantMap v = values;
    auto* rng = QRandomGenerator::global();
    auto pick = [rng](const QStringList& l) { return l.isEmpty() ? QString() : l.at(int(rng->bounded(l.size()))); };
    if (id == QLatin1String("modmenu")) {
        // another style each time, on a place of the camp (outside, not an ending's duplicate), with a track of the game
        const QStringList styles{U("бл"), U("7дл"), U("панель"), U("тетрадь"), U("дневник"), U("доска"), U("монитор"), U("нуар"), U("живое"),
                                 U("кино"), U("карта"), U("свой"), U("свой"), U("свой")};
        QString st = v.value(QStringLiteral("style")).toString();
        for (int i = 0; i < 8 && st == v.value(QStringLiteral("style")).toString(); ++i) st = pick(styles);
        v.insert(QStringLiteral("style"), st);
        QStringList places;
        for (const QString& b : m_es.backgrounds())
            if (b.startsWith(QLatin1String("ext_")) && !b.contains(QLatin1String("_ending")) && !b.contains(QLatin1String("bus"))) places << b;
        if (!places.isEmpty()) v.insert(QStringLiteral("bg"), pick(places));
        // «свой»: parts that go together - the air of the place's hour, a colour for the look, a way to come in
        for (const char* k : {"layout", "look", "accent", "fx", "enter"}) v.insert(QString::fromLatin1(k), QString());
        if (st == U("свой")) {
            const QString bg = v.value(QStringLiteral("bg")).toString();
            const bool night = bg.contains(QLatin1String("night")), evening = bg.contains(QLatin1String("sunset"));
            v.insert(QStringLiteral("layout"), pick({U("слева"), U("справа"), U("по центру"), U("снизу")}));
            const QString look = pick(night ? QStringList{U("неон"), U("таблички"), U("текст")} : QStringList{U("текст"), U("таблички"), U("бл"), U("неон")});
            v.insert(QStringLiteral("look"), look);
            if (look != U("бл"))
                v.insert(QStringLiteral("accent"), pick(night ? QStringList{QStringLiteral("#8be9fd"), QStringLiteral("#bd93f9"), QStringLiteral("#ff79c6"), QStringLiteral("#6be5c5")}
                                                       : evening ? QStringList{QStringLiteral("#ffb86c"), QStringLiteral("#ffd27d"), QStringLiteral("#ff6e6e")}
                                                                 : QStringList{QStringLiteral("#ffd27d"), QStringLiteral("#50fa7b"), QStringLiteral("#8be9fd"), QStringLiteral("#ff79c6")}));
            v.insert(QStringLiteral("fx"), rng->bounded(4) == 0 ? pick({U("снег"), U("сердца"), U("дождь")})
                                                                : night ? U("светлячки") : evening ? U("листья") : U("пыль"));
            v.insert(QStringLiteral("enter"), pick({U("выезд"), U("проявление"), U("снизу"), U("печать")}));
        }
        QStringList tracks;
        for (auto it = m_es.music.begin(); it != m_es.music.end(); ++it)
            if (!it.key().contains(QLatin1String("silence"))) tracks << it.key();
        if (!tracks.isEmpty()) v.insert(QStringLiteral("music"), pick(tracks));
        static const QStringList heroes{QStringLiteral("sl smile dress"), QStringLiteral("dv grin pioneer"), QStringLiteral("un shy pioneer"),
                                        QStringLiteral("mi happy pioneer"), QStringLiteral("us laugh sport"), QStringLiteral("sl happy pioneer"),
                                        QStringLiteral("un smile dress"), QStringLiteral("mi smile dress"), QStringLiteral("dv smile pioneer")};
        QStringList ok;
        for (const QString& h : heroes) if (m_es.sprites.contains(h)) ok << h;
        QVariantList hs;
        for (int i = 0; i < 2 && !ok.isEmpty(); ++i) {
            const QString h = pick(ok);
            ok.removeAll(h);
            hs << QVariantMap{{QStringLiteral("sprite"), h}};
        }
        if (!hs.isEmpty()) v.insert(QStringLiteral("heroes"), hs);
    }
    return v;
}

QVariantList Engine::titleLooks() const
{
    QVariantList out;
    for (const TitleLook& l : kTitleLooks)
        out << QVariantMap{{QStringLiteral("name"), QString::fromUtf8(l.name)}, {QStringLiteral("font"), QString::fromLatin1(l.font)},
                           {QStringLiteral("color"), QString::fromLatin1(l.color)}, {QStringLiteral("size"), l.size},
                           {QStringLiteral("style"), QString::fromLatin1(l.style)}};
    return out;
}

QVariantList Engine::communitySounds() const
{
    // es-doc's community folder: sounds, ambiences and music from mods, with their authors
    QVariantList out;
    const QJsonObject o = QJsonDocument::fromJson(readFile(m_root + QStringLiteral("/data/es_doc/community.json"))).object();
    for (const QJsonValue& v : o.value(QStringLiteral("sounds")).toArray()) {
        const QJsonObject s = v.toObject();
        QStringList caps;
        for (const QJsonValue& c : s.value(QStringLiteral("captions")).toArray()) caps << c.toString();
        const QString file = s.value(QStringLiteral("file")).toString();
        const QString abs = m_root + QStringLiteral("/data/es_doc/") + file;
        if (!QFileInfo::exists(abs)) continue;
        out << QVariantMap{{QStringLiteral("file"), file}, {QStringLiteral("kind"), s.value(QStringLiteral("kind")).toString()},
                           {QStringLiteral("title"), s.value(QStringLiteral("title")).toString()}, {QStringLiteral("captions"), caps.join(QStringLiteral(" · "))},
                           {QStringLiteral("url"), QUrl::fromLocalFile(abs).toString()}};
    }
    return out;
}

QString Engine::importCommunity(const QString& file)
{
    if (m_current.isEmpty()) return {};
    const QString src = m_root + QStringLiteral("/data/es_doc/") + file;
    if (file.contains(QLatin1String("..")) || !QFileInfo::exists(src)) return {};
    const QString name = QFileInfo(src).fileName();
    const QString dst = assetsDir(m_current) + QStringLiteral("/audio/") + name;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (!QFileInfo::exists(dst) && !QFile::copy(src, dst)) { emit toast(U("Не удалось скопировать ") + name, 2); return {}; }
    // the author goes with the file: CREDITS.txt of the mod
    for (const QVariant& v : communitySounds()) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("file")).toString() != file) continue;
        QFile credits(assetsDir(m_current) + QStringLiteral("/CREDITS.txt"));
        if (credits.open(QIODevice::Append | QIODevice::Text))
            credits.write((QStringLiteral("audio/") + name + U("  <-  «") + s.value(QStringLiteral("title")).toString() + U("» (") +
                           s.value(QStringLiteral("captions")).toString() + U("), каталог сообщества es-doc: https://es-doc.sovue.org\n")).toUtf8());
    }
    emit assetsChanged();
    return QStringLiteral("audio/") + name;
}

QVariantList Engine::backgrounds() const
{
    QVariantList out;
    for (const QString& b : m_es.backgrounds())
        out << QVariantMap{{QStringLiteral("id"), b}, {QStringLiteral("custom"), false}, {QStringLiteral("title"), esName(QStringLiteral("bg"), b)}};
    const auto custom = m_renderer.customImages();
    for (auto it = custom.begin(); it != custom.end(); ++it)
        if (it.key().startsWith(QLatin1String("bg "))) out << QVariantMap{{QStringLiteral("id"), it.key().mid(3)}, {QStringLiteral("custom"), true}};
    return out;
}

QVariantList Engine::cgs() const
{
    QVariantList out;
    for (const QString& b : m_es.cgs())
        if (!esPatchImage(QStringLiteral("cg ") + b))
            out << QVariantMap{{QStringLiteral("id"), b}, {QStringLiteral("custom"), false}, {QStringLiteral("title"), esName(QStringLiteral("cg"), b)}};
    const auto custom = m_renderer.customImages();
    for (auto it = custom.begin(); it != custom.end(); ++it)
        if (it.key().startsWith(QLatin1String("cg "))) out << QVariantMap{{QStringLiteral("id"), it.key().mid(3)}, {QStringLiteral("custom"), true}};
    return out;
}

QVariantList Engine::music() const
{
    QVariantList out;
    for (auto it = m_es.music.begin(); it != m_es.music.end(); ++it)
        out << QVariantMap{{QStringLiteral("word"), it.key()}, {QStringLiteral("path"), it.value()}, {QStringLiteral("cmd"), U("музыка ") + it.key()},
                           {QStringLiteral("title"), esName(QStringLiteral("music"), it.key())}};
    if (!m_current.isEmpty())
        for (const QString& f : QDir(assetsDir(m_current) + QStringLiteral("/audio")).entryList(QDir::Files))
            out << QVariantMap{{QStringLiteral("word"), f}, {QStringLiteral("path"), QStringLiteral("audio/") + f},
                               {QStringLiteral("cmd"), U("музыкафайл audio/") + f}, {QStringLiteral("custom"), true}};
    return out;
}

QVariantList Engine::sounds() const
{
    QVariantList out;
    for (auto it = m_es.sounds.begin(); it != m_es.sounds.end(); ++it)
        out << QVariantMap{{QStringLiteral("word"), it.key()}, {QStringLiteral("path"), it.value()}, {QStringLiteral("cmd"), U("звук ") + it.key()},
                           {QStringLiteral("title"), esName(QStringLiteral("sfx"), it.key())}};
    if (!m_current.isEmpty())
        for (const QString& f : QDir(assetsDir(m_current) + QStringLiteral("/audio")).entryList(QDir::Files))
            out << QVariantMap{{QStringLiteral("word"), f}, {QStringLiteral("path"), QStringLiteral("audio/") + f},
                               {QStringLiteral("cmd"), U("звукфайл audio/") + f}, {QStringLiteral("custom"), true}};
    return out;
}

QVariantList Engine::ambience() const
{
    QVariantList out;
    for (auto it = m_es.ambience.begin(); it != m_es.ambience.end(); ++it)
        out << QVariantMap{{QStringLiteral("word"), it.key()}, {QStringLiteral("path"), it.value()}, {QStringLiteral("cmd"), U("атмосфера ") + it.key()},
                           {QStringLiteral("title"), esName(QStringLiteral("ambience"), it.key())}};
    // the mod's own files loop on the ambience channel too («атмосфера audio/лес.ogg»)
    if (!m_current.isEmpty())
        for (const QString& f : QDir(assetsDir(m_current) + QStringLiteral("/audio")).entryList(QDir::Files))
            out << QVariantMap{{QStringLiteral("word"), f}, {QStringLiteral("path"), QStringLiteral("audio/") + f},
                               {QStringLiteral("cmd"), U("атмосфера audio/") + f}, {QStringLiteral("custom"), true}};
    return out;
}

// ================================================================== story tools

QString Engine::compile(const QString& text) const
{
    QString err;
    const QString rpy = compileText(text, options(), &err, m_current.isEmpty() ? QVector<CustomImage>{} : build::customImages(assetsDir(m_current), parseMeta(pySplitLines(stripBom(text)), nullptr, options()).modId));
    return err.isEmpty() ? rpy : QStringLiteral("# ") + err;
}

QVariantList Engine::lint(const QString& text) const
{
    LintContext ctx;
    ctx.es = &m_es;
    const auto custom = m_renderer.customImages();
    for (auto it = custom.begin(); it != custom.end(); ++it) ctx.customImages.insert(it.key());
    if (!m_current.isEmpty())
        for (const QString& f : QDir(assetsDir(m_current) + QStringLiteral("/audio")).entryList(QDir::Files)) ctx.customAudio.insert(QStringLiteral("audio/") + f);
    ctx.opt = options();
    QVariantList out;
    for (const LintIssue& i : lintStory(text, ctx)) out << issueMap(i);
    if (!m_current.isEmpty()) {
        // the project's files: checked again only when something in the folder changed
        QString sig;
        QDirIterator it(assetsDir(m_current), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo fi(it.next());
            sig += fi.filePath() + QString::number(fi.size()) + QString::number(fi.lastModified().toMSecsSinceEpoch());
        }
        if (sig != m_assetSig) {
            m_assetSig = sig;
            m_assetIssues = build::checkAssets(assetsDir(m_current));
        }
        for (const LintIssue& i : std::as_const(m_assetIssues)) out << issueMap(i);
    }
    return out;
}

static QVariantList notesList(const QVector<ScreenplayNote>& notes, int base)
{
    QVariantList out;
    for (const ScreenplayNote& n : notes)
        out << QVariantMap{{QStringLiteral("line"), n.line + base}, {QStringLiteral("warn"), n.warn}, {QStringLiteral("text"), n.text}};
    return out;
}

QVariantMap Engine::convertScreenplay(const QString& text, bool expand) const
{
    QVector<ScreenplayNote> notes;
    const QStringList lines = convertToStory(text, expand, &notes);
    return {{QStringLiteral("lines"), lines}, {QStringLiteral("notes"), notesList(notes, 1)}};
}

QVariantMap Engine::expandScreenplayLine(const QString& storyText, int line) const
{
    QVector<ScreenplayNote> notes;
    const QStringList lines = gb::expandScreenplayLine(pySplitLines(stripBom(storyText)), line - 1, &notes);
    return {{QStringLiteral("lines"), lines}, {QStringLiteral("notes"), notesList(notes, 1)}};
}

QString Engine::screenplayKind(const QString& lineText) const { return gb::screenplayKind(lineText); }
QString Engine::clipboardText() const { return QGuiApplication::clipboard()->text(); }
void Engine::copyText(const QString& text) const { QGuiApplication::clipboard()->setText(text); }

QVariantMap Engine::sceneInfo(const QString& text, int line) const
{
    const SceneState s = sceneAt(text, line, &m_es);
    QVariantList sprites;
    for (const SpriteShow& sp : s.sprites)
        sprites << QVariantMap{{QStringLiteral("image"), sp.image}, {QStringLiteral("tag"), sp.tag}, {QStringLiteral("mirror"), sp.mirror}};
    // the mod's points and flags: where each one starts and every place it changes (by scene),
    // because summing both branches of a choice would show a value no player ever has
    struct Var { QString init; QStringList changes; bool here = false; bool persistent = false; };
    QMap<QString, Var> vars;
    QString scene = QStringLiteral("start");
    const QStringList lines = pySplitLines(stripBom(text));
    for (int i = 0; i < lines.size(); ++i) {
        const QString s = pyStrip(lines[i]);
        if (s.startsWith(QLatin1Char(':'))) { scene = pyStrip(s.mid(1)); continue; }
        const QString w = firstWord(s);
        const QString cmd = normalizeCommand(w);
        const QStringList p = pySplit(pyStrip(s.mid(w.size())));
        if (p.isEmpty()) continue;
        const bool pers = cmd == QLatin1String("persistentvar") || cmd == QLatin1String("persistentadd");
        if (cmd == QLatin1String("setvar") || cmd == QLatin1String("variable") || cmd == QLatin1String("persistentvar")) {
            Var& v = vars[p[0]];
            v.persistent = v.persistent || pers;
            if (v.init.isEmpty() && v.changes.isEmpty()) v.init = p.value(1, QStringLiteral("0"));
            else v.changes << U("= %1 в «%2»").arg(p.value(1, QStringLiteral("0")), scene);
            if (i + 1 == line) v.here = true;
        } else if (cmd == QLatin1String("addvar") || cmd == QLatin1String("persistentadd")) {
            Var& v = vars[p[0]];
            v.persistent = v.persistent || pers;
            QString d = p.value(1, QStringLiteral("1"));
            if (!d.startsWith(QLatin1Char('-')) && !d.startsWith(QLatin1Char('+'))) d.prepend(QLatin1Char('+'));
            v.changes << U("%1 в «%2»").arg(d, scene);
            if (i + 1 == line) v.here = true;
        }
    }
    QVariantList varList;
    for (auto it = vars.cbegin(); it != vars.cend(); ++it)
        varList << QVariantMap{{QStringLiteral("name"), it.key()}, {QStringLiteral("init"), it->init.isEmpty() ? QStringLiteral("0") : it->init},
                               {QStringLiteral("changes"), it->changes.join(QStringLiteral("  ·  "))}, {QStringLiteral("here"), it->here},
                               {QStringLiteral("persistent"), it->persistent}};
    return {{QStringLiteral("bg"), s.bg}, {QStringLiteral("sprites"), sprites}, {QStringLiteral("name"), s.speakerName},
            {QStringLiteral("color"), s.speakerColor}, {QStringLiteral("text"), s.text}, {QStringLiteral("time"), s.timeOfDay},
            {QStringLiteral("music"), s.music}, {QStringLiteral("weather"), s.weather}, {QStringLiteral("filter"), s.filter},
            {QStringLiteral("ambience"), s.ambience}, {QStringLiteral("sound"), s.sound}, {QStringLiteral("moment"), s.moment},
            {QStringLiteral("nvl"), s.nvlMode}, {QStringLiteral("vars"), varList}};
}

QString Engine::previewUrl(const QString& text, int line, const QString& extra, int choiceHover)
{
    // above the first picture (the @meta lines, the first label) the preview shows the mod's
    // opening frame - also under a hovered palette command, so «погода снег» snows on the camp
    auto hasPicture = [](const SceneState& s) { return !s.bg.isEmpty() || !s.sprites.isEmpty() || !s.text.isEmpty(); };
    const QStringList all = pySplitLines(text);
    int base = qMax(0, line);
    SceneState st = sceneAt(text, base, &m_es);
    if (!hasPicture(st)) {
        for (int i = base + 1; i <= all.size(); ++i) {
            const SceneState s = sceneAt(text, i, &m_es);
            if (hasPicture(s)) { st = s; base = i; break; }
        }
    }
    if (!extra.isEmpty()) {
        QStringList lines = all.mid(0, base);
        lines << extra.split(QLatin1Char('\n'));
        st = sceneAt(lines.join(QLatin1Char('\n')), int(lines.size()), &m_es);
    }
    if (choiceHover != -2) st.choiceHover = choiceHover;
    QMutexLocker lock(&m_sceneMx);
    const int key = ++m_sceneKey;
    m_scenes.insert(key, st);
    m_scenes.remove(key - 48);
    return QStringLiteral("image://gb/scene/%1").arg(key);
}

QVariantMap Engine::cinemaStart(const QString& text, int line)
{
    m_cinema.load(text, &m_es);
    return cinemaMap(m_cinema.start(line));
}

QVariantMap Engine::cinemaNext(int option) { return cinemaMap(m_cinema.next(option)); }

QVariantMap Engine::cinemaMap(const CinemaStop& c)
{
    static const char* const kinds[] = {"say", "choice", "note", "card", "timed", "video", "end"};
    SceneState st = c.scene;
    const bool typed = c.kind == CinemaStop::Say && !st.nvlMode && !st.text.isEmpty() && !st.windowHidden;
    st.hideSayText = typed;
    // the cinema draws its own clickable options (the phone call and the map keep their screens under them)
    if (c.kind == CinemaStop::Choice && st.phoneCall.isEmpty() && !st.map) {
        st.choices.clear();
        st.screenMenu = false;
    }
    int key;
    {
        QMutexLocker lock(&m_sceneMx);
        key = ++m_sceneKey;
        m_scenes.insert(key, st);
        m_scenes.remove(key - 48);
    }
    static const QRegularExpression tags(QStringLiteral("\\{[^}]*\\}"));
    QString text = st.text;
    text.remove(tags);
    auto url = [this](const QString& path) { return path.isEmpty() ? QString() : audioUrl(path); };
    QVariantList sounds, popups;
    for (const QString& s : c.sounds) if (!url(s).isEmpty()) sounds << url(s);
    for (const QString& p : c.popups) popups << QVariantMap{{QStringLiteral("title"), p.section(QLatin1Char('|'), 0, 0)}, {QStringLiteral("text"), p.section(QLatin1Char('|'), 1)}};
    QString video;
    if (!c.video.isEmpty()) {
        const QString f = c.video.startsWith(QLatin1String("es:")) ? m_es.vfs().gameDir() + QLatin1Char('/') + c.video.mid(3)
                                                                   : (m_current.isEmpty() ? QString() : assetsDir(m_current) + QLatin1Char('/') + c.video);
        if (!f.isEmpty() && QFileInfo::exists(f)) video = QUrl::fromLocalFile(f).toString();
    }
    QVariantList ok;
    for (bool b : c.optionOk) ok << b;
    return {{QStringLiteral("kind"), QString::fromLatin1(kinds[c.kind])}, {QStringLiteral("line"), c.line},
            {QStringLiteral("frame"), QStringLiteral("image://gb/cine/%1").arg(key)},
            {QStringLiteral("speaker"), st.speakerName}, {QStringLiteral("color"), st.speakerColor}, {QStringLiteral("text"), text},
            {QStringLiteral("typed"), typed}, {QStringLiteral("whatColor"), st.whatColor.isEmpty() ? QStringLiteral("#ffdd7d") : st.whatColor},
            {QStringLiteral("options"), c.options}, {QStringLiteral("optionOk"), ok}, {QStringLiteral("optionHints"), c.optionHints},
            {QStringLiteral("seconds"), c.seconds},
            {QStringLiteral("music"), url(c.music)}, {QStringLiteral("musicKey"), c.music},
            {QStringLiteral("ambience"), url(c.ambience)}, {QStringLiteral("ambienceKey"), c.ambience},
            {QStringLiteral("sounds"), sounds}, {QStringLiteral("popups"), popups}, {QStringLiteral("video"), video},
            {QStringLiteral("moment"), c.moment}, {QStringLiteral("note"), c.note}, {QStringLiteral("time"), st.timeOfDay}};
}

QVariantList Engine::lineStarts(const QString& text) const
{
    QVariantList out{0};
    for (int i = 0; i < text.size(); ++i) if (text.at(i) == QLatin1Char('\n')) out << i + 1;
    return out;
}

int Engine::lineAt(const QString& text, int pos) const
{
    int n = 1;
    for (int i = 0; i < qMin(pos, int(text.size())); ++i) n += text.at(i) == QLatin1Char('\n');
    return n;
}

QStringList Engine::sceneNames(const QString& text) const
{
    QStringList out;
    for (const QString& raw : pySplitLines(text)) {
        const QString s = pyStrip(raw);
        QString n;
        if (s.startsWith(QLatin1Char(':'))) n = pyStrip(s.mid(1));
        else if (normalizeCommand(firstWord(s)) == QLatin1String("label")) n = pyStrip(s.mid(firstWord(s).size()));
        if (!n.isEmpty() && !out.contains(n)) out << n;
    }
    return out;
}

QStringList Engine::spriteNames(const QString& tag) const
{
    QStringList out = m_es.spriteNames(tag);
    if (!m_hidden.isEmpty() || !m_hiddenShipped.isEmpty()) out.erase(std::remove_if(out.begin(), out.end(), [&](const QString& n) { return spriteHidden(tag, n); }), out.end());
    const QString prefix = tag + QLatin1Char(' ');
    const auto custom = m_renderer.customImages();
    for (auto it = custom.begin(); it != custom.end(); ++it) {
        if (it.key().startsWith(prefix)) out << it.key().mid(prefix.size());
        else if (it.key() == tag) out << QString();
    }
    return out;
}

QStringList Engine::spriteEmotions(const QString& tag) const
{
    QStringList out;
    for (const QString& n : m_es.spriteNames(tag)) {
        const QString e = n.section(QLatin1Char(' '), 0, 0);
        if (!out.contains(e)) out << e;
    }
    return out;
}

QStringList Engine::spriteOutfits(const QString& tag) const
{
    QStringList out;
    for (const QString& n : spriteNames(tag)) {
        const QString o = n.section(QLatin1Char(' '), 1);
        if (!o.isEmpty() && !out.contains(o)) out << o;
    }
    return out;
}

// the names a story counts with: its meters, its variables, the points of its options
static QStringList storyVarNames(const QString& fullText)
{
    QStringList out;
    static const QRegularExpression pts(QStringLiteral("\\[\\s*[+\\-−]\\s*\\d+\\s+([^\\]\\[,]+?)\\s*[\\],]"));
    for (const QString& raw : pySplitLines(fullText)) {
        const QString s = pyStrip(raw);
        const QString w = firstWord(s);
        const QString cmd = normalizeCommand(w);
        const QString rest = pyStrip(s.mid(w.size()));
        QString v;
        if (cmd == QLatin1String("meter")) v = pyStrip(rest.section(QLatin1Char('|'), 0, 0));
        else if (cmd == QLatin1String("addvar") || cmd == QLatin1String("setvar") || cmd == QLatin1String("variable")) v = pySplit(rest).value(0);
        if (!v.isEmpty() && !out.contains(v)) out << v;
        for (auto it = pts.globalMatch(s); it.hasNext();) {
            const QString p = it.next().captured(1).trimmed();
            if (!p.isEmpty() && !out.contains(p)) out << p;
        }
    }
    return out;
}

static QVariantMap choiceOptionMap(const QString& stripped)
{
    QString cleaned;
    const QStringList fx = choiceItemEffects(stripped, &cleaned);
    const ChoiceItemSpec it = parseChoiceItem(cleaned);
    QVariantList points;
    QStringList remember, flags;
    for (const QString& l : fx) {
        const QString w = firstWord(l);
        const QString rest = pyStrip(l.mid(w.size()));
        if (w == QString::fromUtf8("прибавить")) {
            const QStringList p = pySplit(rest);
            points << QVariantMap{{QStringLiteral("v"), p.value(0)}, {QStringLiteral("n"), p.value(1).toDouble()}};
        } else if (w == QString::fromUtf8("запомнит")) {
            remember << rest;
        } else if (w == QString::fromUtf8("установить")) {
            flags << pySplit(rest).value(0);
        }
    }
    static const QRegularExpression numTail(QStringLiteral("^(.+?)\\s+(-?\\d+(?:[.,]\\d+)?)\\+?$"));
    const QRegularExpressionMatch nm = numTail.match(it.need);
    return {{QStringLiteral("caption"), it.caption}, {QStringLiteral("target"), it.target}, {QStringLiteral("image"), it.image},
            {QStringLiteral("cond"), it.cond}, {QStringLiteral("need"), it.need}, {QStringLiteral("hint"), it.hint},
            {QStringLiteral("needVar"), nm.hasMatch() ? nm.captured(1).trimmed() : QString()},
            {QStringLiteral("needN"), nm.hasMatch() ? QString(nm.captured(2)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble() : 0.0},
            {QStringLiteral("exit"), it.exit}, {QStringLiteral("always"), it.always}, {QStringLiteral("points"), points},
            {QStringLiteral("remember"), remember}, {QStringLiteral("flags"), flags}};
}

// an option's mechanics at a glance: «+1» green / «-1» red, «🔒» a lock, «?» a condition, «★» remembered, «↪» its own scene
static QVariantList choiceBadgeParts(const QVariantMap& o)
{
    QVariantList parts;
    auto part = [&](const QString& t, const char* c) { parts << QVariantMap{{QStringLiteral("t"), t}, {QStringLiteral("c"), QString::fromLatin1(c)}}; };
    for (const QVariant& pv : o.value(QStringLiteral("points")).toList()) {
        const double n = pv.toMap().value(QStringLiteral("n")).toDouble();
        if (n) part((n > 0 ? QStringLiteral("+") : QString()) + QString::number(n), n > 0 ? "#50fa7b" : "#ff6b6b");
    }
    if (!o.value(QStringLiteral("need")).toString().isEmpty()) part(QString::fromUtf8("🔒"), "#ffc857");
    if (!o.value(QStringLiteral("cond")).toString().isEmpty()) part(QStringLiteral("?"), "#bd93f9");
    if (!o.value(QStringLiteral("remember")).toStringList().isEmpty()) part(QString::fromUtf8("★"), "#8be9fd");
    if (!o.value(QStringLiteral("flags")).toStringList().isEmpty()) part(QString::fromUtf8("⚑"), "#ff79c6");
    if (!o.value(QStringLiteral("target")).toString().isEmpty()) part(QString::fromUtf8("↪"), "#ff8a80");
    if (o.value(QStringLiteral("exit")).toBool()) part(QString::fromUtf8("выход"), "#9fb3c8");
    return parts;
}

QVariantList Engine::choiceBadges(const QString& text) const
{
    QVariantList out;
    const QStringList lines = pySplitLines(text);
    for (int i = 0; i < lines.size(); ++i) {
        const QString s = pyStrip(lines[i]);
        if (!isChoiceItemLine(s) || (!s.contains(QLatin1Char('[')) && !s.contains(QLatin1String("->")))) continue;
        const QVariantList parts = choiceBadgeParts(choiceOptionMap(s));
        if (!parts.isEmpty()) out << QVariantMap{{QStringLiteral("line"), i + 1}, {QStringLiteral("parts"), parts}};
    }
    return out;
}

QVariantMap Engine::choiceOutline(const QString& block) const
{
    const QStringList lines = pySplitLines(block);
    QVariantMap head, timeout;
    QVariantList options;
    int depth = 0, end = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const QString s = pyStrip(lines[i]);
        const QString w = firstWord(s);
        const QString cmd = normalizeCommand(w);
        if (cmd == QLatin1String("choice")) {
            if (head.isEmpty()) {
                const ChoiceHead h = parseChoiceHead(pyStrip(s.mid(w.size())));
                head = {{QStringLiteral("line"), i + 1}, {QStringLiteral("style"), h.style}, {QStringLiteral("secs"), h.secs.toDouble()},
                        {QStringLiteral("loop"), h.loop}, {QStringLiteral("random"), h.random}};
                depth = 1;
            } else if (depth > 0) {
                ++depth;
            }
            continue;
        }
        if (depth == 0) continue;
        if (cmd == QLatin1String("endchoice")) {
            if (--depth == 0) { end = i + 1; break; }
            continue;
        }
        if (depth != 1) continue;
        if (isChoiceItemLine(s)) {
            QVariantMap o = choiceOptionMap(s);
            o.insert(QStringLiteral("line"), i + 1);
            o.insert(QStringLiteral("badges"), choiceBadgeParts(o));
            options << o;
        } else if (s.contains(QLatin1String("->")) && isTimeoutWord(s.section(QStringLiteral("->"), 0, 0))) {
            timeout = {{QStringLiteral("line"), i + 1}, {QStringLiteral("target"), pyStrip(s.section(QStringLiteral("->"), 1))}};
        }
    }
    if (!end) end = int(lines.size()) + 1;
    for (int k = 0; k < options.size(); ++k) {
        QVariantMap o = options[k].toMap();
        const int from = o.value(QStringLiteral("line")).toInt();
        int to = (k + 1 < options.size() ? options[k + 1].toMap().value(QStringLiteral("line")).toInt() : end) - 1;
        const int late = timeout.value(QStringLiteral("line")).toInt();
        if (late > from && late <= to) to = late - 1;
        int body = 0, first = 0;
        for (int l = from; l < to && l < lines.size(); ++l) {
            if (pyStrip(lines[l]).isEmpty()) continue;
            if (!first) first = l + 1;
            ++body;
        }
        o.insert(QStringLiteral("bodyTo"), to);
        o.insert(QStringLiteral("body"), body);
        // the first line under the option - the card's «Что ответят»
        o.insert(QStringLiteral("firstBody"), first);
        o.insert(QStringLiteral("firstBodyText"), first ? pyStrip(lines[first - 1]) : QString());
        options[k] = o;
    }
    return {{QStringLiteral("head"), head}, {QStringLiteral("options"), options}, {QStringLiteral("timeout"), timeout}, {QStringLiteral("end"), end}};
}

QString Engine::choiceItemLine(const QVariantMap& o) const
{
    auto num = [](double d) { return d == std::floor(d) ? QString::number(qint64(d)) : QString::number(d); };
    QString s = QStringLiteral("- ") + o.value(QStringLiteral("caption")).toString().trimmed();
    for (const QVariant& pv : o.value(QStringLiteral("points")).toList()) {
        const QVariantMap p = pv.toMap();
        const QString v = p.value(QStringLiteral("v")).toString().trimmed();
        const double n = p.value(QStringLiteral("n")).toDouble();
        if (!v.isEmpty() && n != 0) s += QStringLiteral(" [%1%2 %3]").arg(n > 0 ? QStringLiteral("+") : QString(), num(n), v);
    }
    QString need = o.value(QStringLiteral("need")).toString().trimmed();
    const QString needVar = o.value(QStringLiteral("needVar")).toString().trimmed();
    if (!needVar.isEmpty()) need = needVar + QLatin1Char(' ') + num(o.value(QStringLiteral("needN")).toDouble());
    const QString hint = o.value(QStringLiteral("hint")).toString().trimmed();
    if (!need.isEmpty()) s += QString::fromUtf8(" [нужно ") + need + (hint.isEmpty() ? QString() : QStringLiteral(" | ") + hint) + QLatin1Char(']');
    const QString cond = o.value(QStringLiteral("cond")).toString().trimmed();
    if (!cond.isEmpty()) s += QString::fromUtf8(" [если ") + cond + QLatin1Char(']');
    for (const QString& who : o.value(QStringLiteral("remember")).toStringList())
        if (!who.trimmed().isEmpty()) s += QString::fromUtf8(" [запомнит ") + who.trimmed() + QLatin1Char(']');
    for (const QString& f : o.value(QStringLiteral("flags")).toStringList())
        if (!f.trimmed().isEmpty()) s += QString::fromUtf8(" [флаг ") + f.trimmed() + QLatin1Char(']');
    if (o.value(QStringLiteral("exit")).toBool()) s += QString::fromUtf8(" [выход]");
    if (o.value(QStringLiteral("always")).toBool()) s += QString::fromUtf8(" [всегда]");
    const QString target = o.value(QStringLiteral("target")).toString().trimmed();
    if (!target.isEmpty()) s += QStringLiteral(" -> ") + target;
    const QString image = o.value(QStringLiteral("image")).toString().trimmed();
    if (!image.isEmpty()) s += QStringLiteral(" | ") + image;
    return s;
}

QString Engine::choiceHeadLine(const QVariantMap& h) const
{
    const QString style = h.value(QStringLiteral("style")).toString();
    QString s = QString::fromUtf8("выбор");
    if (style == QLatin1String("images")) s += QString::fromUtf8(" визуальный");
    else if (style == QLatin1String("buttons")) s += QString::fromUtf8(" кнопки");
    else if (style == QLatin1String("phone")) s += QString::fromUtf8(" телефон");
    else if (style == QLatin1String("timed")) s += QString::fromUtf8(" на время ") + QString::number(qBound(2, int(h.value(QStringLiteral("secs")).toDouble()), 60));
    if (h.value(QStringLiteral("loop")).toBool()) s += QString::fromUtf8(" по кругу");
    if (h.value(QStringLiteral("random")).toBool()) s += QString::fromUtf8(" наугад");
    return s;
}

QVariantMap Engine::choiceBlockAt(const QString& storyText, int line) const
{
    const QStringList lines = pySplitLines(storyText);
    const int L = line - 1;
    if (L < 0 || L >= lines.size()) return {};
    int header = -1, depth = 0;
    for (int i = L; i >= 0; --i) {
        const QString s = pyStrip(lines[i]);
        const QString cmd = normalizeCommand(firstWord(s));
        if (i < L && (s.startsWith(QLatin1Char(':')) || cmd == QLatin1String("label"))) break;
        if (cmd == QLatin1String("endchoice") && i < L) { ++depth; continue; }
        if (cmd == QLatin1String("choice")) {
            if (depth == 0) { header = i; break; }
            --depth;
        }
    }
    if (header < 0) return {};
    int end = int(lines.size()) - 1;
    depth = 1;
    for (int k = header + 1; k < lines.size(); ++k) {
        const QString s = pyStrip(lines[k]);
        const QString cmd = normalizeCommand(firstWord(s));
        if (s.startsWith(QLatin1Char(':')) || cmd == QLatin1String("label")) { end = k - 1; break; }
        if (cmd == QLatin1String("choice")) ++depth;
        else if (cmd == QLatin1String("endchoice") && --depth == 0) { end = k; break; }
    }
    while (end > header && pyStrip(lines[end]).isEmpty()) --end;
    if (L > end) return {};
    return {{QStringLiteral("from"), header + 1}, {QStringLiteral("to"), end + 1},
            {QStringLiteral("text"), QStringList(lines.mid(header, end - header + 1)).join(QLatin1Char('\n'))}};
}

QVariantList Engine::choiceIssues(const QString& storyText, int from, int to, const QString& block) const
{
    const QStringList lines = pySplitLines(storyText);
    const QStringList b = pySplitLines(block);
    const int f = qBound(1, from, int(lines.size()) + 1);
    QStringList all = lines.mid(0, f - 1);
    all << b;
    all << (to >= f ? lines.mid(to) : lines.mid(f - 1));
    QVariantList out;
    for (const QVariant& v : lint(all.join(QLatin1Char('\n')))) {
        QVariantMap m = v.toMap();
        const int ln = m.value(QStringLiteral("line")).toInt();
        if (ln < f || ln >= f + b.size()) continue;
        m.insert(QStringLiteral("line"), ln - f + 1);
        out << m;
    }
    return out;
}

QVariantList Engine::choiceRects(const QString& storyText, int line, const QString& block) const
{
    QStringList lines = pySplitLines(storyText).mid(0, qMax(0, line));
    lines << block.split(QLatin1Char('\n'));
    const SceneState st = sceneAt(lines.join(QLatin1Char('\n')), int(lines.size()), &m_es);
    QVariantList out;
    for (const QRectF& r : m_renderer.choiceRects(st))
        out << QVariantMap{{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()}, {QStringLiteral("w"), r.width()}, {QStringLiteral("h"), r.height()}};
    return out;
}

QVariantMap Engine::suggest(const QString& lineText, int col, const QString& fullText) const
{
    const QString before = lineText.left(col);
    {
        // «- Вариант [»: what the option's brackets know - and after the word, the names it counts with
        const int ob = int(before.lastIndexOf(QLatin1Char('['))), cb = int(before.lastIndexOf(QLatin1Char(']')));
        if (isChoiceItemLine(pyStrip(lineText)) && ob > cb) {
            const QString inside = before.mid(ob + 1);
            const int sp = int(inside.lastIndexOf(QLatin1Char(' ')));
            const QString pre = (sp < 0 ? inside : inside.mid(sp + 1)).toLower();
            const int at = ob + 1 + (sp < 0 ? 0 : sp + 1);
            QVariantList items;
            QSet<QString> seen;
            auto offer = [&](const QString& text, const QString& hint, const QString& kind) {
                if (items.size() >= 40 || text.isEmpty() || seen.contains(text)) return;
                if (!pre.isEmpty() && !text.toLower().startsWith(pre)) return;
                if (text.toLower() == pre) return;
                seen.insert(text);
                items << QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("hint"), hint}, {QStringLiteral("kind"), kind}};
            };
            if (sp < 0) {
                offer(QStringLiteral("+1"), U("очки: [+1 Славя]"), QStringLiteral("cmd"));
                offer(QStringLiteral("-1"), U("минус очки: [-1 Алиса]"), QStringLiteral("cmd"));
                offer(U("нужно"), U("замок: [нужно Славя 3]"), QStringLiteral("cmd"));
                offer(U("если"), U("появится, если: [если ключ]"), QStringLiteral("cmd"));
                offer(U("запомнит"), U("«Славя это запомнит»"), QStringLiteral("cmd"));
                offer(U("флаг"), U("отметка на потом: [флаг помог]"), QStringLiteral("cmd"));
                offer(U("выход]"), U("закончить расспросы (по кругу)"), QStringLiteral("cmd"));
                offer(U("всегда]"), U("не пропадает (по кругу)"), QStringLiteral("cmd"));
            } else {
                const QString w0 = firstWord(inside).toLower();
                const bool counts = w0.startsWith(QLatin1Char('+')) || w0.startsWith(QLatin1Char('-')) || w0 == U("нужно") || w0 == U("если");
                if (w0 == U("если") && sp == int(firstWord(inside).size())) {
                    offer(U("предмет"), U("есть предмет"), QStringLiteral("cmd"));
                    offer(U("не"), U("наоборот"), QStringLiteral("cmd"));
                }
                if (counts) for (const QString& v : storyVarNames(fullText)) offer(v, U("очки"), QStringLiteral("code"));
                if (counts || w0 == U("запомнит"))
                    for (const QVariant& v : cast()) offer(v.toMap().value(QStringLiteral("name")).toString(), U("героиня"), QStringLiteral("char"));
            }
            return {{QStringLiteral("start"), at}, {QStringLiteral("items"), items}};
        }
    }
    const int start = int(before.lastIndexOf(QRegularExpression(QStringLiteral("\\s")))) + 1;
    const QString prefix = before.mid(start).toLower();
    QVariantList items;
    QSet<QString> seen;
    auto offer = [&](const QString& text, const QString& hint, const QString& kind) {
        if (items.size() >= 40 || text.isEmpty() || seen.contains(text)) return;
        if (!prefix.isEmpty() && !text.toLower().startsWith(prefix)) return;
        if (text.toLower() == prefix) return;
        seen.insert(text);
        items << QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("hint"), hint}, {QStringLiteral("kind"), kind}};
    };
    const QStringList words = pySplit(before.left(start));
    auto effects = [&] { for (const char* e : {"dissolve", "fade", "fade2", "fade3", "dspr", "pixellate", "moveinleft", "moveinright", "hpunch", "vpunch", "none"}) offer(U(e), U("переход"), QStringLiteral("fx")); };
    auto positions = [&] { for (const char* p : {"left", "center", "right", "fleft", "fright", "cleft", "cright"}) offer(U(p), U("позиция"), QStringLiteral("pos")); };
    auto scenes = [&] { for (const QString& s : sceneNames(fullText)) offer(slug(s, QStringLiteral("label"), false), s, QStringLiteral("scene")); };
    if (words.isEmpty()) {
        for (const QVariant& fv : m_forms.forms()) {           // every variant's command word
            const QVariantMap f = fv.toMap();
            QStringList ts{f.value(QStringLiteral("build")).toString()};
            for (const QVariant& b : f.value(QStringLiteral("builds")).toMap()) ts << b.toString();
            for (const QString& t : ts) {
                int i = 0;
                while (i < t.size() && !QStringLiteral(" [{\n").contains(t.at(i))) ++i;
                if (i > 1) offer(t.left(i), f.value(QStringLiteral("title")).toString(), QStringLiteral("cmd"));
            }
        }
        for (const QVariant& v : cast()) offer(v.toMap().value(QStringLiteral("name")).toString() + QLatin1Char(':'), U("реплика"), QStringLiteral("say"));
        offer(U("Семён:"), U("реплика"), QStringLiteral("say"));
        return {{QStringLiteral("start"), start}, {QStringLiteral("items"), items}};
    }
    const QString cmd = normalizeCommand(words.first());
    const int arg = int(words.size()) - 1;
    static const QSet<QString> spriteCmds{QStringLiteral("show"), QStringLiteral("mirror"), QStringLiteral("mirrorbig"), QStringLiteral("bigshow"),
                                          QStringLiteral("pulse"), QStringLiteral("walk"), QStringLiteral("enterleft"), QStringLiteral("enterright")};
    if (spriteCmds.contains(cmd)) {
        if (arg == 0) {
            for (const QString& t : m_es.spriteTags()) offer(t, m_es.characterName(t), QStringLiteral("char"));
            const auto custom = m_renderer.customImages();
            for (auto it = custom.begin(); it != custom.end(); ++it)
                if (!it.key().startsWith(QLatin1String("bg ")) && !it.key().startsWith(QLatin1String("cg "))) offer(it.key().section(QLatin1Char(' '), 0, 0), U("свой"), QStringLiteral("char"));
        } else if (arg == 1) {
            for (const QString& e : spriteEmotions(words[1])) offer(e, U("эмоция"), QStringLiteral("code"));
        } else if (arg == 2) {
            for (const QString& n : m_es.spriteNames(words[1]))
                if (n.section(QLatin1Char(' '), 0, 0) == words[2]) offer(n.section(QLatin1Char(' '), 1), U("одежда"), QStringLiteral("code"));
            if (const Wardrobe* wr = gb::wardrobe())
                for (const Wardrobe::Outfit& o : wr->outfits(words[1]))
                    if (wr->resolve(words[1] + QLatin1Char(' ') + words[2] + QLatin1Char(' ') + o.part + (o.dists.contains(QString()) ? QString() : QLatin1Char(' ') + o.dists.value(0))))
                        offer(o.part, o.adult ? U("мастерская 18+") : U("мастерская"), QStringLiteral("code"));
            positions();
        } else {
            positions();
            for (const char* d : {"close", "far"}) offer(U(d), U("дистанция"), QStringLiteral("code"));
            effects();
        }
    } else if (cmd == QLatin1String("hide")) {
        if (arg == 0) for (const QString& t : m_es.spriteTags()) offer(t, m_es.characterName(t), QStringLiteral("char"));
        else effects();
    } else if (cmd == QLatin1String("bg") || cmd == QLatin1String("showbg")) {
        if (arg == 0) for (const QVariant& b : backgrounds()) offer(b.toMap().value(QStringLiteral("id")).toString(), U("фон"), QStringLiteral("bg"));
        else effects();
    } else if (cmd == QLatin1String("cg")) {
        if (arg == 0) for (const QVariant& b : cgs()) offer(b.toMap().value(QStringLiteral("id")).toString(), QStringLiteral("CG"), QStringLiteral("bg"));
        else effects();
    } else if (cmd == QLatin1String("music") || cmd == QLatin1String("musicqueue")) {
        for (auto it = m_es.music.begin(); it != m_es.music.end(); ++it) offer(it.key(), U("трек"), QStringLiteral("music"));
        for (const char* o : {"fadein", "loop", "noloop"}) offer(U(o), U("опция"), QStringLiteral("fx"));
    } else if (cmd == QLatin1String("sound") || cmd == QLatin1String("voice")) {
        for (auto it = m_es.sounds.begin(); it != m_es.sounds.end(); ++it) offer(it.key(), U("звук"), QStringLiteral("sfx"));
    } else if (cmd == QLatin1String("ambience")) {
        for (auto it = m_es.ambience.begin(); it != m_es.ambience.end(); ++it) offer(it.key(), U("атмосфера"), QStringLiteral("sfx"));
    } else if (cmd == QLatin1String("jump") || cmd == QLatin1String("callscene")) {
        scenes();
    } else if (cmd == QLatin1String("weather")) {
        for (const char* w : {"снег", "дождь", "листья", "сердца", "искры", "пыль", "стоп"}) offer(U(w), U("погода"), QStringLiteral("fx"));
    } else if (cmd == QLatin1String("colorfilter")) {
        for (const char* w : {"сепия", "чб", "ночь", "тепло", "холод", "сон", "выцвет", "хоррор", "нет"}) offer(U(w), U("фильтр"), QStringLiteral("fx"));
    } else if (cmd == QLatin1String("timeofday")) {
        for (const char* w : {"день", "вечер", "ночь", "пролог"}) offer(U(w), U("время"), QStringLiteral("fx"));
    } else if (cmd == QLatin1String("effect")) {
        effects();
    } else if (cmd == QLatin1String("shake")) {
        for (const char* w : {"слабо", "сильно", "вертикально", "hpunch", "vpunch"}) offer(U(w), U("тряска"), QStringLiteral("fx"));
    } else if (cmd == QLatin1String("sms") || cmd == QLatin1String("phonestart")) {
        if (arg == 0) {
            for (const QVariant& v : cast()) offer(v.toMap().value(QStringLiteral("name")).toString() + (cmd == QLatin1String("sms") ? QStringLiteral(":") : QString()), U("контакт"), QStringLiteral("say"));
            if (cmd == QLatin1String("sms")) offer(U("я:"), U("своё"), QStringLiteral("say"));
        }
    } else if (cmd == QLatin1String("ifjump") || cmd == QLatin1String("ifpersistent") || cmd == QLatin1String("ifitem")) {
        if (before.contains(QLatin1String("->"))) scenes();
    }
    return {{QStringLiteral("start"), start}, {QStringLiteral("items"), items}};
}

// ================================================================== projects

QString Engine::projectDir(const QString& id) const { return m_root + QStringLiteral("/projects/") + id; }
QString Engine::assetsDir(const QString& id) const { return projectDir(id) + QStringLiteral("/assets"); }

void Engine::refreshProjects()
{
    QVariantList list;
    for (const QFileInfo& fi : QDir(m_root + QStringLiteral("/projects")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (fi.fileName().startsWith(QLatin1Char('_'))) continue;
        const QString story = fi.absoluteFilePath() + QStringLiteral("/story.txt");
        if (!QFileInfo::exists(story)) continue;
        const QJsonObject meta = QJsonDocument::fromJson(readFile(fi.absoluteFilePath() + QStringLiteral("/project.json"))).object();
        const QString text = QString::fromUtf8(readFile(story));
        const QDateTime mod = QFileInfo(story).lastModified();
        list << QVariantMap{{QStringLiteral("id"), fi.fileName()}, {QStringLiteral("name"), meta.value(QStringLiteral("name")).toString(fi.fileName())},
                            {QStringLiteral("modified"), mod.toString(QStringLiteral("dd.MM.yyyy HH:mm"))}, {QStringLiteral("stamp"), mod.toMSecsSinceEpoch()},
                            {QStringLiteral("lines"), int(pySplitLines(text).size())}, {QStringLiteral("scenes"), int(sceneNames(text).size())}};
    }
    std::sort(list.begin(), list.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("stamp")).toLongLong() > b.toMap().value(QStringLiteral("stamp")).toLongLong();
    });
    m_projects = list;
    emit projectsChanged();
}

QString Engine::createProject(const QString& name, bool example)
{
    const QString clean = pyStrip(name).isEmpty() ? U("Мой мод") : pyStrip(name);
    const QString base = slug(clean, QStringLiteral("mod"), false);
    QString id = base;
    // free here AND in the game: another copy of GenryBL may have a mod of that name in game/mods
    auto taken = [this](const QString& i) {
        return QFileInfo::exists(projectDir(i)) ||
               (!m_es.esRoot().isEmpty() && QFileInfo::exists(m_es.esRoot() + QStringLiteral("/game/mods/genry_") + i));
    };
    for (int n = 2; taken(id); ++n) id = base + QStringLiteral("_%1").arg(n);
    for (const char* sub : {"/assets/images", "/assets/audio"}) QDir().mkpath(projectDir(id) + QLatin1String(sub));
    writeFile(projectDir(id) + QStringLiteral("/project.json"),
              QJsonDocument(QJsonObject{{QStringLiteral("name"), clean}, {QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate)}}).toJson());
    writeFile(projectDir(id) + QStringLiteral("/story.txt"), starter(m_root, QStringLiteral("genry_") + id, clean, m_settings.value(QStringLiteral("author")).toString().trimmed(), example).toUtf8());
    refreshProjects();
    return id;
}

bool Engine::openProject(const QString& id)
{
    if (!QFileInfo::exists(projectDir(id) + QStringLiteral("/story.txt"))) return false;
    m_current = id;
    m_renderer.setCustomImages(build::customImageFiles(assetsDir(id)));
    setSetting(QStringLiteral("lastProject"), id);
    emit currentProjectChanged();
    emit assetsChanged();
    return true;
}

QString Engine::currentProjectName() const
{
    for (const QVariant& p : m_projects)
        if (p.toMap().value(QStringLiteral("id")).toString() == m_current) return p.toMap().value(QStringLiteral("name")).toString();
    return m_current;
}

QString Engine::loadStory(const QString& id) const { return stripBom(QString::fromUtf8(readFile(projectDir(id) + QStringLiteral("/story.txt")))); }

bool Engine::saveStory(const QString& id, const QString& text)
{
    if (id.isEmpty()) return false;
    if (m_shotMode) return true;
    const QString path = projectDir(id) + QStringLiteral("/story.txt");
    if (loadStory(id) == text) return true;
    const bool ok = writeFile(path, text.toUtf8());
    if (!ok) emit toast(QStringLiteral("Не удалось сохранить %1").arg(QDir::toNativeSeparators(path)), 2);
    return ok;
}

bool Engine::renameProject(const QString& id, const QString& name)
{
    const QString p = projectDir(id) + QStringLiteral("/project.json");
    QJsonObject meta = QJsonDocument::fromJson(readFile(p)).object();
    meta.insert(QStringLiteral("name"), pyStrip(name));
    const bool ok = writeFile(p, QJsonDocument(meta).toJson());
    refreshProjects();
    emit currentProjectChanged();
    return ok;
}

QString Engine::duplicateProject(const QString& id)
{
    const QJsonObject meta = QJsonDocument::fromJson(readFile(projectDir(id) + QStringLiteral("/project.json"))).object();
    const QString copy = createProject(meta.value(QStringLiteral("name")).toString(id) + U(" (копия)"));
    QString story = loadStory(id);
    story.replace(QRegularExpression(QStringLiteral("(?m)^@mod_id .*$")), QStringLiteral("@mod_id genry_") + copy);
    writeFile(projectDir(copy) + QStringLiteral("/story.txt"), story.toUtf8());
    QDirIterator it(assetsDir(id), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString dst = assetsDir(copy) + QLatin1Char('/') + QDir(assetsDir(id)).relativeFilePath(f);
        QDir().mkpath(QFileInfo(dst).absolutePath());
        QFile::copy(f, dst);
    }
    refreshProjects();
    return copy;
}

bool Engine::trashProject(const QString& id)
{
    const QString trash = m_root + QStringLiteral("/projects/_trash");
    QDir().mkpath(trash);
    const bool ok = QDir().rename(projectDir(id), trash + QLatin1Char('/') + id + QLatin1Char('_') +
                                                      QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    if (ok && m_current == id) { m_current.clear(); emit currentProjectChanged(); }
    refreshProjects();
    return ok;
}

QString Engine::coverUrl(const QString& id) const
{
    return QStringLiteral("image://gb/cover/%1/%2").arg(id).arg(QFileInfo(projectDir(id) + QStringLiteral("/story.txt")).lastModified().toMSecsSinceEpoch());
}

SceneState Engine::coverScene(const QString& id) const
{
    const QString text = loadStory(id);
    const int n = qMin(int(pySplitLines(text).size()), 160);
    SceneState best;
    for (int i = 1; i <= n; ++i) {
        const SceneState s = sceneAt(text, i, &m_es);
        if (!s.bg.isEmpty()) best = s;
        if (!s.bg.isEmpty() && !s.sprites.isEmpty() && !s.text.isEmpty()) return s;
    }
    return best;
}

QVariantList Engine::customImages() const
{
    QVariantList out;
    const auto custom = m_renderer.customImages();
    for (auto it = custom.begin(); it != custom.end(); ++it)
        out << QVariantMap{{QStringLiteral("name"), it.key()}, {QStringLiteral("file"), QUrl::fromLocalFile(it.value()).toString()}};
    return out;
}

QString Engine::importImage(const QString& fileUrl, const QString& kind, const QString& name)
{
    if (m_current.isEmpty()) return {};
    const QImage img(localPath(fileUrl));
    if (img.isNull()) { emit toast(U("Это не картинка"), 2); return {}; }
    return saveProjectImage(img, kind, name);
}

QString Engine::saveProjectImage(const QImage& img, const QString& kind, const QString& name)
{
    if (m_current.isEmpty()) return {};
    QString n = slug(name, QStringLiteral("image"), false);
    if (kind == QLatin1String("sprite")) {
        // "Тётя Галя смеётся" -> tag + attribute: first word is the character
        const QStringList parts = pySplit(name);
        n = slug(parts.value(0), QStringLiteral("genry_char"), false);
        if (parts.size() > 1) n += QLatin1Char(' ') + slug(parts.mid(1).join(QLatin1Char('_')), QStringLiteral("normal"), false);
    } else if (kind == QLatin1String("bg") || kind == QLatin1String("cg")) {
        n = kind + QLatin1Char(' ') + n;
    }
    // a background / CG: the screen's 1920×1080 (what the preview shows and the game draws), a photo as JPG like
    // the game's own; a giant sprite comes down to the game's height. Was: a 4000×3000 PNG of 20+ MB in the mod.
    QImage pic = img;
    QString ext = QStringLiteral(".png");
    QString note;
    if (kind == QLatin1String("bg") || kind == QLatin1String("cg")) {
        if (pic.size() != QSize(1920, 1080)) {
            note = U(" (подогнал %1×%2 под экран 1920×1080)").arg(pic.width()).arg(pic.height());
            pic = pic.scaled(1920, 1080, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            pic = pic.copy((pic.width() - 1920) / 2, (pic.height() - 1080) / 2, 1920, 1080);
        }
        if (!pic.hasAlphaChannel()) ext = QStringLiteral(".jpg");
    } else if (kind == QLatin1String("sprite") && pic.height() > 1500) {
        note = U(" (уменьшил %1×%2 до высоты 1080, как спрайты БЛ)").arg(pic.width()).arg(pic.height());
        pic = pic.scaledToHeight(1080, Qt::SmoothTransformation);
    }
    const QString base = assetsDir(m_current) + QStringLiteral("/images/") + n;
    const QString dst = base + ext;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    for (const char* other : {".png", ".jpg", ".jpeg", ".webp"})      // the same name in another format would win half the time
        if (base + QLatin1String(other) != dst) QFile::remove(base + QLatin1String(other));
    if (!pic.save(dst, ext == QLatin1String(".jpg") ? "JPG" : "PNG", ext == QLatin1String(".jpg") ? 92 : -1)) {
        emit toast(U("Не удалось сохранить картинку"), 2);
        return {};
    }
    m_renderer.setCustomImages(build::customImageFiles(assetsDir(m_current)));
    emit assetsChanged();
    emit toast(U("Добавлено: ") + n + note, 0);
    return n;
}

QString Engine::importAudio(const QString& fileUrl)
{
    if (m_current.isEmpty() || m_busy) return {};
    const QString src = localPath(fileUrl);
    const QFileInfo fi(src);
    const QString name = slug(fi.completeBaseName(), QStringLiteral("track"), false) + QStringLiteral(".ogg");
    const QString dst = assetsDir(m_current) + QStringLiteral("/audio/") + name;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (fi.suffix().compare(QLatin1String("ogg"), Qt::CaseInsensitive) == 0) {
        QFile::remove(dst);
        QFile::copy(src, dst);
        emit assetsChanged();
        return QStringLiteral("audio/") + name;
    }
    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty()) { emit toast(U("Нужен ffmpeg (в PATH) или сразу .ogg"), 2); return {}; }
    setBusy(true, U("Конвертирую ") + fi.fileName() + U(" в ogg…"));
    auto* w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, name] {
        const bool ok = w->result();
        w->deleteLater();
        setBusy(false);
        emit assetsChanged();
        emit toast(ok ? U("Файл готов: audio/") + name : U("ffmpeg не смог сконвертировать"), ok ? 0 : 2);
    });
    w->setFuture(QtConcurrent::run([ffmpeg, src, dst] {
        QProcess p;
        p.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-i"), src, QStringLiteral("-vn"), QStringLiteral("-c:a"), QStringLiteral("libvorbis"),
                         QStringLiteral("-q:a"), QStringLiteral("5"), dst});
        return p.waitForFinished(300000) && p.exitCode() == 0 && QFileInfo(dst).size() > 0;
    }));
    return QStringLiteral("audio/") + name;
}

QString Engine::ffmpegPath() const
{
    const QString bundled = m_root + QStringLiteral("/data/tools/ffmpeg.exe");
    return QFileInfo::exists(bundled) ? bundled : QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QStringList Engine::importAudioFiles(const QVariantList& fileUrls)
{
    if (m_current.isEmpty() || m_busy || fileUrls.isEmpty()) return {};
    const QString dir = assetsDir(m_current) + QStringLiteral("/audio");
    QDir().mkpath(dir);
    // voice packs are often alisa/001.ogg + lena/001.ogg: every file gets its own name, and a
    // file already used by earlier lines is never overwritten
    QStringList rels;
    QVector<QPair<QString, QString>> convert;     // src -> dst, done in order in the background
    QSet<QString> taken;
    for (const QVariant& u : fileUrls) {
        const QString src = localPath(u.toString());
        const QFileInfo fi(src);
        if (!fi.isFile()) continue;
        const QString base = slug(fi.completeBaseName(), QStringLiteral("voice"), false);
        QString name = base + QStringLiteral(".ogg");
        for (int n = 2; taken.contains(name) || QFileInfo::exists(dir + QLatin1Char('/') + name); ++n)
            name = QStringLiteral("%1_%2.ogg").arg(base).arg(n);
        taken.insert(name);
        const QString dst = dir + QLatin1Char('/') + name;
        if (fi.suffix().compare(QLatin1String("ogg"), Qt::CaseInsensitive) == 0) QFile::copy(src, dst);
        else convert.push_back({src, dst});
        rels << QStringLiteral("audio/") + name;
    }
    if (convert.isEmpty()) {
        emit assetsChanged();
        emit audioImported(rels, true);
        return rels;
    }
    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty()) {
        emit toast(U("Нужен ffmpeg (в PATH) или сразу .ogg"), 2);
        return {};
    }
    setBusy(true, U("Озвучка → ogg: %1 файл(ов)…").arg(convert.size()));
    auto* w = new QFutureWatcher<int>(this);
    connect(w, &QFutureWatcher<int>::finished, this, [this, w, rels, total = int(convert.size())] {
        const int ok = w->result();
        w->deleteLater();
        setBusy(false);
        emit assetsChanged();
        emit audioImported(rels, ok == total);
        emit toast(ok == total ? U("Озвучка готова: %1 файл(ов)").arg(rels.size()) : U("ffmpeg не смог: %1 из %2").arg(total - ok).arg(total),
                   ok == total ? 0 : 2);
    });
    w->setFuture(QtConcurrent::run([ffmpeg, convert] {
        int ok = 0;
        for (const auto& job : convert) {
            QProcess p;
            p.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-i"), job.first, QStringLiteral("-vn"), QStringLiteral("-c:a"),
                             QStringLiteral("libvorbis"), QStringLiteral("-q:a"), QStringLiteral("5"), job.second});
            if (p.waitForFinished(300000) && p.exitCode() == 0 && QFileInfo(job.second).size() > 0) ++ok;
        }
        return ok;
    }));
    return rels;
}

QVariantMap Engine::modTitle(const QString& text) const
{
    const ModMeta m = parseMeta(pySplitLines(stripBom(text)), nullptr, options());
    return {{QStringLiteral("name"), m.modName}, {QStringLiteral("font"), m.titleFont}, {QStringLiteral("color"), m.titleColor},
            {QStringLiteral("size"), m.titleSize}, {QStringLiteral("style"), m.titleStyle}, {QStringLiteral("author"), m.author},
            {QStringLiteral("hero"), m.heroName}, {QStringLiteral("heroAsk"), m.heroAsk},
            {QStringLiteral("heroShe"), m.heroShe}};
}

QString Engine::applyModTitle(const QString& text, const QVariantMap& t)
{
    // the author given here is also the one the next new mod starts with
    if (t.contains(QStringLiteral("author"))) m_settings.setValue(QStringLiteral("author"), t.value(QStringLiteral("author")).toString().trimmed());
    // replace / add / drop the @mod_name and @mod_title_* lines, leave everything else alone
    const QList<QPair<QString, QString>> want{
        {QStringLiteral("mod_name"), t.value(QStringLiteral("name")).toString().trimmed()},
        {QStringLiteral("mod_title_font"), t.value(QStringLiteral("font")).toString().trimmed()},
        {QStringLiteral("mod_title_color"), t.value(QStringLiteral("color")).toString().trimmed()},
        {QStringLiteral("mod_title_size"), t.value(QStringLiteral("size")).toString().trimmed()},
        {QStringLiteral("mod_title_style"), t.value(QStringLiteral("style")).toString().trimmed()},
        {QStringLiteral("author"), t.value(QStringLiteral("author")).toString().trimmed()},
        {QStringLiteral("hero_name"), t.value(QStringLiteral("hero")).toString().trimmed()},
        {QStringLiteral("hero_ask"), t.value(QStringLiteral("heroAsk")).toString().trimmed()},
        {QStringLiteral("hero_gender"), t.value(QStringLiteral("heroShe")).toBool() ? U("она") : QString()}};
    QStringList lines = stripBom(text).split(QLatin1Char('\n'));
    int anchor = -1;
    for (const auto& kv : want) {
        int found = -1;
        for (int i = 0; i < lines.size(); ++i) {
            const QString s = lines[i].trimmed();
            if (s.startsWith(QLatin1Char('@')) && pySplit(s.mid(1), 1).value(0).toLower() == kv.first) { found = i; break; }
        }
        if (kv.second.isEmpty()) {
            if (found >= 0 && kv.first != QLatin1String("mod_name")) lines.removeAt(found);
            continue;
        }
        const QString line = QLatin1Char('@') + kv.first + QLatin1Char(' ') + kv.second;
        if (found >= 0) { lines[found] = line; anchor = found; continue; }
        if (anchor < 0)
            for (int i = 0; i < lines.size(); ++i)
                if (lines[i].trimmed().startsWith(QLatin1Char('@'))) anchor = i;
        lines.insert(anchor + 1, line);
        anchor = anchor + 1;
    }
    return lines.join(QLatin1Char('\n'));
}

QString Engine::fontFamilyFor(const QString& ref)
{
    QMutexLocker lock(&m_fontMx);
    if (ref.isEmpty()) return m_renderer.headerFamily();
    auto it = m_fontFamilies.constFind(ref);
    if (it != m_fontFamilies.constEnd()) return *it;
    QByteArray data;
    if (ref.startsWith(QLatin1String("es:"))) data = m_es.vfs().read(ref.mid(3));
    else if (!m_current.isEmpty()) data = readFile(assetsDir(m_current) + QLatin1Char('/') + ref);
    QString family;
    if (!data.isEmpty()) {
        const QStringList fams = QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFontFromData(data));
        family = fams.value(0);
    }
    m_fontFamilies.insert(ref, family);
    return family;
}

QVariantList Engine::modFonts()
{
    QVariantList out;
    QStringList game;
    for (const QString& f : m_es.vfs().archiveFiles())
        if (f.startsWith(QLatin1String("fonts/")) && (f.endsWith(QLatin1String(".ttf"), Qt::CaseInsensitive) || f.endsWith(QLatin1String(".otf"), Qt::CaseInsensitive)))
            game << f;
    // ES keeps its fonts loose in game/fonts
    for (const QString& f : QDir(m_es.vfs().gameDir() + QStringLiteral("/fonts")).entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf"), QStringLiteral("*.TTF")}, QDir::Files))
        if (!game.contains(QStringLiteral("fonts/") + f)) game << QStringLiteral("fonts/") + f;
    game.sort(Qt::CaseInsensitive);
    if (!m_current.isEmpty())
        for (const QString& f : QDir(assetsDir(m_current) + QStringLiteral("/fonts")).entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files))
            out << QVariantMap{{QStringLiteral("ref"), QStringLiteral("fonts/") + f}, {QStringLiteral("label"), U("★ ") + f},
                               {QStringLiteral("family"), fontFamilyFor(QStringLiteral("fonts/") + f)}};
    for (const QString& f : game) {
        const QString ref = QStringLiteral("es:") + f;
        const QString fam = fontFamilyFor(ref);
        if (!fam.isEmpty()) out << QVariantMap{{QStringLiteral("ref"), ref}, {QStringLiteral("label"), pyBasename(f)}, {QStringLiteral("family"), fam}};
    }
    return out;
}

QString Engine::importFont(const QString& fileUrl)
{
    if (m_current.isEmpty()) return {};
    const QFileInfo fi(localPath(fileUrl));
    const QString ext = fi.suffix().toLower();
    if (!fi.isFile() || (ext != QLatin1String("ttf") && ext != QLatin1String("otf"))) {
        emit toast(U("Нужен шрифт .ttf или .otf"), 2);
        return {};
    }
    const QString name = slug(fi.completeBaseName(), QStringLiteral("font"), false) + QLatin1Char('.') + ext;
    const QString dst = assetsDir(m_current) + QStringLiteral("/fonts/") + name;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    QFile::remove(dst);
    QFile::copy(fi.absoluteFilePath(), dst);
    emit toast(U("Шрифт в проекте: fonts/") + name, 0);
    return QStringLiteral("fonts/") + name;
}

QString Engine::modsListUrl(const QVariantMap& title)
{
    const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(title)).toJson(QJsonDocument::Compact);
    return QStringLiteral("image://gb/modslist/") + QString::fromLatin1(json.toBase64(QByteArray::Base64UrlEncoding));
}

QVariantList Engine::storySpeakers(const QString& text) const
{
    // who already talks in this story first (in order of appearance), then the ES cast
    QVariantList out;
    QSet<QString> seen;
    QHash<QString, QString> custom;               // name/id (lower) -> colour from «персонаж»
    auto colorOf = [&](const QString& name) {
        const QString key = name.toLower();
        if (custom.contains(key)) return custom.value(key);
        const QString id = speakers().value(key, key);
        const QString c = m_es.characterColor(id, QStringLiteral("day"));
        return m_es.characters.contains(id) ? c : QStringLiteral("#e8e8e8");
    };
    auto add = [&](const QString& raw) {
        const QString name = pyStrip(raw);
        if (name.isEmpty() || seen.contains(name.toLower())) return;
        seen.insert(name.toLower());
        out << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("color"), colorOf(name)}};
    };
    const QStringList lines = pySplitLines(stripBom(text));
    for (const QString& raw : lines) {                  // colours of declared characters first
        const QString s = pyStrip(raw);
        const QString w = firstWord(s);
        if (normalizeCommand(w) != QLatin1String("character")) continue;
        QStringList v = pySplit(pyStrip(s.mid(w.size())));
        if (v.size() < 2) continue;
        const QString color = pyIsHexColor(v.last()) ? v.takeLast() : QStringLiteral("#008000");
        custom.insert(v[0].toLower(), color);
        if (v.size() > 1) custom.insert(v.mid(1).join(QLatin1Char(' ')).toLower(), color);
    }
    for (const QString& raw : lines) {
        const QString s = pyStrip(raw);
        if (s.isEmpty() || s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char('@')) || s.startsWith(QLatin1Char(':'))) continue;
        const QString w = firstWord(s);
        const QString cmd = normalizeCommand(w);
        const QString rest = pyStrip(s.mid(w.size()));
        if (cmd == QLatin1String("character")) {
            QStringList v = pySplit(rest);
            if (v.size() > 1 && pyIsHexColor(v.last())) v.removeLast();
            add(v.size() > 1 ? v.mid(1).join(QLatin1Char(' ')) : v.value(0));
        } else if (cmd == QLatin1String("voicedsay") || cmd == QLatin1String("punchedsay")) {
            add(rest.section(QLatin1Char('|'), 0, 0));
        } else if (s.contains(QLatin1Char(':')) && !isCommandName(cmd) && !s.startsWith(QLatin1Char('-'))) {
            add(s.section(QLatin1Char(':'), 0, 0));
        }
    }
    for (const char* n : {"Семён", "Алиса", "Славя", "Лена", "Ульяна", "Мику", "Ольга Дмитриевна", "Электроник", "Шурик", "Женя", "Юля", "Виола"})
        add(U(n));
    return out;
}

// ================================================================== build / run

BuildEnv Engine::envFor(const QString& id) const
{
    BuildEnv e;
    e.esRoot = m_es.esRoot();
    e.dataDir = m_root + QStringLiteral("/data");
    e.assetsDir = assetsDir(id);
    e.backupsDir = m_root + QStringLiteral("/work/backups");
    e.saveDir = m_root + QStringLiteral("/work/es_saves");
    return e;
}

void Engine::setBusy(bool b, const QString& text)
{
    m_busy = b;
    m_busyText = text;
    emit busyChanged();
}

QString Engine::labelAtLine(const QString& text, int line) const
{
    const QStringList lines = pySplitLines(text);
    for (int i = qMin(line, int(lines.size())) - 1; i >= 0; --i) {
        const QString s = pyStrip(lines[i]);
        QString n;
        if (s.startsWith(QLatin1Char(':'))) n = pyStrip(s.mid(1));
        else if (normalizeCommand(firstWord(s)) == QLatin1String("label")) n = pyStrip(s.mid(firstWord(s).size()));
        else continue;
        return sceneLabel(parseMeta(pySplitLines(stripBom(text)), nullptr, options()).modId, n, options());
    }
    return {};
}

void Engine::play(const QString& id, const QString& text, int line)
{
    if (m_busy) { emit toast(U("Уже собираю, секунду"), 1); return; }
    saveStory(id, text);
    stopGame();
    const BuildEnv env = envFor(id);
    const CompileOptions opt = options();
    QString label = labelAtLine(text, line);
    setBusy(true, U("Собираю мод…"));
    auto log = [this](const QString& s) { QMetaObject::invokeMethod(this, [this, s] { setBusy(true, s); }, Qt::QueuedConnection); };
    auto* w = new QFutureWatcher<BuildReport>(this);
    connect(w, &QFutureWatcher<BuildReport>::finished, this, [this, w, env, label] {
        const BuildReport r = w->result();
        w->deleteLater();
        setBusy(false);
        if (!r.ok) { emit buildFinished(false, r.error); return; }
        QString target = label;
        if (target == QLatin1String("start") || !r.labels.contains(target)) target = r.meta.modId;
        QString err;
        m_gamePid = build::runAt(env, r.meta.modId, target, &err);
        if (!m_gamePid) { emit buildFinished(false, err); return; }
        m_watch.start();
        emit gameRunningChanged();
        emit buildFinished(true, target == r.meta.modId ? U("БЛ запускается с начала мода") : U("БЛ запускается со сцены «") + target + U("»"));
    });
    w->setFuture(QtConcurrent::run([this, env, text, opt, log] {
        waitForWardrobe(log);
        return build::install(env, text, opt, log);
    }));
}

void Engine::stopGame()
{
    if (!m_gamePid) return;
    killPid(m_gamePid);
    m_gamePid = 0;
    m_watch.stop();
    build::removeHook(m_es.esRoot());
    emit gameRunningChanged();
}

void Engine::engineCheck(const QString& id, const QString& text)
{
    if (m_busy) return;
    saveStory(id, text);
    const BuildEnv env = envFor(id);
    const CompileOptions opt = options();
    setBusy(true, U("Проверка движком БЛ (Ren'Py lint)… до пары минут"));
    auto* w = new QFutureWatcher<QPair<bool, QStringList>>(this);
    connect(w, &QFutureWatcher<QPair<bool, QStringList>>::finished, this, [this, w] {
        const auto res = w->result();
        w->deleteLater();
        setBusy(false);
        emit engineCheckFinished(res.first, res.second);
    });
    w->setFuture(QtConcurrent::run([this, env, text, opt] {
        waitForWardrobe({});
        const BuildReport r = build::install(env, text, opt, {});
        if (!r.ok) return qMakePair(false, QStringList{r.error});
        QString err;
        const QStringList hits = build::lint(env.esRoot, r.meta.modId, &err, 600000);
        if (!err.isEmpty()) return qMakePair(false, QStringList{err});
        return qMakePair(hits.isEmpty(), hits);
    }));
}

QString Engine::exportDir() const
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty()) docs = QDir::homePath();
    const QString dir = QDir::cleanPath(docs + QStringLiteral("/GenryBL"));
    QDir().mkpath(dir);
    return dir;
}

void Engine::exportMod(const QString& id, const QString& text, const QString& kind)
{
    if (m_busy) { emit toast(U("Уже собираю, секунду"), 1); return; }
    saveStory(id, text);
    const BuildEnv env = envFor(id);
    const CompileOptions opt = options();
    const bool workshop = kind == QLatin1String("workshop");
    const QString base = exportDir();
    setBusy(true, U("Экспорт: собираю мод…"));
    auto log = [this](const QString& s) { QMetaObject::invokeMethod(this, [this, s] { setBusy(true, s); }, Qt::QueuedConnection); };
    struct Result { bool ok = false; QString message, path, modId; };
    auto* w = new QFutureWatcher<Result>(this);
    connect(w, &QFutureWatcher<Result>::finished, this, [this, w, id, workshop] {
        const Result r = w->result();
        w->deleteLater();
        setBusy(false);
        if (r.ok && workshop) {
            // the Workshop card: the mod's first frame, square, under 1 MB
            QImage shot = m_renderer.render(coverScene(id));
            if (!shot.isNull()) {
                const int side = qMin(shot.width(), shot.height());
                shot = shot.copy((shot.width() - side) / 2, (shot.height() - side) / 2, side, side)
                           .scaled(640, 640, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                shot.save(r.path + QStringLiteral("/preview.jpg"), "JPG", 88);
            }
        }
        emit exportFinished(r.ok, r.message, r.path);
        if (r.ok) revealFile(workshop ? r.path + QStringLiteral("/mods") : r.path);
    });
    w->setFuture(QtConcurrent::run([this, env, text, opt, workshop, base, log]() -> Result {
        Result res;
        waitForWardrobe(log);
        const BuildReport r = build::install(env, text, opt, log);
        if (!r.ok) { res.message = r.error; return res; }
        res.modId = r.meta.modId;
        log(U("Экспорт: мод проверяет сама игра (Ren'Py lint) — до пары минут…"));
        QString err;
        const QStringList hits = build::lint(env.esRoot, r.meta.modId, &err, 600000);
        if (!err.isEmpty()) { res.message = err; return res; }
        if (!hits.isEmpty()) {
            res.message = U("Игра нашла в моде ошибки — экспорт остановлен, чтобы люди не получили сломанный мод:\n") +
                          QStringList(hits.mid(0, 8)).join(QLatin1Char('\n'));
            return res;
        }
        const QStringList missing = build::missingModFiles(r.modDir, r.meta.modId);
        if (!missing.isEmpty()) {
            res.message = U("Мод ссылается на файлы, которых нет в его папке — у другого человека он не заведётся:\n") +
                          QStringList(missing.mid(0, 8)).join(QLatin1Char('\n'));
            return res;
        }
        log(workshop ? U("Экспорт: готовлю папку для Мастерской…") : U("Экспорт: пакую архив и перечитываю его…"));
        if (workshop) {
            res.path = base + QString::fromUtf8("/Мастерская/") + r.meta.modId;
            if (!build::exportWorkshopFolder(r.modDir, r.meta.modId, r.meta.modName, res.path, &err)) { res.message = err; return res; }
            res.message = U("Папка для Мастерской готова: там mods, обложка preview.jpg и «КАК_ВЫЛОЖИТЬ.txt»");
        } else {
            res.path = base + QString::fromUtf8("/Экспорт/") + r.meta.modId + QStringLiteral("_pc.zip");
            if (!build::exportZip(r.modDir, r.meta.modId, r.meta.modName, res.path, &err)) { res.message = err; return res; }
            res.message = U("Архив готов — игра его проверила, все файлы внутри. Отдавай людям: внутри «КАК_УСТАНОВИТЬ.txt»");
        }
        res.ok = true;
        return res;
    }));
}

QString Engine::lastCrash() const
{
    const QString r = crash::newestReport(m_root + QStringLiteral("/work/crash"));
    return !r.isEmpty() && QFileInfo(r).fileName() != m_settings.value(QStringLiteral("crashSeen")).toString() ? r : QString();
}

void Engine::crashSeen()
{
    const QString r = crash::newestReport(m_root + QStringLiteral("/work/crash"));
    m_settings.setValue(QStringLiteral("crashSeen"), QFileInfo(r).fileName());
    emit lastCrashChanged();
}

QString Engine::crashText() const
{
    QFile f(lastCrash());
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.read(60000)) : QString();
}

void Engine::restartApp()
{
    stopGame();
    if (QProcess::startDetached(QCoreApplication::applicationFilePath(), {}, QCoreApplication::applicationDirPath()))
        QTimer::singleShot(300, qApp, &QCoreApplication::quit);
}

void Engine::revealFile(const QString& path) const
{
#ifdef Q_OS_WIN
    QProcess p;
    p.setProgram(QStringLiteral("explorer.exe"));
    p.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(path)));
    p.startDetached();
#else
    openFolder(QFileInfo(path).absolutePath());
#endif
}

void Engine::openFolder(const QString& path) const { QDesktopServices::openUrl(QUrl::fromLocalFile(path.isEmpty() ? m_root : path)); }

QString Engine::audioUrl(const QString& gamePath) const
{
    if (gamePath.startsWith(QLatin1String("file:"))) return gamePath;          // a file of GenryBL itself (the community sounds)
    if (gamePath.startsWith(QLatin1String("audio/")) && !m_current.isEmpty())
        return QUrl::fromLocalFile(assetsDir(m_current) + QLatin1Char('/') + gamePath).toString();
    const QString cache = m_root + QStringLiteral("/work/cache/") + gamePath;
    if (!QFileInfo::exists(cache)) {
        const QByteArray data = m_es.vfs().read(gamePath);
        if (data.isEmpty() || !writeFile(cache, data)) return {};
    }
    return QUrl::fromLocalFile(cache).toString();
}

QVariant Engine::setting(const QString& key, const QVariant& def) const { return m_settings.value(key, def); }
void Engine::setSetting(const QString& key, const QVariant& value) { m_settings.setValue(key, value); }

// ================================================================== images (provider thread)

QImage Engine::providerImage(const QString& rawId, const QSize& req)
{
    // "#<n>" at the end = a revision so QML reloads a picture after a wardrobe edit (names encode '#' as %23)
    const int rev = int(rawId.lastIndexOf(QLatin1Char('#')));
    const QString id = QUrl::fromPercentEncoding((rev > 0 ? rawId.left(rev) : rawId).toUtf8());
    const int slash = int(id.indexOf(QLatin1Char('/')));
    const QString kind = id.left(slash), rest = id.mid(slash + 1);
    auto fit = [&req](const QImage& img) {
        if (img.isNull() || !req.isValid() || (req.width() <= 0 && req.height() <= 0)) return img;
        return img.scaled(req.width() > 0 ? req.width() : img.width() * req.height() / qMax(1, img.height()),
                          req.height() > 0 ? req.height() : img.height() * req.width() / qMax(1, img.width()), Qt::KeepAspectRatio,
                          Qt::SmoothTransformation);
    };
    const int box = req.isValid() && req.width() > 0 ? qMax(req.width(), req.height()) : 200;
    if (kind == QLatin1String("leaf")) {            // the falling leaves of the first start / the installer (no game files)
        QImage img(64, 64, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath leaf;
        leaf.moveTo(32, 4);
        leaf.cubicTo(52, 14, 58, 36, 32, 60);
        leaf.cubicTo(6, 36, 12, 14, 32, 4);
        QLinearGradient g(0, 0, 64, 64);
        g.setColorAt(0, QColor(170, 214, 90));
        g.setColorAt(1, QColor(236, 150, 50));
        p.setBrush(g);
        p.setPen(QPen(QColor(90, 110, 40, 200), 1.6));
        p.drawPath(leaf);
        p.setPen(QPen(QColor(80, 100, 30, 180), 1.4));
        p.drawLine(QPointF(32, 8), QPointF(32, 58));
        for (int i = 0; i < 4; ++i) {
            p.drawLine(QPointF(32, 18 + i * 9), QPointF(22 - i, 12 + i * 9));
            p.drawLine(QPointF(32, 18 + i * 9), QPointF(42 + i, 12 + i * 9));
        }
        p.end();
        return img;
    }
    if (kind == QLatin1String("scene")) {
        SceneState st;
        {
            QMutexLocker lock(&m_sceneMx);
            st = m_scenes.value(rest.toInt());
        }
        return fit(m_renderer.render(st, true));
    }
    if (kind == QLatin1String("cine")) {             // «кино-режим»: the frame as the game shows it, no editor HUD
        SceneState st;
        {
            QMutexLocker lock(&m_sceneMx);
            st = m_scenes.value(rest.toInt());
        }
        return fit(m_renderer.render(st, false));
    }
    if (kind == QLatin1String("cover")) return fit(m_renderer.render(coverScene(rest.section(QLatin1Char('/'), 0, 0))));
    if (kind == QLatin1String("modslist")) {
        const QVariantMap t = QJsonDocument::fromJson(QByteArray::fromBase64(rest.toLatin1(), QByteArray::Base64UrlEncoding)).object().toVariantMap();
        const QString style = t.value(QStringLiteral("style")).toString().toLower();
        return fit(m_renderer.modsList(t.value(QStringLiteral("name")).toString(), fontFamilyFor(t.value(QStringLiteral("font")).toString()),
                                       t.value(QStringLiteral("color")).toString(), t.value(QStringLiteral("size")).toInt(),
                                       style.contains(QLatin1Char('b')), style.contains(QLatin1Char('i'))));
    }
    if (kind == QLatin1String("bg")) return fit(m_renderer.background(QStringLiteral("bg ") + rest));
    if (kind == QLatin1String("cg")) return fit(m_renderer.background(QStringLiteral("cg ") + rest));
    if (kind == QLatin1String("blur")) {            // blur/<cg id> | blur/sprite:<image>
        QImage img = rest.startsWith(QLatin1String("sprite:")) ? m_renderer.spriteThumb(rest.mid(7), 360)
                                                                : m_renderer.background(QStringLiteral("cg ") + rest);
        if (img.isNull()) return {};
        // colour blobs only: 24 px across, then smoothed back up twice, a little darker
        const QSize out(320, qMax(1, 320 * img.height() / qMax(1, img.width())));
        img = img.scaled(24, qMax(1, 24 * img.height() / qMax(1, img.width())), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                  .scaled(out / 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                  .scaled(out, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                  .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        {
            QPainter dim(&img);
            dim.fillRect(img.rect(), QColor(10, 6, 12, 90));
        }
        return fit(img);
    }
    if (kind == QLatin1String("file")) return fit(m_renderer.gameFile(rest));
    if (kind == QLatin1String("ws")) {                     // a Workshop library picture (thumbnails are cached)
        const bool thumb = req.isValid() && req.width() > 0 && req.width() <= 400;
        static QMutex cacheMx;
        static QHash<QString, QImage> cache;
        static QStringList order;
        if (thumb) {
            QMutexLocker lock(&cacheMx);
            auto it = cache.constFind(rest);
            if (it != cache.constEnd()) return *it;
        }
        const QImage img = fit(QImage::fromData(m_library.read(rest)));
        if (thumb && !img.isNull()) {
            QMutexLocker lock(&cacheMx);
            cache.insert(rest, img);
            order << rest;
            while (order.size() > 1500) cache.remove(order.takeFirst());
        }
        return img;
    }
    if (kind == QLatin1String("wfix") || kind == QLatin1String("wfixhead")) {     // «поправить лицо»: image|dx|dy|nonce
        const QStringList p = rest.split(QLatin1Char('|'));
        const QPoint shift(p.value(1).toInt(), p.value(2).toInt());
        const QString image = QUrl::fromPercentEncoding(p.value(0).toUtf8());
        QImage img = m_wardrobe.compose(image, &shift);
        if (img.isNull()) return {};
        if (kind == QLatin1String("wfixhead")) {
            const QRect b = m_wardrobe.faceBox(image);          // where the head is (steady while the face moves)
            if (!b.isNull()) {
                const int side = qMax(b.width(), b.height()) + 260;
                const QPoint c = b.center();
                img = img.copy(QRect(c.x() - side / 2, c.y() - side / 2, side, side));
            }
        }
        return fit(img);
    }
    if (kind == QLatin1String("sprite")) return fit(m_renderer.sprite(rest));
    if (kind == QLatin1String("thumb")) return m_renderer.spriteThumb(rest, box);
    if (kind == QLatin1String("face")) return m_renderer.faceThumb(rest, box);
    return {};
}
