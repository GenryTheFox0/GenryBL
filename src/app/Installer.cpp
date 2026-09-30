// GenryBL V1 - the installer and the uninstaller (GenryBL.exe --install / --uninstall).
// --install runs from the temp folder GenryBL_Setup.exe unpacked: it copies app/ data/ and the
// readme into the chosen folder, writes the game folder and «18+ said» into work/settings.ini,
// makes the shortcuts and the «Programs and Features» entry. The user's mods (projects/) and
// settings are never touched by an update; the uninstaller keeps projects/ unless asked.
#include "Engine.h"
#include "Tr.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

#include <climits>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

using namespace gb;

namespace {

QString U(const char* s) { return QString::fromUtf8(s); }
QString localPathOf(const QString& fileUrl)
{
    const QUrl u(fileUrl);
    return u.isLocalFile() ? u.toLocalFile() : fileUrl;
}
const char* const kUninstallKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GenryBL";

// what the installer carries: everything of the unpacked program but its temp work/ and projects/
QStringList payloadFiles(const QString& root, qint64* bytes)
{
    QStringList out;
    *bytes = 0;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString rel = QDir(root).relativeFilePath(f);
        if (rel.startsWith(QLatin1String("work/")) || rel.startsWith(QLatin1String("projects/"))) continue;
        out << rel;
        *bytes += QFileInfo(f).size();
    }
    out.sort();
    return out;
}

#ifdef Q_OS_WIN
bool makeShortcut(const QString& lnk, const QString& target, const QString& workDir, const QString& description)
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link)))) {
        link->SetPath(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(target).utf16()));
        link->SetWorkingDirectory(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(workDir).utf16()));
        link->SetDescription(reinterpret_cast<const wchar_t*>(description.utf16()));
        link->SetIconLocation(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(target).utf16()), 0);
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            ok = SUCCEEDED(file->Save(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(lnk).utf16()), TRUE));
            file->Release();
        }
        link->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}
#endif

// never install over a GenryBL source tree (a build that lives in its app/) or the game itself
bool isForeignTree(const QString& dir)
{
    const QString d = QDir::cleanPath(dir);
    if (QFileInfo::exists(d + QStringLiteral("/CMakeLists.txt")) || QFileInfo::exists(d + QStringLiteral("/src/app/Engine.cpp"))) return true;
    if (QFileInfo::exists(d + QStringLiteral("/Everlasting Summer.exe")) || QFileInfo::exists(d + QStringLiteral("/../Everlasting Summer.exe")) ||
        d.contains(QStringLiteral("/steamapps/common/"), Qt::CaseInsensitive))
        return true;
    return false;
}

QString desktopLink() { return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + QStringLiteral("/GenryBL.lnk"); }
QString startMenuLink() { return QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + QStringLiteral("/GenryBL.lnk"); }

}   // namespace

QString Engine::mode() const
{
    const QStringList a = QCoreApplication::arguments();
    if (a.contains(QStringLiteral("--install"))) return QStringLiteral("install");
    if (a.contains(QStringLiteral("--uninstall"))) return QStringLiteral("uninstall");
    return {};
}

bool Engine::esFolderOk(const QString& folder) const
{
    QString d = QDir::cleanPath(localPathOf(folder));
    if (d.endsWith(QLatin1String("/game"), Qt::CaseInsensitive)) d = QFileInfo(d).absolutePath();
    return build::isEsRoot(d);
}

QString Engine::loadEsArt(const QString& folder)
{
    QString d = QDir::cleanPath(localPathOf(folder));
    if (QFileInfo(d).isFile()) d = QFileInfo(d).absolutePath();
    if (d.endsWith(QLatin1String("/game"), Qt::CaseInsensitive)) d = QFileInfo(d).absolutePath();
    if (!build::isEsRoot(d)) return gbTr("Тут нет «Бесконечного лета»: нужна папка, где лежат Everlasting Summer.exe и папка game");
    QString err;
    if (!m_es.load(m_root + QStringLiteral("/data/es_catalog.json"), d, &err)) return err;
    m_renderer.setAssets(&m_es, m_root + QStringLiteral("/data"));
    emit esArtChanged();
    return {};
}

QString Engine::defaultInstallDir() const
{
    QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty()) base = QDir::homePath();
    return QDir::cleanPath(base + QStringLiteral("/Programs/GenryBL"));
}

