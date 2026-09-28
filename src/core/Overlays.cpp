// GenryBL V1 - румянец / пот / слёзы / мокрая over ES sprites (see Overlays.h).
#include "Overlays.h"
#include "EsAssets.h"
#include "Wardrobe.h"

#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QSet>
#include <cmath>

namespace gb {

const QStringList& overlayKinds()
{
    static const QStringList k{QStringLiteral("blush"), QStringLiteral("sweat"), QStringLiteral("tears"), QStringLiteral("wet")};
    return k;
}

QString overlayKind(const QString& word)
{
    static const QHash<QString, QString> m = [] {
        QHash<QString, QString> h;
        auto add = [&h](const char* w, const char* k) { h.insert(QString::fromUtf8(w), QString::fromUtf8(k)); };
        for (const char* w : {"румянец", "румянцем", "румяная", "румяный", "blush"}) add(w, "blush");
        for (const char* w : {"пот", "потная", "потный", "вспотела", "вспотел", "sweat"}) add(w, "sweat");
        for (const char* w : {"слёзы", "слезы", "слезинки", "слёзки", "слезки", "tears"}) add(w, "tears");
        for (const char* w : {"мокрая", "мокрый", "мокрые", "мокро", "намокла", "wet"}) add(w, "wet");
        return h;
    }();
    return m.value(word.toLower());
}

QString overlayWord(const QString& kind)
{
    if (kind == QLatin1String("blush")) return QString::fromUtf8("румянец");
    if (kind == QLatin1String("sweat")) return QString::fromUtf8("пот");
    if (kind == QLatin1String("tears")) return QString::fromUtf8("слёзы");
    if (kind == QLatin1String("wet")) return QString::fromUtf8("мокрая");
    return kind;
}

QString withOverlays(const QString& imageWords)
{
    const QStringList w = imageWords.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList keep;
    QSet<QString> kinds;
    for (const QString& x : w) {
        const QString k = keep.isEmpty() ? QString() : overlayKind(x);   // the first word is the tag
        if (k.isEmpty()) keep << x;
        else kinds.insert(k);
    }
    if (kinds.isEmpty()) return imageWords;
    QStringList ordered;
    for (const QString& k : overlayKinds()) if (kinds.contains(k)) ordered << k;
    return keep.join(QLatin1Char(' ')) + QStringLiteral(" genry_ov_") + ordered.join(QLatin1Char('_'));
}

bool splitOverlays(const QString& image, QString* base, QStringList* kinds)
{
    const int at = int(image.indexOf(QLatin1String(" genry_ov_")));
    if (at < 0) return false;
    if (base) *base = image.left(at);
    if (kinds) *kinds = image.mid(at + 10).split(QLatin1Char('_'), Qt::SkipEmptyParts);
    return true;
}

QString overlayFile(const QString& image)
{
    QString base;
    QStringList kinds;
    if (!splitOverlays(image, &base, &kinds)) return {};
    return QString(base).replace(QLatin1Char(' '), QLatin1Char('_')) + QStringLiteral("__") + kinds.join(QLatin1Char('_')) + QStringLiteral(".png");
}

namespace {

QRect alphaBox(const QImage& img, int minAlpha = 16)
{
    int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(row[x]) < minAlpha) continue;
            x0 = qMin(x0, x);
            x1 = qMax(x1, x);
            y0 = qMin(y0, y);
            y1 = qMax(y1, y);
        }
    }
    return x1 < 0 ? QRect() : QRect(QPoint(x0, y0), QPoint(x1, y1));
}

double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// straight (not premultiplied) colour of a premultiplied pixel
void unpremul(QRgb p, double* r, double* g, double* b)
{
    const int a = qAlpha(p);
    if (!a) { *r = *g = *b = 0; return; }
    *r = qRed(p) * 255.0 / a;
    *g = qGreen(p) * 255.0 / a;
    *b = qBlue(p) * 255.0 / a;
}

struct Face {
    QRect box;             // eyes .. mouth
    double sr = 240, sg = 200, sb = 180;   // skin tone
    double u = 1;          // scale: 1 = the normal distance
};

// how much this pixel of the sprite is skin (0..1)
double skinAt(const QImage& body, const Face& f, int x, int y)
{
    if (x < 0 || y < 0 || x >= body.width() || y >= body.height()) return 0;
    const QRgb p = reinterpret_cast<const QRgb*>(body.constScanLine(y))[x];
    if (qAlpha(p) < 200) return 0;
    double r, g, b;
    unpremul(p, &r, &g, &b);
    const double d = std::sqrt((r - f.sr) * (r - f.sr) + (g - f.sg) * (g - f.sg) + (b - f.sb) * (b - f.sb));
    return clamp01(1.0 - (d - 22.0) / 48.0);
}

