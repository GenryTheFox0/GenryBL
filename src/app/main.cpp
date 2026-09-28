// GenryBL V1 - Everlasting Summer mod constructor. C++ core + QML UI.
//   GenryBL.exe                         normal start (launcher)
//   GenryBL.exe --shot   <page> <png>   render one screen to a PNG and quit (UI self-check)
//     page: launcher | projects | editor
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
#include <QTimer>

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

int main(int argc, char** argv)
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("GenryBL"));
    QGuiApplication::setOrganizationName(QStringLiteral("GenryTheFox"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const QStringList args = app.arguments();
    const int shotAt = int(args.indexOf(QStringLiteral("--shot")));
    const bool shot = shotAt > 0 && args.size() > shotAt + 2;

    Engine& engine = *Engine::boot();
    if (!engine.appRoot().isEmpty()) {
        QDir().mkpath(engine.appRoot() + QStringLiteral("/work"));
        g_log = new QFile(engine.appRoot() + QStringLiteral("/work/genrybl.log"));
        if (g_log->open(QIODevice::WriteOnly | QIODevice::Truncate)) qInstallMessageHandler(logHandler);
    }
    if (engine.ready()) QGuiApplication::setWindowIcon(QIcon(QPixmap::fromImage(engine.providerImage(QStringLiteral("file/images/gui/title_menu/owl_idle.png"), QSize(256, 256)))));
    QQmlApplicationEngine qml;
    qml.addImageProvider(QStringLiteral("gb"), new GbImages(&engine));
    qml.rootContext()->setContextProperty(QStringLiteral("shotPage"), shot ? args[shotAt + 1] : QString());
    // optional page input, e.g. "a.wav|b.ogg" for editor-dialogue-import
    qml.rootContext()->setContextProperty(QStringLiteral("shotArg"),
                                          shot && args.size() > shotAt + 3 && !args[shotAt + 3].startsWith(QLatin1String("--")) ? args[shotAt + 3] : QString());
    QObject::connect(&qml, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(3); }, Qt::QueuedConnection);
    qml.loadFromModule("GenryBL", "Main");
    if (qml.rootObjects().isEmpty()) return 3;

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
