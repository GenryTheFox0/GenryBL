#include "PhoneAssets.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QtMath>

#include <cmath>
#include <utility>

namespace gb {
namespace {

const QColor kMe(43, 108, 240);
const QColor kThem(35, 43, 54, 245);
const QColor kAccent(125, 211, 252);

QImage canvas(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

void aa(QPainter& p)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
}

QImage rounded(int w, int h, qreal r, const QColor& fill, const QColor& line = QColor(), qreal lw = 0)
{
    QImage img = canvas(w, h);
    QPainter p(&img);
    aa(p);
    p.setPen(line.isValid() ? QPen(line, lw) : Qt::NoPen);
    p.setBrush(fill);
    const qreal o = line.isValid() ? lw / 2 : 0;
    p.drawRoundedRect(QRectF(o, o, w - 2 * o, h - 2 * o), r, r);
    return img;
}

// a classic receiver: a bent handle with an ear cup and a mouth cup (upright at angle 0)
void handset(QPainter& p, const QRectF& r, const QColor& c, qreal angle)
{
    p.save();
    p.translate(r.center());
    p.rotate(angle);
    const qreal s = r.width();
    QPainterPath h;
    h.moveTo(-0.30 * s, -0.40 * s);                                          // ear cup
    h.cubicTo(-0.18 * s, -0.46 * s, -0.08 * s, -0.40 * s, -0.10 * s, -0.28 * s);
    h.lineTo(-0.14 * s, -0.16 * s);
    h.cubicTo(-0.16 * s, -0.10 * s, -0.12 * s, -0.04 * s, -0.10 * s, 0.00 * s);   // the handle bends in
    h.cubicTo(-0.06 * s, 0.06 * s, -0.02 * s, 0.10 * s, 0.04 * s, 0.13 * s);
    h.cubicTo(0.10 * s, 0.14 * s, 0.14 * s, 0.10 * s, 0.18 * s, 0.08 * s);
    h.lineTo(0.30 * s, 0.10 * s);                                            // mouth cup
    h.cubicTo(0.42 * s, 0.12 * s, 0.44 * s, 0.22 * s, 0.38 * s, 0.30 * s);
    h.cubicTo(0.30 * s, 0.40 * s, 0.10 * s, 0.40 * s, -0.06 * s, 0.28 * s);
    h.cubicTo(-0.24 * s, 0.14 * s, -0.40 * s, -0.08 * s, -0.42 * s, -0.24 * s);
    h.cubicTo(-0.43 * s, -0.34 * s, -0.38 * s, -0.38 * s, -0.30 * s, -0.40 * s);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(h);
    p.restore();
}

// a rounded screen piece: which corners are round (radius 60, like the screen hole)
QImage screenPiece(int w, int h, const QColor& fill, bool top, bool bottom)
{
    QImage img = canvas(w, h);
    QPainter p(&img);
    aa(p);
    QPainterPath path;
    const qreal R = 60;
    path.moveTo(0, top ? R : 0);
    if (top) path.arcTo(QRectF(0, 0, 2 * R, 2 * R), 180, -90); else path.lineTo(0, 0);
    path.lineTo(top ? w - R : w, 0);
    if (top) path.arcTo(QRectF(w - 2 * R, 0, 2 * R, 2 * R), 90, -90);
    path.lineTo(w, bottom ? h - R : h);
    if (bottom) path.arcTo(QRectF(w - 2 * R, h - 2 * R, 2 * R, 2 * R), 0, -90); else path.lineTo(w, h);
    path.lineTo(bottom ? R : 0, h);
    if (bottom) path.arcTo(QRectF(0, h - 2 * R, 2 * R, 2 * R), 270, -90); else path.lineTo(0, h);
    path.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPath(path);
    return img;
}

QPainterPath heart(const QRectF& r)
{
    const qreal w = r.width(), h = r.height();
    QPainterPath hp;
    hp.moveTo(r.left() + w * 0.5, r.top() + h * 0.92);
    hp.cubicTo(r.left() + w * 0.05, r.top() + h * 0.6, r.left(), r.top() + h * 0.2, r.left() + w * 0.27, r.top() + h * 0.1);
    hp.cubicTo(r.left() + w * 0.4, r.top() + h * 0.05, r.left() + w * 0.5, r.top() + h * 0.2, r.left() + w * 0.5, r.top() + h * 0.28);
    hp.cubicTo(r.left() + w * 0.5, r.top() + h * 0.2, r.left() + w * 0.6, r.top() + h * 0.05, r.left() + w * 0.73, r.top() + h * 0.1);
    hp.cubicTo(r.right(), r.top() + h * 0.2, r.right() - w * 0.05, r.top() + h * 0.6, r.left() + w * 0.5, r.top() + h * 0.92);
    return hp;
}

// an app icon: a rounded tile with a two-tone gradient and a white glyph
QImage appIcon(const QString& kind)
{
    struct C { const char* k; QColor a, b; };
    static const C cs[] = {{"msg", QColor(88, 214, 110), QColor(40, 170, 70)},   {"feed", QColor(255, 120, 160), QColor(230, 60, 110)},
                           {"gallery", QColor(255, 206, 80), QColor(245, 150, 40)}, {"calls", QColor(90, 180, 255), QColor(40, 120, 240)},
                           {"camera", QColor(150, 160, 175), QColor(95, 105, 120)}, {"music", QColor(255, 95, 110), QColor(220, 40, 70)},
                           {"notes", QColor(255, 225, 90), QColor(240, 190, 40)},   {"map", QColor(100, 220, 180), QColor(40, 170, 140)}};
    QColor a(120, 120, 120), b(80, 80, 80);
    for (const C& c : cs)
        if (kind == QLatin1String(c.k)) { a = c.a; b = c.b; }
    const int S = 76;
    QImage img = canvas(S, S);
    QPainter p(&img);
    aa(p);
    QLinearGradient g(0, 0, 0, S);
    g.setColorAt(0, a);
    g.setColorAt(1, b);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(0, 0, S, S), 18, 18);
    const QColor w(255, 255, 255);
    p.setBrush(w);
    if (kind == QLatin1String("msg")) {
        p.drawEllipse(QRectF(16, 18, 44, 34));
        QPainterPath tail;
        tail.moveTo(24, 44); tail.lineTo(18, 60); tail.lineTo(36, 49); tail.closeSubpath();
        p.drawPath(tail);
    } else if (kind == QLatin1String("feed")) {
        p.drawPath(heart(QRectF(16, 18, 44, 42)));
    } else if (kind == QLatin1String("gallery")) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(w, 4));
        p.drawRoundedRect(QRectF(16, 20, 44, 36), 6, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(w);
        QPainterPath m;
        m.moveTo(20, 52); m.lineTo(33, 36); m.lineTo(42, 46); m.lineTo(48, 40); m.lineTo(56, 52); m.closeSubpath();
        p.drawPath(m);
        p.drawEllipse(QRectF(44, 25, 7, 7));
    } else if (kind == QLatin1String("calls")) {
        handset(p, QRectF(16, 16, 44, 44), w, 0);
    } else if (kind == QLatin1String("camera")) {
        p.drawRoundedRect(QRectF(14, 26, 48, 32), 7, 7);
        p.drawRoundedRect(QRectF(28, 20, 20, 10), 3, 3);
        p.setBrush(b);
        p.drawEllipse(QRectF(28, 32, 20, 20));
        p.setBrush(w);
        p.drawEllipse(QRectF(33, 37, 10, 10));
    } else if (kind == QLatin1String("music")) {
        p.drawEllipse(QRectF(18, 44, 16, 12));
        p.drawEllipse(QRectF(40, 40, 16, 12));
        p.drawRect(QRectF(30, 20, 4, 30));
        p.drawRect(QRectF(52, 16, 4, 30));
        p.drawRect(QRectF(30, 16, 26, 7));
    } else if (kind == QLatin1String("notes")) {
        p.setPen(QPen(w, 4, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 4; ++i) p.drawLine(QPointF(20, 24 + i * 10), QPointF(i == 3 ? 42 : 56, 24 + i * 10));
    } else if (kind == QLatin1String("map")) {
        QPainterPath pin;
        pin.addEllipse(QRectF(24, 16, 28, 28));
        QPainterPath tip;
        tip.moveTo(26, 36); tip.lineTo(38, 60); tip.lineTo(50, 36); tip.closeSubpath();
        p.drawPath(pin.united(tip));
        p.setBrush(b);
        p.drawEllipse(QRectF(32, 24, 12, 12));
    }
    return img;
}

QImage body()
{
    QImage img = canvas(kPhoneW, kPhoneH);
    QPainter p(&img);
    aa(p);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(38, 44, 56));
    for (const auto& y : {std::pair<int, int>{210, 256}, {300, 380}, {396, 476}}) p.drawRoundedRect(QRectF(4, y.first, 10, y.second - y.first), 3, 3);
    p.drawRoundedRect(QRectF(506, 330, 10, 120), 3, 3);
    QLinearGradient metal(0, 0, kPhoneW, 0);
    metal.setColorAt(0, QColor(58, 64, 78));
    metal.setColorAt(0.5, QColor(30, 34, 42));
    metal.setColorAt(1, QColor(58, 64, 78));
    p.setBrush(metal);
    p.drawRoundedRect(QRectF(10, 5, 500, 1010), 74, 74);
    p.setPen(QPen(QColor(120, 132, 150), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(12, 7, 496, 1006), 72, 72);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(6, 7, 9));
    p.drawRoundedRect(QRectF(16, 11, 488, 998), 68, 68);
    // the screen: a transparent hole the phone's content shows through
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.drawRoundedRect(QRectF(kPhoneScrX, kPhoneScrY, kPhoneScrW, kPhoneScrH), 60, 60);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const qreal cx = kPhoneScrX + kPhoneScrW / 2.0;
    p.setBrush(Qt::black);
    p.drawRoundedRect(QRectF(cx - 60, kPhoneScrY + 10, 120, 34), 17, 17);      // the island
    p.setBrush(QColor(18, 24, 40));
    p.drawEllipse(QRectF(cx + 30, kPhoneScrY + 20, 14, 14));
    return img;
}