QVariantMap Engine::installCheck(const QString& folder) const
{
    const QString dir = QDir::cleanPath(localPathOf(folder));
    qint64 bytes = 0;
    payloadFiles(m_root, &bytes);
    QVariantMap r{{QStringLiteral("dir"), QDir::toNativeSeparators(dir)}, {QStringLiteral("needMB"), int(bytes / (1024 * 1024)) + 1},
                  {QStringLiteral("existing"), QFileInfo::exists(dir + QStringLiteral("/app/GenryBL.exe"))}};
    // the nearest existing parent tells the free space
    QString probe = dir;
    while (!probe.isEmpty() && !QFileInfo::exists(probe)) {
        const QString up = QFileInfo(probe).absolutePath();
        if (up == probe) break;
        probe = up;
    }
    const QStorageInfo si(probe);
    const qint64 freeMB = si.isValid() ? si.bytesAvailable() / (1024 * 1024) : -1;
    r.insert(QStringLiteral("freeMB"), int(qMin<qint64>(freeMB, INT_MAX)));
    QString error;
    if (dir.isEmpty() || !QDir::isAbsolutePath(dir)) error = gbTr("Выбери папку");
    else if (isForeignTree(dir)) error = gbTr("Тут уже лежит другая копия GenryBL или сама игра — не трогаю, выбери другую папку");
    else if (QDir::cleanPath(dir).startsWith(QDir::cleanPath(m_root), Qt::CaseInsensitive)) error = gbTr("Это временная папка установщика — выбери другую");
    else if (freeMB >= 0 && freeMB < bytes / (1024 * 1024) + 50) error = gbTr("Не хватает места: нужно %1 МБ, свободно %2 МБ").arg(bytes / (1024 * 1024) + 50).arg(freeMB);
    r.insert(QStringLiteral("ok"), error.isEmpty());
    r.insert(QStringLiteral("error"), error);
    return r;
}

void Engine::install(const QString& folder, const QString& esRootIn, bool desktop, bool startMenu)
{
    const QString dir = QDir::cleanPath(localPathOf(folder));
    QString esRoot = QDir::cleanPath(localPathOf(esRootIn));
    if (!esRoot.isEmpty() && !build::isEsRoot(esRoot) && esRoot.endsWith(QLatin1String("/game"), Qt::CaseInsensitive)) esRoot = QFileInfo(esRoot).absolutePath();
    const QString src = m_root;
    auto* w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcher<QString>::finished, this, [this, w] {
        const QString err = w->result();
        w->deleteLater();
        emit installFinished(err.isEmpty(), err.isEmpty() ? gbTr("Готово") : err);
    });
    w->setFuture(QtConcurrent::run([this, src, dir, esRoot, desktop, startMenu]() -> QString {
        qint64 bytes = 0;
        const QStringList files = payloadFiles(src, &bytes);
        if (files.isEmpty()) return gbTr("Установщик пустой — скачай его заново");
        if (isForeignTree(dir)) return gbTr("Тут уже лежит другая копия GenryBL или сама игра — не трогаю");
        if (!QDir().mkpath(dir)) return gbTr("Не могу создать папку %1").arg(QDir::toNativeSeparators(dir));
        // an update: the running old version holds its exe
        const QString oldExe = dir + QStringLiteral("/app/GenryBL.exe");
        if (QFileInfo::exists(oldExe)) {
            // «Обновить» from inside GenryBL (--update): the old program is closing right now - give it a minute
            const int tries = QCoreApplication::arguments().contains(QStringLiteral("--update")) ? 120 : 1;
            bool free = false;
            for (int t = 0; t < tries && !free; ++t) {
                QFile probe(oldExe);
                free = probe.open(QIODevice::ReadWrite);
                if (!free && t + 1 < tries) QThread::msleep(500);
            }
            if (!free) return gbTr("GenryBL сейчас открыт — закрой его и нажми «Установить» ещё раз");
        }
        qint64 done = 0;
        int n = 0;
        for (const QString& rel : files) {
            const QString from = src + QLatin1Char('/') + rel, to = dir + QLatin1Char('/') + rel;
            QDir().mkpath(QFileInfo(to).absolutePath());
            QFile::remove(to);
            if (!QFile::copy(from, to)) return gbTr("Не удалось записать %1").arg(QDir::toNativeSeparators(to));
            done += QFileInfo(from).size();
            if (++n % 8 == 0 || n == files.size()) {
                const int pct = int(done * 1000 / qMax<qint64>(1, bytes));
                QMetaObject::invokeMethod(this, [this, pct, rel] { emit installProgress(pct, 1000, rel); }, Qt::QueuedConnection);
            }
        }
        QDir().mkpath(dir + QStringLiteral("/projects"));
        {
            QSettings s(dir + QStringLiteral("/work/settings.ini"), QSettings::IniFormat);
            if (build::isEsRoot(esRoot)) s.setValue(QStringLiteral("esRoot"), esRoot);
            s.setValue(QStringLiteral("age18"), QStringLiteral("true"));
        }
        const QString exe = dir + QStringLiteral("/app/GenryBL.exe");
#ifdef Q_OS_WIN
        if (desktop) makeShortcut(desktopLink(), exe, dir + QStringLiteral("/app"), U("GenryBL — конструктор модов «Бесконечного лета»"));
        if (startMenu) makeShortcut(startMenuLink(), exe, dir + QStringLiteral("/app"), U("GenryBL — конструктор модов «Бесконечного лета»"));
        QSettings reg(QString::fromLatin1(kUninstallKey), QSettings::NativeFormat);
        reg.setValue(QStringLiteral("DisplayName"), U("GenryBL — конструктор модов «Бесконечного лета»"));
        reg.setValue(QStringLiteral("DisplayVersion"), version());
        reg.setValue(QStringLiteral("Publisher"), QStringLiteral("GenryTheFox"));
        reg.setValue(QStringLiteral("InstallLocation"), QDir::toNativeSeparators(dir));
        reg.setValue(QStringLiteral("DisplayIcon"), QDir::toNativeSeparators(exe) + QStringLiteral(",0"));
        reg.setValue(QStringLiteral("UninstallString"), QLatin1Char('"') + QDir::toNativeSeparators(exe) + QStringLiteral("\" --uninstall"));
        reg.setValue(QStringLiteral("NoModify"), 1);
        reg.setValue(QStringLiteral("NoRepair"), 1);
        reg.setValue(QStringLiteral("EstimatedSize"), int(bytes / 1024));
#else
        Q_UNUSED(desktop);
        Q_UNUSED(startMenu);
#endif
        return {};
    }));
}