// multiply a layer's alpha by the skin mask (hatching and beads stay on the cheeks)
void maskBySkin(QImage& layer, const QImage& body, const Face& f)
{
    for (int y = 0; y < layer.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(layer.scanLine(y));
        for (int x = 0; x < layer.width(); ++x) {
            if (!qAlpha(row[x])) continue;
            const double k = skinAt(body, f, x, y);
            const QRgb p = row[x];
            row[x] = qRgba(int(qRed(p) * k), int(qGreen(p) * k), int(qBlue(p) * k), int(qAlpha(p) * k));
        }
    }
}

QImage blank(const QImage& like)
{
    QImage img(like.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

void drawBlush(QPainter& out, const QImage& body, const Face& f)
{
    QImage layer = blank(body);
    const QRect& b = f.box;
    const double fw = b.width(), fh = b.height();
    const double cy = b.top() + 0.66 * fh;
    const double rx = 0.21 * fw, ry = 0.11 * fw;
    for (const double cx : {b.left() + 0.2 * fw, b.left() + 0.8 * fw}) {
        const int x0 = int(cx - rx), x1 = int(cx + rx), y0 = int(cy - ry), y1 = int(cy + ry);
        for (int y = qMax(0, y0); y <= qMin(layer.height() - 1, y1); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(layer.scanLine(y));
            for (int x = qMax(0, x0); x <= qMin(layer.width() - 1, x1); ++x) {
                const double dx = (x - cx) / rx, dy = (y - cy) / ry, r2 = dx * dx + dy * dy;
                if (r2 >= 1) continue;
                const double a = 0.52 * std::pow(1.0 - r2, 1.6);
                const int A = int(255 * a);
                row[x] = qRgba(255 * A / 255, 70 * A / 255, 98 * A / 255, A);
            }
        }
    }
    // anime hatching: «///» on each cheek
    QPainter hp(&layer);
    hp.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(255, 118, 138, 210), qMax(1.2, 1.7 * f.u), Qt::SolidLine, Qt::RoundCap);
    hp.setPen(pen);
    for (const double cx : {b.left() + 0.2 * fw, b.left() + 0.8 * fw}) {
        for (int i = 0; i < 3; ++i) {
            const double x = cx - 0.07 * fw + i * 0.065 * fw;
            hp.drawLine(QPointF(x + 0.03 * fw, cy - 0.035 * fw), QPointF(x - 0.015 * fw, cy + 0.04 * fw));
        }
    }
    hp.end();
    maskBySkin(layer, body, f);
    out.drawImage(0, 0, layer);
}

QPainterPath dropPath(QPointF top, double w, double h)
{
    QPainterPath p;
    p.moveTo(top);
    p.cubicTo(top + QPointF(w * 0.18, h * 0.35), top + QPointF(w * 0.5, h * 0.55), top + QPointF(w * 0.5, h * 0.74));
    p.cubicTo(top + QPointF(w * 0.5, h * 1.02), top + QPointF(-w * 0.5, h * 1.02), top + QPointF(-w * 0.5, h * 0.74));
    p.cubicTo(top + QPointF(-w * 0.5, h * 0.55), top + QPointF(-w * 0.18, h * 0.35), top);
    return p;
}

void drawDrop(QPainter& p, QPointF top, double w, double h, double u, int alpha = 235)
{
    const QPainterPath d = dropPath(top, w, h);
    QLinearGradient g(top, top + QPointF(0, h));
    g.setColorAt(0, QColor(214, 240, 255, alpha));
    g.setColorAt(1, QColor(128, 196, 246, alpha));
    p.setBrush(g);
    p.setPen(QPen(QColor(58, 128, 198, alpha), qMax(1.0, 1.4 * u)));
    p.drawPath(d);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, alpha * 0.85));
    p.drawEllipse(QPointF(top.x() - w * 0.16, top.y() + h * 0.62), w * 0.12, h * 0.1);
}

void drawSweat(QPainter& out, const QImage& body, const Face& f)
{
    const QRect& b = f.box;
    const double fw = b.width();
    // the big anime drop hangs on the side of the head that has hair next to the face
    const int probeY = qBound(0, int(b.top() - 0.05 * fw), body.height() - 1);
    const int rightX = qBound(0, int(b.right() + 0.2 * fw), body.width() - 1);
    const bool right = qAlpha(reinterpret_cast<const QRgb*>(body.constScanLine(probeY))[rightX]) > 128;
    const double x = right ? b.right() + 0.12 * fw : b.left() - 0.12 * fw;
    out.setRenderHint(QPainter::Antialiasing);
    drawDrop(out, QPointF(x, b.top() - 0.12 * fw), 0.17 * fw, 0.28 * fw, f.u);
    // small beads on the forehead and the temple, only where there is skin
    QImage layer = blank(body);
    QPainter lp(&layer);
    lp.setRenderHint(QPainter::Antialiasing);
    const QPointF beads[] = {{b.left() + 0.34 * fw, b.top() - 0.02 * fw}, {b.left() + 0.62 * fw, b.top() + 0.01 * fw},
                             {right ? b.right() - 0.02 * fw : b.left() + 0.02 * fw, b.top() + 0.3 * fw}};
    for (const QPointF& c : beads) drawDrop(lp, c, 0.055 * fw, 0.085 * fw, f.u * 0.6, 220);
    lp.end();
    maskBySkin(layer, body, f);
    out.drawImage(0, 0, layer);
}

