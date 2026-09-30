// GenryBL V1 - Everlasting Summer mod constructor. C++ core + QML UI.
//   GenryBL.exe                         normal start (launcher)
//   GenryBL.exe --shot   <page> <png>   render one screen to a PNG and quit (UI self-check)
//     page: launcher | projects | editor
#include "CrashCatcher.h"
#include "Engine.h"
#include "Images.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTime>
#include <QIcon>
#include <QPixmap>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif
#include <QElapsedTimer>
#include <memory>

static QFile* g_log = nullptr;
static QElapsedTimer g_since;                   // since the process started: the start's own timeline in the log

// A WIN32 app has no console: QML warnings and errors go to work/genrybl.log.
static void logHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    if (!g_log) return;
    static const char* const kinds[] = {"debug", "warning", "critical", "fatal", "info"};
    QString where;
    if (ctx.file) where = QStringLiteral(" (%1:%2)").arg(QString::fromUtf8(ctx.file)).arg(ctx.line);
    g_log->write(QStringLiteral("%1 [%2] %3%4\n").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                                     QString::fromLatin1(kinds[qBound(0, int(type), 4)]), msg, where).toUtf8());
    g_log->flush();
}

// «Масштаб интерфейса» (Инструменты): Qt's own scale factor over whatever Windows gives - read from work/settings.ini
// before the application exists (Qt takes it only at start). A 1920x1080 laptop at 150 % is a 1280 px wide window:
// 80 % gives it the room of a 1600 px one. Nothing set = as Windows says (the default).
static void applyUiScale(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; ++i)          // GenryBL.exe --ui-scale 0.8: for this start only (a shortcut, the self-check)
        if (qstrcmp(argv[i], "--ui-scale") == 0) {
            bool ok = false;
            const double f = QByteArray(argv[i + 1]).toDouble(&ok);
            if (ok && f >= 0.5 && f <= 2.0) { qputenv("QT_SCALE_FACTOR", QByteArray(argv[i + 1])); return; }
        }
#ifdef Q_OS_WIN
    wchar_t buf[4096];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    QDir d = QFileInfo(QString::fromWCharArray(buf, int(n))).absoluteDir();
#else
    QDir d = QDir::current();
#endif
    for (int i = 0; i < 5; ++i) {               // the same walk up as Engine's root: the folder with data/
        if (QFileInfo::exists(d.filePath(QStringLiteral("data/es_catalog.json")))) {
            const QSettings s(d.filePath(QStringLiteral("work/settings.ini")), QSettings::IniFormat);
            bool ok = false;
            const double f = s.value(QStringLiteral("uiScale")).toString().toDouble(&ok);
            if (ok && f >= 0.5 && f <= 2.0) qputenv("QT_SCALE_FACTOR", QByteArray::number(f, 'g', 3));
            return;
        }
        if (!d.cdUp()) return;
    }
}

