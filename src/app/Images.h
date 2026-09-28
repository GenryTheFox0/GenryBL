// GenryBL V1 - "image://gb/..." : every picture in the UI goes through the one renderer.
//   scene/<key>          what the player sees (Engine::previewUrl)
//   cover/<project>/<t>  launcher card: the project's first real frame
//   thumb/<ch>/<code>    emotion thumbnail      sprite/<ch>/<code>  full sprite
//   bg/<name>            background             file/<game path>    any Everlasting Summer image (menu art, logo)
#pragma once
#include <QQuickImageProvider>

class Engine;

class GbImages : public QQuickImageProvider {
public:
    explicit GbImages(Engine* engine)
        : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading)
        , m_engine(engine)
    {
    }
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    Engine* m_engine;
};