QImage wallpaper()
{
    QImage img = canvas(kPhoneScrW, kPhoneScrH);
    QPainter p(&img);
    aa(p);
    QPainterPath round;                          // the screen's own rounded corners (the body's hole)
    round.addRoundedRect(QRectF(0, 0, kPhoneScrW, kPhoneScrH), 60, 60);
    p.setClipPath(round);
    QLinearGradient g(0, 0, 0, kPhoneScrH);
    g.setColorAt(0, QColor(18, 22, 38));
    g.setColorAt(1, QColor(38, 30, 68));
    p.fillRect(img.rect(), g);
    struct B { qreal x, y, r; QColor c; };
    const B bs[] = {{80, 300, 170, QColor(90, 120, 255, 70)}, {380, 620, 210, QColor(200, 90, 255, 60)}, {200, 880, 160, QColor(60, 200, 220, 55)},
                    {420, 140, 120, QColor(255, 150, 120, 40)}};
    p.setPen(Qt::NoPen);
    for (const B& b : bs) {
        QRadialGradient rg(b.x, b.y, b.r);
        rg.setColorAt(0, b.c);
        rg.setColorAt(1, QColor(b.c.red(), b.c.green(), b.c.blue(), 0));
        p.setBrush(rg);
        p.drawEllipse(QPointF(b.x, b.y), b.r, b.r);
    }
    return img;
}