bool Engine::launchInstalled(const QString& folder)
{
    const QString dir = QDir::cleanPath(localPathOf(folder));
    return QProcess::startDetached(dir + QStringLiteral("/app/GenryBL.exe"), {}, dir + QStringLiteral("/app"));
}

void Engine::uninstall(bool removeProjects)
{
    // runs from the installed app/: everything but the running exe's folder now, that one after we quit
    const QString root = m_root;
    for (const QString& lnk : {desktopLink(), startMenuLink()}) {
        const QString target = QFileInfo(lnk).symLinkTarget();       // a .lnk of ANOTHER GenryBL stays
        if (QFileInfo::exists(lnk) && (target.isEmpty() || QDir::cleanPath(target).startsWith(QDir::cleanPath(root), Qt::CaseInsensitive)))
            QFile::remove(lnk);
    }
#ifdef Q_OS_WIN
    QSettings(QString::fromLatin1(kUninstallKey).section(QLatin1Char('\\'), 0, -2), QSettings::NativeFormat).remove(QStringLiteral("GenryBL"));
#endif
    QDir(root + QStringLiteral("/data")).removeRecursively();
    for (const QString& f : QDir(root).entryList(QDir::Files)) QFile::remove(root + QLatin1Char('/') + f);
    if (removeProjects) QDir(root + QStringLiteral("/projects")).removeRecursively();
    // app/ (this very exe) and work/ (its open log and settings): a hidden cmd waits until this process
    // is gone, removes them, then the root if nothing else is left. One raw command line: Qt's quoting
    // of arguments (\") means nothing to cmd.
    const QString app = QDir::toNativeSeparators(root + QStringLiteral("/app"));
    const QString work = QDir::toNativeSeparators(root + QStringLiteral("/work"));
    const QString rootN = QDir::toNativeSeparators(root);
    QProcess cmd;
    cmd.setProgram(QStringLiteral("cmd.exe"));
#ifdef Q_OS_WIN
    cmd.setNativeArguments(QStringLiteral("/c ping -n 4 127.0.0.1 >nul & rmdir /s /q \"%1\" & rmdir /s /q \"%2\" & rmdir \"%3\"").arg(app, work, rootN));
    cmd.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
#endif
    cmd.setWorkingDirectory(QDir::tempPath());          // not inside the folder being removed
    cmd.startDetached();
    emit installFinished(true, removeProjects ? gbTr("GenryBL удалён вместе с модами") : gbTr("GenryBL удалён. Твои моды остались в %1").arg(QDir::toNativeSeparators(root + QStringLiteral("/projects"))));
}