void drawTears(QPainter& out, const QImage& body, const Face& f)
{
    const QRect& b = f.box;
    const double fw = b.width(), fh = b.height();
    const double ye = b.top() + 0.46 * fh;
    QImage layer = blank(body);
    QPainter p(&layer);
    p.setRenderHint(QPainter::Antialiasing);
    for (const double xe : {b.left() + 0.29 * fw, b.left() + 0.73 * fw}) {
        QPainterPath s;
        s.moveTo(xe, ye);
        s.cubicTo(QPointF(xe - 0.02 * fw, ye + 0.18 * fh), QPointF(xe + 0.015 * fw, ye + 0.32 * fh), QPointF(xe - 0.01 * fw, b.bottom() + 0.04 * fh));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(150, 208, 255, 150), 0.055 * fw, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(s);
        p.setPen(QPen(QColor(255, 255, 255, 185), qMax(1.0, 0.016 * fw), Qt::SolidLine, Qt::RoundCap));
        p.drawPath(s.translated(-0.012 * fw, 0));
        // the eye brims over: a glossy line along the lower lid
        p.setPen(QPen(QColor(235, 248, 255, 170), qMax(1.2, 0.022 * fw), Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(xe - 0.11 * fw, ye - 0.08 * fh, 0.22 * fw, 0.1 * fh), 200 * 16, 140 * 16);
        drawDrop(p, QPointF(xe - 0.01 * fw, b.bottom() + 0.02 * fh), 0.06 * fw, 0.09 * fw, f.u * 0.7, 225);
    }
    p.end();
    // keep it on the sprite (the drop under the chin may hang in the air a little)
    for (int y = 0; y < layer.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(layer.scanLine(y));
        const QRgb* src = reinterpret_cast<const QRgb*>(body.constScanLine(y));
        for (int x = 0; x < layer.width(); ++x) {
            if (!qAlpha(row[x]) || qAlpha(src[x]) > 128 || y > b.bottom()) continue;
            row[x] = 0;
        }
    }
    out.drawImage(0, 0, layer);
}

void drawWet(QPainter& out, const QImage& body, const Face& f, quint32 seed)
{
    // damp: the clothes and hair darken and cool, the face much less
    QImage sheen = blank(body);
    for (int y = 0; y < body.height(); ++y) {
        const QRgb* src = reinterpret_cast<const QRgb*>(body.constScanLine(y));
        QRgb* row = reinterpret_cast<QRgb*>(sheen.scanLine(y));
        for (int x = 0; x < body.width(); ++x) {
            const int a = qAlpha(src[x]);
            if (a < 8) continue;
            const double k = (0.2 - 0.13 * skinAt(body, f, x, y)) * a / 255.0;
            const int A = int(255 * k);
            row[x] = qRgba(22 * A / 255, 46 * A / 255, 82 * A / 255, A);
        }
    }
    out.drawImage(0, 0, sheen);
    const QRect all = alphaBox(body, 200);
    if (all.isEmpty()) return;
    QRandomGenerator rng(seed);
    QImage drops = blank(body);
    QPainter p(&drops);
    p.setRenderHint(QPainter::Antialiasing);
    auto opaqueAt = [&](double x, double y) {
        const int ix = int(x), iy = int(y);
        if (ix < 0 || iy < 0 || ix >= body.width() || iy >= body.height()) return false;
        return qAlpha(reinterpret_cast<const QRgb*>(body.constScanLine(iy))[ix]) > 220;
    };
    // drips running down from the hair
    for (int i = 0; i < 9; ++i) {
        for (int t = 0; t < 30; ++t) {
            const double x = all.left() + rng.generateDouble() * all.width();
            const double y = all.top() + rng.generateDouble() * all.height() * 0.4;
            if (!opaqueAt(x, y)) continue;
            const double len = (22 + rng.generateDouble() * 46) * f.u;
            QLinearGradient g(QPointF(x, y), QPointF(x, y + len));
            g.setColorAt(0, QColor(220, 240, 255, 0));
            g.setColorAt(1, QColor(220, 240, 255, 150));
            p.setPen(QPen(QBrush(g), qMax(1.0, 1.5 * f.u), Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(x, y), QPointF(x + (rng.generateDouble() - 0.5) * 3 * f.u, y + len));
            drawDrop(p, QPointF(x, y + len - 2 * f.u), 5 * f.u, 8 * f.u, f.u * 0.5, 210);
            break;
        }
    }
    // beads all over
    for (int i = 0; i < 46; ++i) {
        for (int t = 0; t < 30; ++t) {
            const double x = all.left() + rng.generateDouble() * all.width();
            const double y = all.top() + rng.generateDouble() * all.height() * 0.8;
            if (!opaqueAt(x, y)) continue;
            const double r = (2.2 + rng.generateDouble() * 3.6) * f.u;
            drawDrop(p, QPointF(x, y - r), r * 1.25, r * 1.9, f.u * 0.45, 190 + int(rng.generateDouble() * 50));
            break;
        }
    }
    p.end();
    // nothing outside the sprite's outline
    for (int y = 0; y < drops.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(drops.scanLine(y));
        const QRgb* src = reinterpret_cast<const QRgb*>(body.constScanLine(y));
        for (int x = 0; x < drops.width(); ++x) {
            if (!qAlpha(row[x])) continue;
            const double k = qAlpha(src[x]) / 255.0;
            const QRgb q = row[x];
            row[x] = qRgba(int(qRed(q) * k), int(qGreen(q) * k), int(qBlue(q) * k), int(qAlpha(q) * k));
        }
    }
    out.drawImage(0, 0, drops);
}

} // namespace

QImage makeOverlay(const EsAssets& es, const QString& baseSprite, const QStringList& kinds)
{
    if (kinds.isEmpty()) return {};
    const auto it = es.sprites.constFind(baseSprite);
    QImage body;
    Face f;
    if (it != es.sprites.constEnd()) {
        const EsSprite& s = *it;
        body = QImage(s.w, s.h, QImage::Format_ARGB32_Premultiplied);
        body.fill(Qt::transparent);
        const QString emo = baseSprite.section(QLatin1Char(' '), 1, 1);
        QPainter p(&body);
        for (const EsLayer& l : s.layers) {
            const QImage img = QImage::fromData(es.vfs().read(l.path)).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (img.isNull()) continue;
            p.drawImage(l.x, l.y, img);
            if (l.path.endsWith(QLatin1Char('_') + emo + QStringLiteral(".png"))) f.box = alphaBox(img).translated(l.x, l.y);
        }
    } else if (const Wardrobe* w = wardrobe()) {
        // a sprite of the workshop wardrobe: its own layers and face
        body = w->compose(baseSprite);
        if (body.isNull()) return {};
        f.box = w->faceBox(baseSprite);
    } else {
        return {};
    }
    const QSize sz = body.size();
    // no emotion layer found (or a huge one): an ES head sits here
    if (f.box.isEmpty() || f.box.width() > sz.width() / 4)
        f.box = QRect(int(sz.width() * 0.42), int(sz.height() * 0.27), int(sz.width() * 0.155), int(sz.width() * 0.15));
    f.u = f.box.width() / 135.0;
    // skin tone: the light pixels of the lower middle of the face
    double r = 0, g = 0, b = 0;
    int n = 0;
    const QRect& fb = f.box;
    for (int y = int(fb.top() + 0.55 * fb.height()); y < int(fb.top() + 0.85 * fb.height()); ++y) {
        if (y < 0 || y >= body.height()) continue;
        const QRgb* row = reinterpret_cast<const QRgb*>(body.constScanLine(y));
        for (int x = int(fb.left() + 0.12 * fb.width()); x < int(fb.left() + 0.88 * fb.width()); ++x) {
            if (x < 0 || x >= body.width() || qAlpha(row[x]) < 250) continue;
            double pr, pg, pb;
            unpremul(row[x], &pr, &pg, &pb);
            if (pr < 150 || pr < pb) continue;          // lines, mouth, shadows, eyes
            r += pr;
            g += pg;
            b += pb;
            ++n;
        }
    }
    if (n > 20) {
        f.sr = r / n;
        f.sg = g / n;
        f.sb = b / n;
    }
    QImage out = blank(body);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    if (kinds.contains(QStringLiteral("wet"))) drawWet(p, body, f, qHash(baseSprite));
    if (kinds.contains(QStringLiteral("blush"))) drawBlush(p, body, f);
    if (kinds.contains(QStringLiteral("tears"))) drawTears(p, body, f);
    if (kinds.contains(QStringLiteral("sweat"))) drawSweat(p, body, f);
    p.end();
    return out;
}

} // namespace gb