QImage statusRight()
{
    QImage img = canvas(120, 40);
    QPainter p(&img);
    aa(p);
    const QColor w(255, 255, 255);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 4; ++i) {
        const int hh = 6 + i * 4;
        p.setBrush(i < 3 ? w : QColor(255, 255, 255, 90));
        p.drawRoundedRect(QRectF(4 + i * 7, 30 - hh, 4, hh), 1, 1);
    }
    QFont f(QStringLiteral("Calibri"));
    f.setPixelSize(17);
    f.setBold(true);
    p.setFont(f);
    p.setPen(w);
    p.drawText(QRectF(36, 10, 40, 22), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("LTE"));
    p.setPen(QPen(QColor(255, 255, 255, 170), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(76, 14, 32, 16), 5, 5);
    p.setPen(Qt::NoPen);
    p.setBrush(w);
    p.drawRoundedRect(QRectF(79, 17, 20, 10), 2, 2);
    p.setBrush(QColor(255, 255, 255, 170));
    p.drawRoundedRect(QRectF(110, 19, 3, 6), 1, 1);
    return img;
}

QImage checks()
{
    QImage img = canvas(22, 14);
    QPainter p(&img);
    aa(p);
    p.setPen(QPen(QColor(160, 220, 255), 2, Qt::SolidLine, Qt::RoundCap));
    for (const qreal o : {0.0, 6.0}) {
        p.drawLine(QPointF(2 + o, 7), QPointF(6 + o, 11));
        p.drawLine(QPointF(6 + o, 11), QPointF(14 + o, 2));
    }
    return img;
}

