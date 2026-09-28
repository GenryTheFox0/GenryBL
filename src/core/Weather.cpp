#include "Weather.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QLinearGradient>

namespace gb {

QStringList weatherParticleNames()
{
    return {QStringLiteral("snow"), QStringLiteral("rain"), QStringLiteral("leaf"), QStringLiteral("leaf2"),
            QStringLiteral("heart"), QStringLiteral("spark"), QStringLiteral("dust")};
}

QImage weatherParticle(const QString& name)
{
    auto canvas = [](int w, int h) {
        QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        return img;
    };
    if (name == QLatin1String("snow")) {                 // soft round flake with a bright core
        QImage img = canvas(40, 40);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QRadialGradient g(20, 20, 20);
        // a faint blue-grey rim: without it a white flake vanishes on the bright day sky of ES
        g.setColorAt(0.0, QColor(255, 255, 255, 255));
        g.setColorAt(0.4, QColor(255, 255, 255, 240));
        g.setColorAt(0.62, QColor(205, 220, 240, 175));
        g.setColorAt(0.8, QColor(140, 160, 195, 70));
        g.setColorAt(1.0, QColor(140, 160, 195, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QRectF(0, 0, 40, 40));
        return img;
    }
    if (name == QLatin1String("rain")) {                 // a streak, fading in from the top
        QImage img = canvas(6, 90);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, 0, 90);
        g.setColorAt(0.0, QColor(200, 220, 255, 0));
        g.setColorAt(0.7, QColor(210, 228, 255, 150));
        g.setColorAt(1.0, QColor(235, 245, 255, 235));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(QRectF(1.5, 0, 3, 90), 1.5, 1.5);
        return img;
    }
    if (name == QLatin1String("leaf") || name == QLatin1String("leaf2")) {
        const bool autumn = name == QLatin1String("leaf2");
        QImage img = canvas(44, 28);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath leaf;
        leaf.moveTo(3, 14);
        leaf.cubicTo(12, 1, 30, 1, 41, 14);
        leaf.cubicTo(30, 27, 12, 27, 3, 14);
        QLinearGradient g(0, 0, 44, 28);
        g.setColorAt(0.0, autumn ? QColor(247, 176, 66) : QColor(150, 210, 80));
        g.setColorAt(1.0, autumn ? QColor(196, 84, 30) : QColor(70, 135, 38));
        p.setPen(QPen(autumn ? QColor(150, 60, 20, 200) : QColor(45, 95, 25, 200), 1.2));
        p.setBrush(g);
        p.drawPath(leaf);
        p.setPen(QPen(autumn ? QColor(120, 50, 15, 190) : QColor(40, 85, 20, 190), 1.4));
        p.drawLine(QPointF(4, 14), QPointF(39, 14));
        for (int i = 0; i < 3; ++i) {
            const double x = 12 + i * 8;
            p.drawLine(QPointF(x, 14), QPointF(x + 5, 8));
            p.drawLine(QPointF(x, 14), QPointF(x + 5, 20));
        }
        return img;
    }
    if (name == QLatin1String("heart")) {
        QImage img = canvas(40, 38);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath h;
        h.moveTo(20, 35);
        h.cubicTo(2, 22, 0, 8, 10, 4);
        h.cubicTo(15, 2, 19, 5, 20, 10);
        h.cubicTo(21, 5, 25, 2, 30, 4);
        h.cubicTo(40, 8, 38, 22, 20, 35);
        QRadialGradient g(15, 12, 26);
        g.setColorAt(0.0, QColor(255, 190, 215));
        g.setColorAt(0.5, QColor(255, 110, 165));
        g.setColorAt(1.0, QColor(215, 45, 110));
        p.setPen(QPen(QColor(255, 255, 255, 120), 1.2));
        p.setBrush(g);
        p.drawPath(h);
        return img;
    }
    if (name == QLatin1String("spark")) {                // warm glow + a four-point star
        QImage img = canvas(36, 36);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QRadialGradient g(18, 18, 18);
        g.setColorAt(0.0, QColor(255, 250, 210, 255));
        g.setColorAt(0.25, QColor(255, 225, 110, 200));
        g.setColorAt(1.0, QColor(255, 200, 60, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QRectF(0, 0, 36, 36));
        QPainterPath star;
        star.moveTo(18, 1);
        star.quadTo(19.5, 16.5, 35, 18);
        star.quadTo(19.5, 19.5, 18, 35);
        star.quadTo(16.5, 19.5, 1, 18);
        star.quadTo(16.5, 16.5, 18, 1);
        p.setBrush(QColor(255, 255, 235, 230));
        p.drawPath(star);
        return img;
    }
    // dust: a soft grey mote
    QImage img = canvas(20, 20);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QRadialGradient g(10, 10, 10);
    g.setColorAt(0.0, QColor(225, 225, 232, 200));
    g.setColorAt(1.0, QColor(200, 200, 210, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(QRectF(0, 0, 20, 20));
    return img;
}

QVector<WeatherLayer> weatherLayers(const QString& key)
{
    auto L = [](const char* png, double zoom, double alpha, int count, int xs0, int xs1, int ys0, int ys1, const char* anim = "", double rot = 0) {
        WeatherLayer l;
        l.png = QString::fromLatin1(png);
        l.zoom = zoom;
        l.alpha = alpha;
        l.count = count;
        l.xs0 = xs0; l.xs1 = xs1; l.ys0 = ys0; l.ys1 = ys1;
        l.anim = QString::fromLatin1(anim);
        l.rotate = rot;
        return l;
    };
    // far (small, faint, slow) -> near (big, bright, fast): the depth makes it read as weather
    if (key == QLatin1String("snow"))
        return {L("snow", 0.3, 0.65, 140, -10, 25, 25, 55), L("snow", 0.55, 0.9, 80, -25, 45, 55, 105), L("snow", 1.0, 1.0, 26, -45, 70, 110, 190)};
    if (key == QLatin1String("rain"))
        return {L("rain", 0.55, 0.45, 150, 220, 260, 1350, 1550, "", -9), L("rain", 0.85, 0.7, 70, 280, 330, 1800, 2100, "", -9)};
    if (key == QLatin1String("leaf"))
        return {L("leaf", 0.55, 0.85, 14, 20, 70, 45, 95, "spin"), L("leaf2", 0.7, 0.95, 10, 30, 90, 70, 130, "spinfast"),
                L("leaf", 1.0, 1.0, 6, 50, 120, 100, 170, "spinfast")};
    if (key == QLatin1String("heart"))
        return {L("heart", 0.45, 0.75, 22, -25, 25, 35, 75), L("heart", 0.8, 1.0, 10, -35, 35, 70, 120, "twinkle")};
    if (key == QLatin1String("spark"))
        return {L("spark", 0.35, 0.8, 45, -20, 20, -70, -25, "twinkle"), L("spark", 0.7, 1.0, 14, -30, 30, -110, -50, "twinkle")};
    if (key == QLatin1String("dust"))
        return {L("dust", 0.6, 0.55, 70, -12, 18, 6, 22), L("dust", 1.0, 0.7, 20, -18, 25, 10, 30)};
    return {};
}

QString weatherTint(const QString& key, int level)
{
    if (key == QLatin1String("rain")) return level >= 3 ? QStringLiteral("#0b16264d") : level == 1 ? QStringLiteral("#0b162618") : QStringLiteral("#0b162633");
    if (key == QLatin1String("snow") && level >= 3) return QStringLiteral("#dfe8f41a");
    return {};
}

int weatherLevelWord(const QString& word)
{
    static const QStringList weak{QStringLiteral("слабо"), QStringLiteral("слабый"), QStringLiteral("слабая"), QStringLiteral("лёгкий"),
                                  QStringLiteral("легкий"), QStringLiteral("лёгкая"), QStringLiteral("легкая"), QStringLiteral("мало"),
                                  QStringLiteral("light"), QStringLiteral("weak")};
    static const QStringList strong{QStringLiteral("сильно"), QStringLiteral("сильный"), QStringLiteral("сильная"), QStringLiteral("метель"),
                                    QStringLiteral("много"), QStringLiteral("буря"), QStringLiteral("strong"), QStringLiteral("heavy")};
    const QString w = word.toLower();
    if (weak.contains(w)) return 1;
    if (strong.contains(w)) return 3;
    if (w == QLatin1String("обычно") || w == QLatin1String("normal")) return 2;
    return 0;
}

double weatherLevelFactor(int level) { return level <= 1 ? 0.5 : level >= 3 ? 1.8 : 1.0; }

} // namespace gb
