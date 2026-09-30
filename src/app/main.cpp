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
#endif

static QFile* g_log = nullptr;

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
    applyUiScale(argc, argv);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("GenryBL"));
    QGuiApplication::setOrganizationName(QStringLiteral("GenryTheFox"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const QStringList args = app.arguments();
    const int shotAt = int(args.indexOf(QStringLiteral("--shot")));
    const bool shot = shotAt > 0 && args.size() > shotAt + 2;

    Engine& engine = *Engine::boot();
    engine.setShotMode(shot);
    if (!engine.appRoot().isEmpty()) {
        QDir().mkpath(engine.appRoot() + QStringLiteral("/work"));
        g_log = new QFile(engine.appRoot() + QStringLiteral("/work/genrybl.log"));
        if (g_log->open(QIODevice::WriteOnly | QIODevice::Truncate)) qInstallMessageHandler(logHandler);
        crash::install(engine.appRoot() + QStringLiteral("/work/crash"), engine.appRoot() + QStringLiteral("/work/genrybl.log"), engine.version());
    }
    if (engine.ready()) QGuiApplication::setWindowIcon(QIcon(QPixmap::fromImage(engine.providerImage(QStringLiteral("file/images/gui/title_menu/owl_idle.png"), QSize(256, 256)))));
    QQmlApplicationEngine qml;
    qml.addImageProvider(QStringLiteral("gb"), new GbImages(&engine));
    qml.rootContext()->setContextProperty(QStringLiteral("shotPage"), shot ? args[shotAt + 1] : QString());
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
    if (qml.rootObjects().isEmpty()) return 3;

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