int main(int argc, char** argv)
{
    // how GenryBL draws and where its Qt plugins are is its own business: a Qt setting inherited from whoever started it
    // (a Qt tool, a game a mod launched it from) must not break the window ("no Qt platform plugin could be initialized")
    for (const char* v : {"QT_QPA_PLATFORM", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QT_SCALE_FACTOR"}) qunsetenv(v);
    g_since.start();
    applyUiScale(argc, argv);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("GenryBL"));
    QGuiApplication::setOrganizationName(QStringLiteral("GenryTheFox"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const QStringList args = app.arguments();
    const int shotAt = int(args.indexOf(QStringLiteral("--shot")));
    const bool shot = shotAt > 0 && args.size() > shotAt + 2;
    // GenryBL.exe --boot-test <png> [ms]: the loading screen's own check - the window stays cloaked (nothing on screen,
    // silent), the time to its first frame goes to <png>.txt, its picture to <png>, then quit. With [ms]: the whole
    // start runs (the launcher is built behind the loading screen) and the picture is taken [ms] after the first frame
    const int bootAt = int(args.indexOf(QStringLiteral("--boot-test")));
    const bool bootTest = !shot && bootAt > 0 && args.size() > bootAt + 1;
    const int bootGrabMs = bootTest && args.size() > bootAt + 2 ? args[bootAt + 2].toInt() : 0;

    Engine& engine = *Engine::boot();
    engine.setShotMode(shot || (bootTest && bootGrabMs <= 0));  // [ms]: the start for real - music, Discord (mute them in that root)
    if (!engine.appRoot().isEmpty()) {
        QDir().mkpath(engine.appRoot() + QStringLiteral("/work"));
        const QString logPath = engine.appRoot() + QStringLiteral("/work/genrybl.log");
        const QString prevPath = engine.appRoot() + QStringLiteral("/work/genrybl.prev.log");
        if (!shot && QFileInfo::exists(logPath)) {         // the last start's log survives this one (a start that went wrong)
            QFile::remove(prevPath);
            QFile::rename(logPath, prevPath);
        }
        g_log = new QFile(logPath);
        if (g_log->open(QIODevice::WriteOnly | QIODevice::Truncate)) qInstallMessageHandler(logHandler);
        qInfo("start: %s, engine up in %lld ms", qPrintable(engine.version()), g_since.elapsed());
        crash::install(engine.appRoot() + QStringLiteral("/work/crash"), engine.appRoot() + QStringLiteral("/work/genrybl.log"), engine.version());
    }
    if (engine.ready()) QGuiApplication::setWindowIcon(QIcon(QPixmap::fromImage(engine.providerImage(QStringLiteral("file/images/gui/title_menu/owl_idle.png"), QSize(256, 256)))));
    QQmlApplicationEngine qml;
    qml.addImageProvider(QStringLiteral("gb"), new GbImages(&engine));
    qml.rootContext()->setContextProperty(QStringLiteral("shotPage"), shot ? args[shotAt + 1] : QString());
    qml.rootContext()->setContextProperty(QStringLiteral("bootTest"), bootTest && bootGrabMs <= 0);
    // optional page input, e.g. "a.wav|b.ogg" for editor-dialogue-import
    qml.rootContext()->setContextProperty(QStringLiteral("shotArg"),
                                          shot && args.size() > shotAt + 3 && !args[shotAt + 3].startsWith(QLatin1String("--")) ? args[shotAt + 3] : QString());
    // --shot ... --size 1280x720: the window of a smaller screen (a 1920x1080 laptop at 150 % Windows scale)
    const int sizeAt = int(args.indexOf(QStringLiteral("--size")));
    qml.rootContext()->setContextProperty(QStringLiteral("shotSize"), shot && sizeAt > 0 && args.size() > sizeAt + 1 ? args[sizeAt + 1] : QString());
    QObject::connect(&qml, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(3); }, Qt::QueuedConnection);
    // a new language in «Инструменты»: every qsTr() in the window is re-read at once
    QObject::connect(&engine, &Engine::languageChanged, &qml, [&qml] { qml.retranslate(); });
    qml.loadFromModule("GenryBL", "Main");
    if (qml.rootObjects().isEmpty()) { qCritical("start: the window's QML did not load"); return 3; }
    qInfo("start: window loaded at %lld ms", g_since.elapsed());

    // The window comes on screen with its first finished frame (the loading screen): Windows' blank white window
    // before it - and the 1600x900 one left inside a maximized window - never reach the screen. Main.qml keeps the
    // window hidden and sizes it; here it is shown, cloaked until that frame is swapped.
    if (auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first()); win && !win->isVisible()) {
#ifdef Q_OS_WIN
        if (shot) {                                    // a self-check picture: cloaked for good - drawn and animated, never seen
            const HWND hwnd = reinterpret_cast<HWND>(win->winId());
            BOOL on = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &on, sizeof on);
        }
        if (!shot) {
            const HWND hwnd = reinterpret_cast<HWND>(win->winId());
            BOOL on = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &on, sizeof on);
            auto once = std::make_shared<QMetaObject::Connection>();
            if (bootTest) {
                const QString out = args[bootAt + 1];
                auto clock = std::make_shared<QElapsedTimer>();
                clock->start();
                *once = QObject::connect(win, &QQuickWindow::frameSwapped, &app, [once, clock, win, out, bootGrabMs] {
                    QObject::disconnect(*once);
                    const qint64 ms = clock->elapsed();
                    QTimer::singleShot(bootGrabMs > 0 ? bootGrabMs : 1200, win, [win, out, ms] {
                        QFile t(out + QStringLiteral(".txt"));
                        if (t.open(QIODevice::WriteOnly)) t.write(QByteArray::number(ms) + " ms to the first frame\n");
                        QCoreApplication::exit(win->grabWindow().save(out) ? 0 : 4);
                    });
                }, Qt::QueuedConnection);
                QTimer::singleShot(10000, &app, [] { QCoreApplication::exit(5); });   // no frame while cloaked
            } else {
                auto shown = std::make_shared<bool>(false);
                *once = QObject::connect(win, &QQuickWindow::frameSwapped, &app, [once, hwnd, shown] {
                    QObject::disconnect(*once);
                    if (*shown) return;
                    *shown = true;
                    BOOL off = FALSE;
                    DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &off, sizeof off);
                    qInfo("start: first frame on screen at %lld ms", g_since.elapsed());
                }, Qt::QueuedConnection);
                QTimer::singleShot(4000, &app, [hwnd, shown] {  // whatever happens, never an invisible window
                    if (*shown) return;
                    *shown = true;
                    BOOL off = FALSE;
                    DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &off, sizeof off);
                    qWarning("start: no frame in 4 s - the window is shown anyway");
                });
            }
        }
#endif
        if (win->property("bootMaximized").toBool()) win->showMaximized();
        else win->show();
        qInfo("start: window shown at %lld ms", g_since.elapsed());
    }

    // GenryBL.exe --crash-test: fall on purpose after 3 s (the crash catcher's own check)
    if (args.contains(QStringLiteral("--crash-test")))
        QTimer::singleShot(3000, &app, [] { volatile int* nowhere = nullptr; *nowhere = 1; });
    if (shot) {
        auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
        const QString out = args[shotAt + 2];
        // --late: pages that scan or load a lot first (the Workshop library)
        QTimer::singleShot(args.contains(QStringLiteral("--fast")) ? 1500 : args.contains(QStringLiteral("--late")) ? 16000 : 5200, &app, [win, out] {
            const QImage img = win->grabWindow();
            QCoreApplication::exit(img.save(out) ? 0 : 4);
        });
    }
    return app.exec();
}