QImage playButton(const QColor& tri)
{
    QImage img = canvas(46, 46);
    QPainter p(&img);
    aa(p);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(1, 1, 44, 44));
    p.setBrush(tri);
    QPainterPath t;
    t.moveTo(18, 13); t.lineTo(18, 33); t.lineTo(34, 23); t.closeSubpath();
    p.drawPath(t);
    return img;
}

QImage wave()
{
    QImage img = canvas(160, 30);
    QPainter p(&img);
    aa(p);
    p.setPen(Qt::NoPen);
    static const int hs[] = {6, 10, 14, 20, 26, 18, 12, 8, 14, 22, 26, 16, 10, 6, 12, 18, 24, 14, 8, 12, 18, 10, 6, 10, 14, 8};
    for (int j = 0; j < 26; ++j) {
        p.setBrush(QColor(255, 255, 255, j < 9 ? 235 : 130));
        p.drawRoundedRect(QRectF(j * 6, 15 - hs[j] / 2.0, 3, hs[j]), 1, 1);
    }
    return img;
}

QImage callButton(bool accept)
{
    QImage img = canvas(96, 96);
    QPainter p(&img);
    aa(p);
    p.setPen(Qt::NoPen);
    p.setBrush(accept ? QColor(48, 196, 108) : QColor(229, 72, 77));
    p.drawEllipse(QRectF(4, 4, 88, 88));
    handset(p, QRectF(26, 26, 44, 44), Qt::white, accept ? 0 : 135);
    return img;
}

QImage ring()
{
    QImage img = canvas(300, 300);
    QPainter p(&img);
    aa(p);
    p.setPen(QPen(QColor(125, 211, 252, 200), 3));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(3, 3, 294, 294));
    return img;
}

QImage chevron()
{
    QImage img = canvas(18, 30);
    QPainter p(&img);
    aa(p);
    p.setPen(QPen(kAccent, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF({QPointF(14, 3), QPointF(4, 15), QPointF(14, 27)}));
    return img;
}

QImage dot()
{
    QImage img = canvas(12, 12);
    QPainter p(&img);
    aa(p);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(1, 1, 10, 10));
    return img;
}

QImage badge()
{
    QImage img = canvas(28, 28);
    QPainter p(&img);
    aa(p);
    p.setPen(QPen(Qt::white, 2));
    p.setBrush(QColor(236, 64, 64));
    p.drawEllipse(QRectF(2, 2, 24, 24));
    return img;
}

QImage heartIcon(bool full)
{
    QImage img = canvas(30, 28);
    QPainter p(&img);
    aa(p);
    const QPainterPath h = heart(QRectF(2, 2, 26, 24));
    if (full) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 107, 139));
    } else {
        p.setPen(QPen(QColor(159, 179, 200), 2));
        p.setBrush(Qt::NoBrush);
    }
    p.drawPath(h);
    return img;
}

}   // namespace

