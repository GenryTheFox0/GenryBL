#include "Images.h"
#include "Engine.h"

QImage GbImages::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QImage img = m_engine->providerImage(id, requestedSize);
    if (size) *size = img.size();
    return img;
}