QStringList phoneAssetNames()
{
    return {QStringLiteral("body"),     QStringLiteral("wall"),      QStringLiteral("bubble_me"), QStringLiteral("bubble_them"),
            QStringLiteral("pill"),     QStringLiteral("pill_line"), QStringLiteral("date_pill"), QStringLiteral("card"),
            QStringLiteral("banner"),   QStringLiteral("dock"),      QStringLiteral("status"),    QStringLiteral("checks"),
            QStringLiteral("play_me"),  QStringLiteral("play_them"), QStringLiteral("wave"),      QStringLiteral("call_yes"),
            QStringLiteral("call_no"),  QStringLiteral("ring"),      QStringLiteral("back"),      QStringLiteral("dot"),
            QStringLiteral("badge"),    QStringLiteral("heart"),     QStringLiteral("heart_full"), QStringLiteral("app_msg"),
            QStringLiteral("app_feed"), QStringLiteral("app_gallery"), QStringLiteral("app_calls"), QStringLiteral("app_camera"),
            QStringLiteral("app_music"), QStringLiteral("app_notes"), QStringLiteral("app_map"), QStringLiteral("circle"),
            QStringLiteral("mask"), QStringLiteral("head"), QStringLiteral("panel"), QStringLiteral("shade"), QStringLiteral("softmask")};
}

QImage phoneAsset(const QString& n)
{
    if (n == QLatin1String("body")) return body();
    if (n == QLatin1String("wall")) return wallpaper();
    // 9-slice sources (Ren'Py Frame with borders = the radius)
    if (n == QLatin1String("bubble_me")) return rounded(64, 64, 22, kMe);
    if (n == QLatin1String("bubble_them")) return rounded(64, 64, 22, kThem);
    if (n == QLatin1String("pill")) return rounded(104, 52, 26, kMe);
    if (n == QLatin1String("pill_line")) return rounded(104, 52, 26, QColor(43, 108, 240, 40), kMe, 2);
    if (n == QLatin1String("date_pill")) return rounded(56, 28, 14, QColor(255, 255, 255, 30));
    if (n == QLatin1String("card")) return rounded(64, 64, 22, QColor(28, 36, 48, 240));
    if (n == QLatin1String("banner")) return rounded(64, 64, 26, QColor(22, 27, 36, 235), QColor(255, 255, 255, 30), 1.5);
    if (n == QLatin1String("dock")) return rounded(96, 96, 34, QColor(255, 255, 255, 46));
    if (n == QLatin1String("status")) return statusRight();
    if (n == QLatin1String("checks")) return checks();
    if (n == QLatin1String("play_me")) return playButton(kMe);
    if (n == QLatin1String("play_them")) return playButton(QColor(35, 43, 54));
    if (n == QLatin1String("wave")) return wave();
    if (n == QLatin1String("call_yes")) return callButton(true);
    if (n == QLatin1String("call_no")) return callButton(false);
    if (n == QLatin1String("ring")) return ring();
    if (n == QLatin1String("back")) return chevron();
    if (n == QLatin1String("dot")) return dot();
    if (n == QLatin1String("badge")) return badge();
    if (n == QLatin1String("heart")) return heartIcon(false);
    if (n == QLatin1String("heart_full")) return heartIcon(true);
    if (n.startsWith(QLatin1String("app_"))) return appIcon(n.mid(4));
    if (n == QLatin1String("circle") || n == QLatin1String("mask")) {      // letter avatars / the round cut of a face
        QImage img = canvas(200, 200);
        QPainter p(&img);
        aa(p);
        p.setPen(Qt::NoPen);
        p.setBrush(n == QLatin1String("mask") ? QColor(Qt::white) : kAccent);
        p.drawEllipse(QRectF(1, 1, 198, 198));
        return img;
    }
    // pieces of the screen with its rounded corners, so nothing pokes out of the body
    if (n == QLatin1String("head")) return screenPiece(kPhoneScrW, 134, QColor(14, 18, 26, 235), true, false);
    if (n == QLatin1String("panel")) return screenPiece(128, 128, QColor(14, 18, 26, 245), false, true);     // Frame(…, 60, 60)
    if (n == QLatin1String("shade")) return screenPiece(128, 128, QColor(5, 8, 12, 176), true, true);         // Frame(…, 60, 60)
    if (n == QLatin1String("softmask")) {                 // a soft round fade (AlphaMask of the blurred caller)
        QImage img = canvas(472, 472);
        QPainter p(&img);
        aa(p);
        QRadialGradient g(236, 236, 236);
        g.setColorAt(0.0, QColor(255, 255, 255, 255));
        g.setColorAt(0.55, QColor(255, 255, 255, 200));
        g.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QRectF(0, 0, 472, 472));
        return img;
    }
    return {};
}

} // namespace gb
