#include "Renderer.h"
#include "EsAssets.h"
#include "Overlays.h"
#include "Py.h"
#include "PhoneAssets.h"
#include "Wardrobe.h"
#include "Weather.h"
#include "Compiler.h"

#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <cmath>

namespace gb {

namespace {

QString U(const char* s) { return QString::fromUtf8(s); }

// ---- Ren'Py colour matrices (matrixcolor), applied per pixel on straight alpha
struct Mat {
    double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    double add[3] = {0, 0, 0};
};

Mat mul(const Mat& a, const Mat& b)   // a after b
{
    Mat r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = 0;
            for (int k = 0; k < 3; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
        }
        r.add[i] = a.add[i];
        for (int k = 0; k < 3; ++k) r.add[i] += a.m[i][k] * b.add[k];
    }
    return r;
}

Mat saturation(double s)
{
    const double r = 0.2126, g = 0.7152, b = 0.0722;   // Ren'Py SaturationMatrix weights
    Mat m;
    const double w[3] = {r, g, b};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m.m[i][j] = w[j] * (1 - s) + (i == j ? s : 0);
    return m;
}

Mat brightness(double v)
{
    Mat m;
    m.add[0] = m.add[1] = m.add[2] = v;
    return m;
}

Mat tint(double r, double g, double b)
{
    Mat m;
    m.m[0][0] = r;
    m.m[1][1] = g;
    m.m[2][2] = b;
    return m;
}

Mat tintHex(const char* hex)
{
    const QColor c(QString::fromLatin1(hex));
    return tint(c.redF(), c.greenF(), c.blueF());
}

Mat contrast(double c)
{
    Mat m = tint(c, c, c);
    m.add[0] = m.add[1] = m.add[2] = (1 - c) / 2;
    return m;
}

Mat sepia()
{
    Mat m;
    const double s[3][3] = {{0.393, 0.769, 0.189}, {0.349, 0.686, 0.168}, {0.272, 0.534, 0.131}};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) m.m[i][j] = s[i][j];
    return m;
}

void apply(QImage& img, const Mat& m)
{
    img = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb* p = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const double c[3] = {qRed(p[x]) / 255.0, qGreen(p[x]) / 255.0, qBlue(p[x]) / 255.0};
            int o[3];
            for (int i = 0; i < 3; ++i)
                o[i] = qBound(0, int(std::lround((m.m[i][0] * c[0] + m.m[i][1] * c[1] + m.m[i][2] * c[2] + m.add[i]) * 255)), 255);
            p[x] = qRgba(o[0], o[1], o[2], qAlpha(p[x]));
        }
    }
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// genry_sprite_time_matrix() from the generated header
bool genryTimeMatrix(const QString& t, Mat* m)
{
    if (t == QLatin1String("night")) { *m = mul(mul(saturation(0.62), brightness(-0.10)), tintHex("#6a78c8")); return true; }
    if (t == QLatin1String("sunset")) { *m = mul(mul(saturation(0.86), brightness(0.02)), tintHex("#ffd08a")); return true; }
    if (t == QLatin1String("prolog") || t == QLatin1String("prologue")) { *m = mul(mul(saturation(0.72), brightness(-0.02)), tintHex("#b8c7ff")); return true; }
    return false;
}

// genry_filter_* transforms on the master layer
bool filterMatrix(const QString& k, Mat* m)
{
    if (k == QLatin1String("sepia")) { *m = sepia(); return true; }
    if (k == QLatin1String("gray")) { *m = saturation(0.0); return true; }
    if (k == QLatin1String("night")) { *m = mul(mul(saturation(0.55), brightness(-0.08)), tintHex("#6a78c8")); return true; }
    if (k == QLatin1String("warm")) { *m = tintHex("#ffcf8f"); return true; }
    if (k == QLatin1String("cold")) { *m = tintHex("#9fc6ff"); return true; }
    if (k == QLatin1String("dream")) { *m = mul(saturation(0.55), brightness(0.12)); return true; }
    if (k == QLatin1String("faded")) { *m = mul(saturation(0.7), contrast(0.9)); return true; }
    if (k == QLatin1String("horror")) { *m = mul(mul(saturation(0.3), contrast(1.2)), brightness(-0.12)); return true; }
    return false;
}

QImage cover(const QImage& src, int w, int h)
{
    if (src.isNull()) return {};
    const QImage s = src.scaled(w, h, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return s.copy((s.width() - w) / 2, (s.height() - h) / 2, w, h);
}

QStringList wrap(const QString& text, const QFont& f, qreal width)
{
    QStringList out;
    const QFontMetricsF fm(f);
    for (const QString& para : text.split(QLatin1Char('\n'))) {
        QString line;
        for (const QString& word : para.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const QString cand = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
            if (fm.horizontalAdvance(cand) <= width || line.isEmpty()) line = cand;
            else { out << line; line = word; }
        }
        out << line;
    }
    return out;
}

// strip Ren'Py text tags ({i}, {b}, {size=38}, ...) for drawing
QString plain(QString t)
{
    static const QRegularExpression tags(QStringLiteral("\\{[^{}]*\\}"));
    return t.remove(tags);
}

void shadowText(QPainter& p, const QPointF& at, const QString& t, const QColor& c, int dx = 2, int dy = 2)
{
    p.setPen(QColor(0, 0, 0));
    p.drawText(at + QPointF(dx, dy), t);
    p.setPen(c);
    p.drawText(at, t);
}

void outlineText(QPainter& p, const QPointF& at, const QString& t, const QFont& f, const QColor& fill, const QColor& line, qreal w)
{
    QPainterPath path;
    path.addText(at, f, t);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.strokePath(path, QPen(line, w * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.fillPath(path, fill);
    p.restore();
}

void frame9(QPainter& p, const QImage& img, const QRectF& r, int b)
{
    if (img.isNull()) { p.fillRect(r, QColor(20, 24, 36, 220)); return; }
    const int w = img.width(), h = img.height();
    const QRectF src[9] = {{0, 0, qreal(b), qreal(b)}, {qreal(b), 0, qreal(w - 2 * b), qreal(b)}, {qreal(w - b), 0, qreal(b), qreal(b)},
                           {0, qreal(b), qreal(b), qreal(h - 2 * b)}, {qreal(b), qreal(b), qreal(w - 2 * b), qreal(h - 2 * b)},
                           {qreal(w - b), qreal(b), qreal(b), qreal(h - 2 * b)}, {0, qreal(h - b), qreal(b), qreal(b)},
                           {qreal(b), qreal(h - b), qreal(w - 2 * b), qreal(b)}, {qreal(w - b), qreal(h - b), qreal(b), qreal(b)}};
    const qreal x0 = r.left(), x1 = r.left() + b, x2 = r.right() - b, y0 = r.top(), y1 = r.top() + b, y2 = r.bottom() - b;
    const QRectF dst[9] = {{x0, y0, qreal(b), qreal(b)}, {x1, y0, x2 - x1, qreal(b)}, {x2, y0, qreal(b), qreal(b)},
                           {x0, y1, qreal(b), y2 - y1}, {x1, y1, x2 - x1, y2 - y1}, {x2, y1, qreal(b), y2 - y1},
                           {x0, y2, qreal(b), qreal(b)}, {x1, y2, x2 - x1, qreal(b)}, {x2, y2, qreal(b), qreal(b)}};
    for (int i = 0; i < 9; ++i) p.drawImage(dst[i], img, src[i]);
}

} // namespace

void Renderer::setAssets(const EsAssets* es, const QString& dataDir)
{
    m_es = es;
    m_dataDir = dataDir;
    if (!es) return;
    // ES's own dialogue font (game/fonts/calibri.ttf) so the preview text matches the game
    for (const char* f : {"fonts/calibri.ttf", "fonts/calibrib.ttf", "fonts/calibrii.ttf", "fonts/corbel.ttf", "fonts/gothic.ttf"}) {
        const QByteArray data = es->vfs().read(QString::fromLatin1(f));
        if (data.isEmpty()) continue;
        const int id = QFontDatabase::addApplicationFontFromData(data);
        const QStringList fam = QFontDatabase::applicationFontFamilies(id);
        if (!fam.isEmpty() && QByteArray(f) == "fonts/calibri.ttf") m_family = fam.first();
        if (!fam.isEmpty() && QByteArray(f) == "fonts/corbel.ttf") m_header = fam.first();     // ES header_font
        if (!fam.isEmpty() && QByteArray(f) == "fonts/gothic.ttf") m_link = fam.first();       // ES link_font
    }
}

void Renderer::setCustomImages(const QHash<QString, QString>& nameToFile)
{
    QMutexLocker lock(&m_mx);
    // the same name may be a new picture now (re-imported «bg лес»): the project's files are read again
    for (const QString& f : std::as_const(m_custom)) m_files.remove(f);
    for (const QString& f : nameToFile) m_files.remove(f);
    m_custom = nameToFile;
    m_sprites.clear();
    m_bgs.clear();
    m_cacheBytes = 0;
    for (const QImage& i : std::as_const(m_files)) m_cacheBytes += i.sizeInBytes();
}

void Renderer::remember(QHash<QString, QImage>& cache, const QString& key, const QImage& img) const
{
    static constexpr qint64 kLimit = qint64(1200) * 1024 * 1024;
    if (m_cacheBytes + img.sizeInBytes() > kLimit) {
        m_files.clear();
        m_sprites.clear();
        m_bgs.clear();
        m_cacheBytes = 0;
    }
    const auto old = cache.constFind(key);
    if (old != cache.constEnd()) m_cacheBytes -= old->sizeInBytes();
    cache.insert(key, img);
    m_cacheBytes += img.sizeInBytes();
}

void Renderer::dropSpriteCache()
{
    QMutexLocker lock(&m_mx);
    m_sprites.clear();
}

QHash<QString, QString> Renderer::customImages() const
{
    QMutexLocker lock(&m_mx);
    return m_custom;
}

QImage Renderer::load(const QString& gamePath) const
{
    {
        QMutexLocker lock(&m_mx);
        auto it = m_files.constFind(gamePath);
        if (it != m_files.constEnd()) return *it;
    }
    QImage img;
    if (m_es) img = QImage::fromData(m_es->vfs().read(gamePath));
    if (img.isNull() && QFile::exists(gamePath)) img = QImage(gamePath);
    if (!img.isNull()) img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QMutexLocker lock(&m_mx);
    remember(m_files, gamePath, img);
    return img;
}

QImage Renderer::gameFile(const QString& path) const { return load(path); }

QImage Renderer::sprite(const QString& image, const QString& spriteTime) const
{
    const QString key = image + QLatin1Char('@') + spriteTime;
    {
        QMutexLocker lock(&m_mx);
        auto it = m_sprites.constFind(key);
        if (it != m_sprites.constEnd()) return *it;
    }
    QImage out;
    QString ovBase;
    QStringList ovKinds;
    if (m_es && splitOverlays(image, &ovBase, &ovKinds)) {
        // «показать dv smile pioneer румянец»: the sprite, then the same overlay picture the mod gets
        out = sprite(ovBase, spriteTime);
        if (!out.isNull()) {
            out = out.copy();
            QImage ov = makeOverlay(*m_es, ovBase, ovKinds);
            if (spriteTime == QLatin1String("sunset")) apply(ov, tint(0.94, 0.82, 1.0));
            else if (spriteTime == QLatin1String("night")) apply(ov, tint(0.63, 0.78, 0.82));
            QPainter p(&out);
            p.drawImage(0, 0, ov);
        }
    } else if (m_es && m_es->sprites.contains(image)) {
        const EsSprite& s = m_es->sprites[image];
        out = QImage(s.w, s.h, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        QPainter p(&out);
        for (const EsLayer& l : s.layers) p.drawImage(l.x, l.y, load(l.path));
        p.end();
        if (s.hasTint) apply(out, tint(s.tint[0], s.tint[1], s.tint[2]));
        else if (spriteTime == QLatin1String("sunset")) apply(out, tint(0.94, 0.82, 1.0));      // ES ConditionSwitch
        else if (spriteTime == QLatin1String("night")) apply(out, tint(0.63, 0.78, 0.82));
    } else {
        QString file;
        {
            QMutexLocker lock(&m_mx);
            file = m_custom.value(image);
        }
        if (!file.isEmpty()) {
            out = load(file);
        } else if (const Wardrobe* w = wardrobe()) {
            // «гардероб мастерской»: the workshop layers, darkened like ES's own sprites
            out = w->compose(image);
            if (!out.isNull()) {
                if (spriteTime == QLatin1String("sunset")) apply(out, tint(0.94, 0.82, 1.0));
                else if (spriteTime == QLatin1String("night")) apply(out, tint(0.63, 0.78, 0.82));
            }
        }
    }
    QMutexLocker lock(&m_mx);
    remember(m_sprites, key, out);
    return out;
}

QImage Renderer::spriteThumb(const QString& image, int box) const
{
    const QImage s = sprite(image);
    if (s.isNull()) return {};
    const QRect crop(s.width() / 8, 0, s.width() * 6 / 8, s.height());
    return s.copy(crop).scaled(box, box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage Renderer::faceThumb(const QString& image, int box) const
{
    const QImage s = sprite(image);
    if (s.isNull()) return {};
    const int w = s.width(), h = s.height();
    int top = -1;
    for (int y = 0; y < h && top < 0; y += 2) {
        const QRgb* row = reinterpret_cast<const QRgb*>(s.constScanLine(y));
        for (int x = 0; x < w; x += 2) if (qAlpha(row[x]) > 200) { top = y; break; }
    }
    if (top < 0) return spriteThumb(image, box);
    qint64 sum = 0, n = 0;
    for (int y = top; y < qMin(h, top + h / 8); y += 3) {
        const QRgb* row = reinterpret_cast<const QRgb*>(s.constScanLine(y));
        for (int x = 0; x < w; x += 3) if (qAlpha(row[x]) > 200) { sum += x; ++n; }
    }
    const int cx = n ? int(sum / n) : w / 2;
    const int side = qMax(64, w * 42 / 100);
    const QRect crop(qBound(0, cx - side / 2, qMax(0, w - side)), qMax(0, top - side / 16), side, side);
    return s.copy(crop).scaled(box, box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// a picture in the phone (a photo in the chat, a post): a Ren'Py image name or a file of the mod
QImage Renderer::phonePicture(const QString& name) const
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return {};
    if (n.contains(QLatin1Char('/')) || n.contains(QLatin1Char('.'))) {
        const QString base = QLatin1Char('/') + QFileInfo(n).fileName();
        QString file;
        {
            QMutexLocker lock(&m_mx);
            for (auto it = m_custom.constBegin(); it != m_custom.constEnd() && file.isEmpty(); ++it)
                if (it.value().endsWith(base, Qt::CaseInsensitive)) file = it.value();
        }
        return file.isEmpty() ? QImage() : load(file);
    }
    if (n.startsWith(QLatin1String("bg ")) || n.startsWith(QLatin1String("cg ")) || (m_es && m_es->images.contains(n))) return background(n);
    const QImage s = sprite(n);
    return s.isNull() ? background(n) : s;
}

QImage Renderer::background(const QString& name) const
{
    {
        QMutexLocker lock(&m_mx);
        auto it = m_bgs.constFind(name);
        if (it != m_bgs.constEnd()) return *it;
    }
    QImage out;
    const QString low = name.toLower();
    if (low == QLatin1String("black") || low == QLatin1String("white") || low == QLatin1String("bg black") || low == QLatin1String("bg white")) {
        out = QImage(W, H, QImage::Format_ARGB32_Premultiplied);
        out.fill(low.endsWith(QLatin1String("white")) ? Qt::white : Qt::black);
    } else if (m_es && m_es->images.contains(name)) {
        const EsImage& e = m_es->images[name];
        if (!e.color.isEmpty()) {
            out = QImage(W, H, QImage::Format_ARGB32_Premultiplied);
            out.fill(QColor(e.color));
        } else {
            out = cover(load(e.path), W, H);
            if (e.sepia) apply(out, sepia());
            if (e.hasTint) apply(out, tint(e.tint[0], e.tint[1], e.tint[2]));
        }
    } else {
        QString file;
        {
            QMutexLocker lock(&m_mx);
            file = m_custom.value(name);
            if (file.isEmpty() && name.startsWith(QLatin1String("bg "))) file = m_custom.value(name.mid(3));
        }
        if (!file.isEmpty()) out = cover(load(file), W, H);
    }
    QMutexLocker lock(&m_mx);
    remember(m_bgs, name, out);
    return out;
}

QImage Renderer::render(const SceneState& s, bool hud) const
{
    QImage canvas(W, H, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::TextAntialiasing);

    // ---- master layer: background + sprites (+ colour filter over both)
    const QImage bg = s.bg.isEmpty() ? QImage() : background(s.bg);
    if (!bg.isNull()) p.drawImage(0, 0, bg);
    else if (!s.bg.isEmpty()) {
        p.fillRect(canvas.rect(), QColor(18, 24, 38));
        p.setPen(QColor(120, 140, 170));
        QFont f(m_family);
        f.setPixelSize(34);
        p.setFont(f);
        p.drawText(canvas.rect(), Qt::AlignCenter, U("нет картинки: ") + s.bg);
    }
    for (const SpriteShow& sp : s.sprites) {
        QImage img = sprite(sp.image, s.spriteTime);
        if (img.isNull()) {
            // unknown sprite: a clear placeholder instead of a silent gap
            const QRectF r(sp.xpos * W - 200, 180, 400, 900);
            p.fillRect(r, QColor(255, 80, 120, 60));
            p.setPen(QColor(255, 120, 150));
            QFont f(m_family);
            f.setPixelSize(30);
            p.setFont(f);
            p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, U("нет спрайта\n") + sp.image);
            continue;
        }
        Mat m;
        if (sp.timeTint && genryTimeMatrix(s.spriteTime, &m)) apply(img, m);
        const double w = img.width() * sp.zoom, h = img.height() * sp.zoom;
        const double x = sp.xpos * W - sp.xanchor * w, y = sp.ypos * H - sp.yanchor * h;
        p.save();
        p.setOpacity(sp.alpha);
        if (sp.rotate != 0.0) {                                      // Ren'Py turns it round its middle
            p.translate(x + w / 2, y + h / 2);
            p.rotate(sp.rotate);
            p.translate(-(x + w / 2), -(y + h / 2));
        }
        if (sp.mirror) {
            p.translate(x + w, y);
            p.scale(-1, 1);
            p.drawImage(QRectF(0, 0, w, h), img);
        } else {
            p.drawImage(QRectF(x, y, w, h), img);
        }
        p.restore();
    }
    if (!s.filter.isEmpty()) {
        Mat m;
        if (filterMatrix(s.filter, &m)) {
            p.end();
            apply(canvas, m);
            p.begin(&canvas);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.setRenderHint(QPainter::TextAntialiasing);
        }
    }

    // ---- effects over the scene
    if (s.dim > 0) p.fillRect(canvas.rect(), QColor(0, 0, 0, int(qBound(0.0, s.dim, 1.0) * 255)));
    if (!s.weather.isEmpty()) {
        // the same layers and particle pictures the game gets (Weather.cpp): far, middle, near
        static QMutex partMx;
        static QHash<QString, QImage> parts;
        auto particle = [](const QString& n) {
            QMutexLocker lock(&partMx);
            auto it = parts.find(n);
            if (it == parts.end()) it = parts.insert(n, weatherParticle(n));
            return *it;
        };
        const QString tint = weatherTint(s.weather, s.weatherLevel);   // Ren'Py "#rrggbbaa"
        if (tint.size() == 9)
            p.fillRect(canvas.rect(), QColor(tint.mid(1, 2).toInt(nullptr, 16), tint.mid(3, 2).toInt(nullptr, 16), tint.mid(5, 2).toInt(nullptr, 16),
                                             tint.mid(7, 2).toInt(nullptr, 16)));
        QRandomGenerator rng(1234);
        for (const WeatherLayer& l : s.liveFx ? QVector<WeatherLayer>() : weatherLayers(s.weather)) {
            const QImage img = particle(l.png);
            const int n = std::max(1, int(std::lround(l.count * weatherLevelFactor(s.weatherLevel))));
            for (int i = 0; i < n; ++i) {
                const double x = rng.bounded(W + 80) - 40.0, y = rng.bounded(H + 80) - 40.0;
                p.save();
                p.setOpacity(l.alpha * (l.anim == QLatin1String("twinkle") ? 0.45 + rng.bounded(0.55) : 1.0));
                p.translate(x, y);
                if (l.rotate != 0) p.rotate(l.rotate);
                else if (l.anim.startsWith(QLatin1String("spin"))) p.rotate(rng.bounded(360.0));
                p.scale(l.zoom, l.zoom);
                p.drawImage(QPointF(-img.width() / 2.0, -img.height() / 2.0), img);
                p.restore();
            }
        }
    }
    if (s.dream && m_es) {
        const QImage fx = load(m_es->images.value(QStringLiteral("prologue_dream")).path);
        if (!fx.isNull()) {
            p.save();
            p.setOpacity(s.dreamAlpha);
            p.drawImage(canvas.rect(), fx);
            p.restore();
        }
    }
    if (s.noteDim > 0 || !s.nvlText.isEmpty()) p.fillRect(canvas.rect(), QColor(0, 0, 0, int((s.nvlText.isEmpty() ? s.noteDim : 0.6) * 255)));
    if (!s.liveFx && (s.eyesClosed || s.sleepy)) {
        const QImage up = load(QStringLiteral("images/anim/blink_up.png")), down = load(QStringLiteral("images/anim/blink_down.png"));
        const int off = s.eyesClosed ? 0 : H / 2;
        if (!up.isNull()) p.drawImage(QRectF(0, -off, W, H), up);
        if (!down.isNull()) p.drawImage(QRectF(0, off, W, H), down);
        if (up.isNull() && s.eyesClosed) p.fillRect(canvas.rect(), Qt::black);
    }
    if (s.flash) p.fillRect(canvas.rect(), QColor(255, 255, 255, 210));

    QFont base(m_family);
    base.setPixelSize(28);

    // ---- cards
    if (!s.cardKind.isEmpty()) {
        if (s.cardKind == QLatin1String("askname")) {
            // kV1Hero genry_ask_name: ES's own paper plate (yesno_prompt), header font, #64483c; yalign like Ren'Py
            const QImage plate = load(QStringLiteral("images/gui/o_rly/base.png"));
            if (!plate.isNull()) p.drawImage(canvas.rect(), plate);
            else p.fillRect(canvas.rect(), QColor(0, 0, 0, 0xa8));
            auto line = [&](const QString& text, int size, qreal yalign, const QColor& color) {
                QFont f(m_header);
                f.setPixelSize(size);
                const QFontMetricsF fm(f);
                p.setFont(f);
                p.setPen(color);
                const qreal h = fm.height();
                p.drawText(QRectF(0, yalign * (H - h), W, h), Qt::AlignCenter, text);
            };
            line(s.cardText, 34, 0.413, QColor(0x64, 0x48, 0x3c));
            line(s.cardSub + QStringLiteral("|"), 52, 0.487, QColor(0x3f, 0x2a, 0x1d));
            line(QString::fromUtf8("Готово"), 60, 0.66, QColor(0x64, 0x48, 0x3c));
        } else if (s.cardKind == QLatin1String("chapter")) {
            const QImage back = load(QStringLiteral("images/anim/backdrop/back.jpg"));
            if (!back.isNull()) p.drawImage(canvas.rect(), back);
            else p.fillRect(canvas.rect(), Qt::black);
            const QImage noise = load(QStringLiteral("images/anim/backdrop/1.png"));
            if (!noise.isNull()) p.drawImage(canvas.rect(), noise);
            QFont big(m_family);
            big.setPixelSize(96);
            p.setFont(big);
            p.setPen(QColor(216, 216, 216, 184));
            p.drawText(QRectF(0, H * 0.405 - 70, W * 0.91, 140), Qt::AlignCenter, s.cardText);
            QFont sub(m_family);
            sub.setPixelSize(28);
            p.setFont(sub);
            p.setPen(QColor(207, 212, 220, 209));
            p.drawText(QRectF(0, H * 0.515 - 30, W * 0.91, 60), Qt::AlignCenter, s.cardSub);
        } else if (s.cardKind == QLatin1String("splitflap")) {
            // the settled board of «табло» (kV1SplitFlap: dark split tiles, cream letters)
            const int size = 64, tw = int(size * 0.86), th = int(size * 1.45), gap = 5, space = int(size * 0.45);
            int total = 0;
            for (const QChar ch : s.cardText) total += (ch == QLatin1Char(' ') ? space : tw) + gap;
            qreal x = (W - total + gap) / 2.0;
            const qreal y = H * 0.45 - th / 2.0;
            QFont f(m_header);
            f.setPixelSize(size);
            f.setBold(true);
            p.setFont(f);
            for (const QChar ch : s.cardText) {
                if (ch == QLatin1Char(' ')) { x += space + gap; continue; }
                p.fillRect(QRectF(x, y, tw, th), QColor(0x15, 0x17, 0x1b));
                p.fillRect(QRectF(x, y, tw, th / 2), QColor(0x2a, 0x2d, 0x33));
                p.fillRect(QRectF(x, y + th / 2 - 1, tw, 2), Qt::black);
                p.setPen(QColor(0xf2, 0xe6, 0xc2));
                p.drawText(QRectF(x, y, tw, th), Qt::AlignCenter, QString(ch));
                x += tw + gap;
            }
        } else {
            if (s.cardKind != QLatin1String("title")) p.fillRect(canvas.rect(), Qt::black);
            else p.fillRect(canvas.rect(), QColor(0, 0, 0, 235));
            QFont f(m_family);
            f.setPixelSize(s.cardKind == QLatin1String("timeskip") ? 64 : s.cardKind == QLatin1String("title") ? 38 : 34);
            f.setBold(s.cardKind == QLatin1String("title"));
            p.setFont(f);
            p.setPen(s.cardKind == QLatin1String("timeskip") ? QColor(0xe8, 0xee, 0xf7) : QColor(0xf5, 0xf5, 0xf5));
            p.drawText(canvas.rect(), Qt::AlignCenter | Qt::TextWordWrap, plain(s.cardText));
        }
    }

    // ---- «фонарик»: the dark over the place and the heroines (master layer), a soft circle of light - in the middle here,
    // under the mouse in the game; the dialogue box stays over it
    if (s.flashlight) {
        const QPointF c(W / 2.0, H * 0.45);
        const qreal r1 = 330 * s.flashZoom;
        QColor dark(s.flashColor);
        if (!dark.isValid()) dark = QColor(0x05, 0x08, 0x10);
        QRadialGradient g(c, r1);
        QColor clear = dark;
        clear.setAlpha(0);
        QColor full = dark;
        full.setAlpha(245);
        g.setColorAt(0.0, clear);
        g.setColorAt(150.0 / 330.0, clear);
        g.setColorAt(1.0, full);
        g.setSpread(QGradient::PadSpread);
        p.fillRect(canvas.rect(), g);
    }

    // ---- NVL note (ES nvl screen: choice_box frame, padding 175/150)
    if (!s.nvlText.isEmpty() && m_es) {
        const QString tod = s.timeOfDay == QLatin1String("prologue") ? QStringLiteral("prologue") : s.timeOfDay;
        frame9(p, load(QStringLiteral("images/gui/choice/%1/choice_box.png").arg(tod)), QRectF(0, 0, W, H), 50);
        p.setFont(base);
        qreal y = 150 + 28;
        for (const QString& l : wrap(plain(s.nvlText), base, W - 350)) {
            shadowText(p, QPointF(175, y), l, QColor(0xff, 0xdd, 0x7d));
            y += QFontMetricsF(base).height() + 2;
        }
    }

    // ---- say window (ES screen say, small font mode)
    const bool saying = !s.text.isEmpty() && !s.windowHidden && s.cardKind.isEmpty() && s.nvlText.isEmpty() && !s.phoneOpen &&
                        (s.choices.isEmpty() || s.choiceAsked) && !s.modMenuOpen && s.codeLock.isEmpty() &&
                        !s.phoneHome && !s.feedOpen && s.phoneCall.isEmpty();
    if (saying && m_es) {
        const QString tod = s.timeOfDay == QLatin1String("prologue") ? QStringLiteral("prologue") : s.timeOfDay;
        const QImage box = load(QStringLiteral("images/gui/dialogue_box/%1/dialogue_box.png").arg(tod));
        if (!box.isNull()) p.drawImage(174, 916, box);
        else p.fillRect(QRectF(174, 916, 1570, 150), QColor(10, 10, 20, 200));
        for (const char* b : {"hide", "save", "menu", "load"}) {
            static const QHash<QString, int> xs{{QStringLiteral("hide"), 1508}, {QStringLiteral("save"), 1567}, {QStringLiteral("menu"), 1625},
                                                {QStringLiteral("load"), 1682}};
            const QImage bi = load(QStringLiteral("images/gui/dialogue_box/%1/%2_idle.png").arg(tod, QString::fromLatin1(b)));
            if (!bi.isNull()) p.drawImage(xs.value(QString::fromLatin1(b)), 933, bi);
        }
        const QImage back = load(QStringLiteral("images/gui/dialogue_box/%1/backward_idle.png").arg(tod));
        if (!back.isNull()) p.drawImage(38, 949, back);
        const QImage fwd = load(QStringLiteral("images/gui/dialogue_box/%1/forward_idle.png").arg(tod));
        if (!fwd.isNull()) p.drawImage(1768, 949, fwd);
        p.setFont(base);
        const QFontMetricsF fm(base);
        if (!s.speakerName.isEmpty()) shadowText(p, QPointF(194, 931 + fm.ascent()), s.speakerName, QColor(s.speakerColor));
        qreal y = 964 + fm.ascent();
        const QColor what = s.whatColor.isEmpty() ? QColor(0xff, 0xdd, 0x7d) : QColor(s.whatColor);
        for (const QString& l : s.hideSayText ? QStringList() : wrap(plain(s.text), base, 1541)) {
            shadowText(p, QPointF(194, y), l, what);
            y += fm.height() + 2;
        }
        if (s.textPages > 1 && !s.hideSayText) {
            QFont pf(m_family);
            pf.setPixelSize(20);
            p.setFont(pf);
            p.setPen(QColor(0xff, 0xdd, 0x7d, 0xb0));
            p.drawText(QRectF(1400, 1032, 330, 26), Qt::AlignRight | Qt::AlignVCenter, QString::fromUtf8("%1/%2 ▸").arg(s.textPage).arg(s.textPages));
        }
    }

    // ---- choice menu (the mod's own `screen choice`) / screen menu
    if (!s.choices.isEmpty() && s.choiceStyle != QLatin1String("timed") && s.choiceStyle != QLatin1String("phone")) {
        QFont f(m_family);
        f.setPixelSize(28);
        const QFontMetricsF fm(f);
        if (s.choiceStyle == QLatin1String("images")) {
            // genry_choice_img: tall strips, the picture cropped to its strip, idle 55% / hovered 100%
            p.fillRect(canvas.rect(), Qt::black);
            const int n = int(s.choices.size());
            const int sw = W / qMax(1, n);
            QFont cap(m_family);
            cap.setPixelSize(44);
            const QFontMetricsF cm(cap);
            for (int i = 0; i < n; ++i) {
                const QRect strip(i * sw, 0, sw, H);
                const QString img = s.choiceImages.value(i);
                const QString kind = s.choiceKinds.value(i);
                QImage pic;
                if (!img.isEmpty()) {
                    if (kind == QLatin1String("sprite")) pic = sprite(img, s.spriteTime);
                    if (pic.isNull()) pic = background(img);
                    if (pic.isNull() && kind != QLatin1String("sprite")) pic = sprite(img, s.spriteTime);
                }
                p.save();
                p.setClipRect(strip);
                p.setOpacity(i == s.choiceHover ? 1.0 : 0.55);
                if (pic.isNull()) {
                    p.fillRect(strip, QColor(0x18, 0x22, 0x1d));
                } else if (kind == QLatin1String("sprite") && pic.height() != H) {
                    const QImage sc = pic.scaledToHeight(H, Qt::SmoothTransformation);
                    p.fillRect(strip, QColor(0x11, 0x18, 0x14));
                    p.drawImage(QPointF(strip.center().x() + 1 - sc.width() / 2.0, 0), sc);
                } else {
                    if (kind == QLatin1String("sprite")) p.fillRect(strip, QColor(0x11, 0x18, 0x14));
                    p.drawImage(QPointF(strip.center().x() + 1 - pic.width() / 2.0, 0), pic);
                }
                const bool locked = !s.choiceHints.value(i).isEmpty();
                if (locked) p.fillRect(strip, QColor(0, 0, 0, 0xa8));
                p.fillRect(QRect(strip.left(), H - 250, sw, 250), QColor(0, 0, 0, 0xa0));
                if (locked) {
                    QFont hf(m_family);
                    hf.setPixelSize(24);
                    p.setFont(hf);
                    p.setPen(QColor(0xd8, 0xd8, 0xd8));
                    p.drawText(QRectF(strip.left() + 30, H * 0.95 - 32, sw - 60, 34), Qt::AlignHCenter | Qt::AlignTop, s.choiceHints[i]);
                }
                // caption: size 44 white, 3px dark outline, centred, wrapped to the strip
                QStringList rows;
                QString cur;
                for (const QString& word : s.choices[i].split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                    const QString tryLine = cur.isEmpty() ? word : cur + QLatin1Char(' ') + word;
                    if (!cur.isEmpty() && cm.horizontalAdvance(tryLine) > sw - 60) { rows << cur; cur = word; }
                    else cur = tryLine;
                }
                if (!cur.isEmpty()) rows << cur;
                qreal y = H * 0.95 - rows.size() * cm.height() - (locked ? 34 : 0);
                for (const QString& row : rows) {
                    QPainterPath path;
                    path.addText(QPointF(strip.center().x() - cm.horizontalAdvance(row) / 2, y + cm.ascent()), cap, row);
                    p.setRenderHint(QPainter::Antialiasing);
                    p.strokePath(path, QPen(QColor(0, 0, 0, 0xcc), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.fillPath(path, locked ? QColor(0x9a, 0x9a, 0x9a) : QColor(Qt::white));
                    y += cm.height();
                }
                p.restore();
                if (i > 0) p.fillRect(QRect(strip.left() - 1, 0, 2, H), QColor(255, 255, 255, 0x30));
            }
        } else if (s.screenMenu) {
            p.fillRect(canvas.rect(), QColor(0, 0, 0, 115));
            const qreal pw = 470, bh = fm.height() + 28;
            const qreal ph = 26 * 2 + 50 + 22 + s.choices.size() * (bh + 10);
            const QRectF panel(W * 0.78 - pw / 2, (H - ph) / 2, pw, ph);
            p.fillRect(panel, QColor(0x11, 0x18, 0x27, 224));
            QFont t(m_family);
            t.setPixelSize(38);
            p.setFont(t);
            p.setPen(QColor(0x7d, 0xd3, 0xfc));
            p.drawText(QRectF(panel.left() + 28, panel.top() + 20, pw - 56, 50), Qt::AlignLeft | Qt::AlignVCenter, s.menuTitle);
            qreal y = panel.top() + 26 + 50 + 22;
            p.setFont(f);
            for (const QString& c : s.choices) {
                const QRectF r(panel.left() + 28, y, pw - 56, bh);
                p.fillRect(r, QColor(0x18, 0x21, 0x31));
                p.setPen(QColor(0xee, 0xf6, 0xff));
                p.drawText(r.adjusted(18, 0, -18, 0), Qt::AlignLeft | Qt::AlignVCenter, c);
                y += bh + 10;
            }
        } else if (s.choiceStyle == QLatin1String("buttons")) {
            // genry_choice_btn: the old constructor's dark buttons (the question's box stays bright under them)
            p.fillRect(s.choiceAsked ? QRectF(0, 0, W, 905) : QRectF(canvas.rect()), QColor(0, 0, 0, 0x99));
            const qreal bw = 680, bh = fm.height() + 36, gap = 16;
            const qreal total = s.choices.size() * bh + (s.choices.size() - 1) * gap;
            qreal y = H * 0.45 - total / 2;
            for (int i = 0; i < s.choices.size(); ++i) {
                const bool locked = !s.choiceHints.value(i).isEmpty();
                const QRectF r((W - bw) / 2, y, bw, bh);
                p.fillRect(r, locked ? QColor(0x1a, 0x1d, 0x22) : i == s.choiceHover ? QColor(0x2a, 0x4f, 0x86) : QColor(0x16, 0x24, 0x3a));
                p.setPen(locked ? QColor(0x7d, 0x87, 0x94) : QColor(0xee, 0xf6, 0xff));
                p.setFont(f);
                p.drawText(locked ? r.adjusted(0, 0, 0, -18) : r, Qt::AlignCenter, s.choices[i]);
                if (locked) {
                    QFont hf2(m_family);
                    hf2.setPixelSize(20);
                    p.setFont(hf2);
                    p.drawText(r.adjusted(0, 30, 0, 0), Qt::AlignCenter, s.choiceHints[i]);
                }
                y += bh + gap;
            }
        } else {
            // ES's own `screen choice` (screens.rpy): a full-width choice_box frame (50px borders,
            // padding 75/50), corbel 37, colours by time of day; the hovered one in the hover colour
            static const QHash<QString, QPair<QColor, QColor>> colors{
                {QStringLiteral("day"), {QColor(0x46, 0x61, 0x23), QColor(0x9d, 0xcd, 0x55)}},
                {QStringLiteral("night"), {QColor(0x14, 0x56, 0x44), QColor(0x3c, 0xcf, 0xa2)}},
                {QStringLiteral("sunset"), {QColor(0x69, 0x65, 0x2f), QColor(0xdc, 0xd1, 0x68)}},
                {QStringLiteral("prologue"), {QColor(0x49, 0x64, 0x63), QColor(0x98, 0xd8, 0xda)}}};
            const QString tod = colors.contains(s.timeOfDay) ? s.timeOfDay : QStringLiteral("day");
            QFont hf(m_header);
            hf.setPixelSize(37);
            const QFontMetricsF hm(hf);
            const qreal rowH = hm.height() + 12;
            QFont hintF(m_header);
            hintF.setPixelSize(22);
            const qreal hintH = QFontMetricsF(hintF).height();
            qreal boxH = 50 + 50 + s.choices.size() * rowH;
            for (const QString& h : s.choiceHints) if (!h.isEmpty()) boxH += hintH;
            const QRectF box(0, (H - boxH) / 2, W, boxH);
            frame9(p, load(QStringLiteral("images/gui/choice/%1/choice_box.png").arg(tod)), box, 50);
            qreal y = box.top() + 50;
            for (int i = 0; i < s.choices.size(); ++i) {
                const bool locked = !s.choiceHints.value(i).isEmpty();
                p.setFont(hf);
                p.setPen(locked ? QColor(0x8c, 0x8c, 0x8c) : i == s.choiceHover ? colors[tod].second : colors[tod].first);
                p.drawText(QRectF(75, y, W - 150, rowH), Qt::AlignCenter, s.choices[i]);
                y += rowH;
                if (locked) {             // genry_choice_es: the lock's hint under the grey option
                    p.setFont(hintF);
                    p.setPen(QColor(0x9a, 0x9a, 0x9a));
                    p.drawText(QRectF(75, y - 8, W - 150, hintH), Qt::AlignCenter, s.choiceHints[i]);
                    y += hintH;
                }
            }
        }
    }

    // ---- Телефон 3.0 - the same pictures and layout as the game (V1Phone.inc + PhoneAssets.cpp):
    // body 520x1020 in the middle of the frame, its screen 472x982 inside
    {
        static QMutex phMx;
        static QHash<QString, QImage> phImgs;
        auto asset = [](const QString& n) {
            QMutexLocker lock(&phMx);
            auto it = phImgs.find(n);
            if (it == phImgs.end()) it = phImgs.insert(n, phoneAsset(n));
            return *it;
        };
        auto font = [this](int px, bool b) {
            QFont f(m_family);
            f.setPixelSize(px);
            f.setBold(b);
            return f;
        };
        auto text = [&p](const QRectF& r, const QString& t, const QFont& f, const QColor& c, int align = Qt::AlignLeft | Qt::AlignTop) {
            p.setFont(f);
            p.setPen(c);
            p.drawText(r, align, t);
        };
        const QColor accent(0x7d, 0xd3, 0xfc), dim(0x9f, 0xb3, 0xc8);
        const qreal ox = (W - kPhoneW) / 2.0, oy = (H - kPhoneH) / 2.0;
        const qreal sx = ox + kPhoneScrX, sy = oy + kPhoneScrY, SW = kPhoneScrW, SH = kPhoneScrH;
        auto avatar = [&](const QString& name, qreal size, const QPointF& at) {
            const QString id = speakers().value(name.toLower(), name.toLower());
            const QString f = esChibiFile(id);
            const QImage face = f.isEmpty() || id == QLatin1String("?") ? QImage() : QImage(m_dataDir + QStringLiteral("/mod_assets/chibi/") + f + QStringLiteral(".png"));
            const QRectF r(at, QSizeF(size, size));
            if (!face.isNull()) { p.drawImage(r, face); return; }
            p.drawImage(r, asset(QStringLiteral("circle")));
            text(r, name.left(1).toUpper(), font(int(size * 0.5), true), QColor(0x0b, 0x10, 0x18), Qt::AlignCenter);
        };
        auto statusBar = [&](const QString& t) {
            text(QRectF(sx + 36, sy + 9, 120, 28), t, font(22, true), Qt::white);
            p.drawImage(QPointF(sx + 338, sy), asset(QStringLiteral("status")));
        };
        auto header = [&](const QString& title, bool back) {
            p.drawImage(QPointF(sx, sy), asset(QStringLiteral("head")));
            if (back) p.drawImage(QPointF(sx + 18, sy + 78), asset(QStringLiteral("back")));
            p.fillRect(QRectF(sx, sy + 133, SW, 1), QColor(255, 255, 255, 22));
            if (!title.isEmpty()) text(QRectF(sx + 30, sy + 70, SW - 60, 40), title, font(30, true), accent);
        };
        auto blurred = [](const QImage& img) {
            return img.scaled(qMax(1, img.width() / 20), qMax(1, img.height() / 20), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .scaled(img.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        };
        auto fitted = [&](const QString& name, qreal w, qreal h, QSizeF* sz) {
            QImage img = phonePicture(name);
            *sz = img.isNull() ? QSizeF(w, h * 0.7) : QSizeF(img.size()).scaled(w, h, Qt::KeepAspectRatio);
            return img.isNull() ? img : img.scaled(sz->toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        };
        const QString clock = s.phone.isEmpty() ? QStringLiteral("14:25") : s.phone.last().time;
        const bool phoneChoice = !s.choices.isEmpty() && s.choiceStyle == QLatin1String("phone");

        // -- the chat
        if (s.phoneOpen || phoneChoice) {
            p.drawImage(QPointF(sx, sy), asset(QStringLiteral("wall")));
            const qreal rh = phoneChoice ? 52 + 62 * s.choices.size() : 0;
            const QRectF view(sx, sy + 136, SW, SH - 136 - rh);
            const QFont bf = font(23, false), tf = font(14, false), cf = font(21, false);
            const QFontMetricsF bm(bf), tm(tf), cm(cf);
            struct Item { QString kind; bool me; QString who, body, time, status; QStringList lines; QImage img; QSizeF isz; qreal w, h; };
            QVector<Item> items;
            qreal total = 8 + 28 + 10;
            for (const PhoneMessage& m : s.phone) {
                Item it;
                it.me = m.side.startsWith(QLatin1String("me"));
                it.who = m.name; it.body = m.text; it.time = m.time; it.status = m.status;
                it.kind = m.side == QLatin1String("call") ? QStringLiteral("call")
                          : m.side.contains(QLatin1Char('_')) ? m.side.section(QLatin1Char('_'), 1) : QStringLiteral("text");
                if (it.kind == QLatin1String("call")) {
                    it.w = SW; it.h = 22;
                } else if (it.kind == QLatin1String("text")) {
                    it.lines = wrap(m.text, bf, 330 - 36);
                    qreal lw = tm.horizontalAdvance(m.time) + (it.me ? 30 : 0);
                    for (const QString& l : it.lines) lw = qMax(lw, bm.horizontalAdvance(l));
                    it.w = qMin<qreal>(330, lw + 36);
                    it.h = 10 + it.lines.size() * bm.height() + 2 + 18 + 8;
                } else if (it.kind == QLatin1String("voice")) {
                    it.w = 10 + 46 + 10 + 160 + 10 + 48 + 14;
                    it.h = 62;
                } else {
                    it.img = fitted(m.text, 300, 250, &it.isz);
                    if (!it.img.isNull() && it.kind != QLatin1String("photo")) it.img = blurred(it.img);
                    it.lines = m.status.isEmpty() ? QStringList() : wrap(m.status, cf, 290);
                    it.w = it.isz.width() + 14;
                    it.h = 7 + it.isz.height() + 4 + it.lines.size() * cm.height() + 18 + 8;
                }
                total += it.h + 10;
                items << it;
            }
            if (!s.phoneTyping.isEmpty()) total += 46 + 10;
            const qreal shift = qMax<qreal>(0, total + 14 - view.height());
            p.save();
            p.setClipRect(view);
            qreal y = view.top() + 8 - shift;
            const QRectF pill(sx + SW / 2 - 48, y, 96, 28);
            frame9(p, asset(QStringLiteral("date_pill")), pill, 14);
            text(pill, U("Сегодня"), font(16, false), QColor(255, 255, 255, 170), Qt::AlignCenter);
            y += 28 + 10;
            for (const Item& it : items) {
                if (it.kind == QLatin1String("call")) {
                    text(QRectF(sx, y, SW, it.h), it.body, font(16, false), dim, Qt::AlignCenter);
                    y += it.h + 10;
                    continue;
                }
                const qreal bx = it.me ? sx + SW - 18 - it.w : sx + 12 + 36 + 8;
                if (!it.me) avatar(it.who, 36, QPointF(sx + 12, y + it.h - 36));
                const QRectF br(bx, y, it.w, it.h);
                frame9(p, asset(it.me ? QStringLiteral("bubble_me") : QStringLiteral("bubble_them")), br, 22);
                if (it.kind == QLatin1String("text")) {
                    qreal ty = y + 10;
                    for (const QString& l : it.lines) {
                        text(QRectF(bx + 18, ty, it.w - 36, bm.height()), l, bf, Qt::white);
                        ty += bm.height();
                    }
                } else if (it.kind == QLatin1String("voice")) {
                    p.drawImage(QPointF(bx + 10, y + 8), asset(it.me ? QStringLiteral("play_me") : QStringLiteral("play_them")));
                    p.drawImage(QPointF(bx + 66, y + 16), asset(QStringLiteral("wave")));
                    text(QRectF(bx + 236, y + 10, 60, 22), it.status, font(17, false), QColor(255, 255, 255, 220));
                } else {
                    const QRectF ir(bx + 7, y + 7, it.isz.width(), it.isz.height());
                    if (it.img.isNull()) p.fillRect(ir, QColor(16, 22, 32));
                    else p.drawImage(ir.topLeft(), it.img);
                    if (it.kind != QLatin1String("photo")) {
                        const QString label = it.kind == QLatin1String("photo18") ? U("18+  нажми, чтобы открыть") : U("1 раз  нажми, чтобы открыть");
                        const QFont lf = font(19, true);
                        const qreal lw = QFontMetricsF(lf).horizontalAdvance(label) + 32;
                        const QRectF lr(ir.center().x() - lw / 2, ir.center().y() - 17, lw, 34);
                        frame9(p, asset(QStringLiteral("date_pill")), lr, 14);
                        text(lr, label, lf, Qt::white, Qt::AlignCenter);
                    }
                    qreal ty = ir.bottom() + 4;
                    for (const QString& l : it.lines) {
                        text(QRectF(bx + 17, ty, 290, cm.height()), l, cf, Qt::white);
                        ty += cm.height();
                    }
                }
                // time (+ the read ticks of the hero's own messages)
                const qreal tw = tm.horizontalAdvance(it.time);
                text(QRectF(br.right() - 14 - tw - (it.me && it.kind == QLatin1String("text") ? 26 : 0), br.bottom() - 24, tw, 18), it.time, tf, QColor(255, 255, 255, 160));
                if (it.me && it.kind == QLatin1String("text")) p.drawImage(QPointF(br.right() - 36, br.bottom() - 21), asset(QStringLiteral("checks")));
                y += it.h + 10;
            }
            if (!s.phoneTyping.isEmpty()) {
                avatar(s.phoneTyping, 36, QPointF(sx + 12, y + 10));
                frame9(p, asset(QStringLiteral("bubble_them")), QRectF(sx + 56, y, 84, 46), 22);
                for (int j = 0; j < 3; ++j) {
                    p.setOpacity(j == 1 ? 1.0 : 0.45);
                    p.drawImage(QPointF(sx + 76 + j * 17, y + 17), asset(QStringLiteral("dot")));
                }
                p.setOpacity(1.0);
            }
            p.restore();
            header(QString(), true);
            statusBar(clock);
            avatar(s.phoneContact.isEmpty() ? U("СМС") : s.phoneContact, 52, QPointF(sx + 46, sy + 66));
            text(QRectF(sx + 110, sy + 62, 300, 32), s.phoneContact, font(25, true), Qt::white);
            text(QRectF(sx + 110, sy + 94, 300, 24), s.phoneTyping.isEmpty() ? U("в сети") : U("печатает…"), font(17, false), accent);
            if (phoneChoice) {       // «выбор телефон»: reply bubbles at the bottom of the phone
                const QRectF panel(sx, sy + SH - rh, SW, rh);
                frame9(p, asset(QStringLiteral("panel")), panel, 60);
                text(QRectF(panel.left() + 24, panel.top() + 10, 200, 22), U("Ответить"), font(17, true), accent);
                qreal yy = panel.top() + 40;
                for (int i = 0; i < s.choices.size(); ++i) {
                    const QRectF pr(panel.left() + 24, yy, 424, 52);
                    frame9(p, asset(i == s.choiceHover ? QStringLiteral("pill") : QStringLiteral("pill_line")), pr, 26);
                    text(pr, s.choices[i], font(22, false), s.choices[i].startsWith(QLatin1Char('(')) ? QColor(255, 255, 255, 150) : QColor(Qt::white), Qt::AlignCenter);
                    yy += 62;
                }
            }
            p.drawImage(QPointF(ox, oy), asset(QStringLiteral("body")));
        }

        // -- the camp feed
        if (s.feedOpen) {
            p.drawImage(QPointF(sx, sy), asset(QStringLiteral("wall")));
            p.save();
            p.setClipRect(QRectF(sx, sy + 136, SW, SH - 136));
            qreal y = sy + 142;
            const QFont pf = font(21, false), nf = font(22, true), sf = font(14, false), cfn = font(18, false);
            if (s.feed.isEmpty()) text(QRectF(sx, y + 20, SW, 30), U("Пока ни одного поста"), font(22, false), dim, Qt::AlignCenter);
            for (const QStringList& post : s.feed) {
                const QStringList lines = post.value(1).isEmpty() ? QStringList() : wrap(post.value(1), pf, 416);
                QSizeF isz;
                const QImage img = post.value(2).isEmpty() ? QImage() : fitted(post.value(2), 416, 300, &isz);
                QStringList comments;
                for (int i = 4; i < post.size(); ++i) if (post[i].startsWith(QLatin1String("c:"))) comments << post[i].mid(2);
                const qreal h = 14 + 44 + 10 + lines.size() * QFontMetricsF(pf).height() + (img.isNull() ? 0 : isz.height() + 10) + 30 + comments.size() * 30 + 14;
                const QRectF card(sx + 12, y, 448, h);
                frame9(p, asset(QStringLiteral("card")), card, 22);
                avatar(post.value(0), 44, QPointF(card.left() + 16, card.top() + 14));
                text(QRectF(card.left() + 72, card.top() + 14, 300, 26), post.value(0), nf, Qt::white);
                text(QRectF(card.left() + 72, card.top() + 40, 300, 18), U("только что"), sf, dim);
                qreal ty = card.top() + 14 + 44 + 10;
                for (const QString& l : lines) {
                    text(QRectF(card.left() + 16, ty, 416, QFontMetricsF(pf).height()), l, pf, QColor(0xdd, 0xe8, 0xf5));
                    ty += QFontMetricsF(pf).height();
                }
                if (!img.isNull()) {
                    p.drawImage(QPointF(card.left() + 16, ty + 4), img);
                    ty += isz.height() + 10;
                }
                p.drawImage(QPointF(card.left() + 16, ty + 2), asset(QStringLiteral("heart")));
                text(QRectF(card.left() + 54, ty + 2, 60, 26), post.value(3), font(20, false), dim);
                if (!comments.isEmpty()) text(QRectF(card.left() + 110, ty + 5, 250, 22), U("комментарии: ") + QString::number(comments.size()), font(17, false), dim);
                ty += 30;
                for (const QString& cm : comments) {
                    avatar(cm.section(QLatin1Char('|'), 0, 0), 26, QPointF(card.left() + 16, ty));
                    text(QRectF(card.left() + 50, ty + 2, 380, 24), cm.section(QLatin1Char('|'), 0, 0) + QStringLiteral("  ") + cm.section(QLatin1Char('|'), 1), cfn, QColor(0xc9, 0xd6, 0xe6));
                    ty += 30;
                }
                y += h + 14;
            }
            p.restore();
            header(s.feedTitle, false);
            statusBar(QStringLiteral("14:30"));
            text(QRectF(sx + 360, sy + 80, 100, 26), U("Закрыть"), font(20, false), dim);
            p.drawImage(QPointF(ox, oy), asset(QStringLiteral("body")));
        }

        // -- the home screen: clock, apps with unread badges, the dock
        if (s.phoneHome) {
            p.drawImage(QPointF(sx, sy), asset(QStringLiteral("wall")));
            statusBar(clock);
            text(QRectF(sx, sy + 80, SW, 110), clock, font(92, true), Qt::white, Qt::AlignHCenter | Qt::AlignTop);
            text(QRectF(sx, sy + 200, SW, 30), U("Лагерь «Совёнок»"), font(22, false), QColor(255, 255, 255, 205), Qt::AlignHCenter | Qt::AlignTop);
            int unread = 0;
            for (const PhoneMessage& m : s.phone) if (m.side.startsWith(QLatin1String("them"))) ++unread;
            const struct { const char* icon; const char* label; int count; } apps[] = {
                {"app_msg", "Сообщения", unread}, {"app_feed", "Лента", int(s.feed.size())}, {"app_gallery", "Галерея", 0}, {"app_calls", "Звонки", 0},
                {"app_camera", "Камера", 0}, {"app_music", "Музыка", 0}, {"app_notes", "Заметки", 0}, {"app_map", "Карта", 0}};
            const qreal gx = sx + (SW - (4 * 76 + 3 * 26)) / 2.0;
            for (int i = 0; i < 8; ++i) {
                const QPointF at(gx + (i % 4) * (76 + 26), sy + 300 + (i / 4) * (76 + 26 + 26));
                p.drawImage(at, asset(QString::fromLatin1(apps[i].icon)));
                text(QRectF(at.x() - 20, at.y() + 80, 116, 20), U(apps[i].label), font(15, false), Qt::white, Qt::AlignHCenter | Qt::AlignTop);
                if (apps[i].count) {
                    p.drawImage(at + QPointF(56, -8), asset(QStringLiteral("badge")));
                    text(QRectF(at.x() + 56, at.y() - 8, 28, 28), QString::number(qMin(99, apps[i].count)), font(15, true), Qt::white, Qt::AlignCenter);
                }
            }
            const QRectF dock(sx + 21, sy + 830, 430, 110);
            frame9(p, asset(QStringLiteral("dock")), dock, 34);
            text(dock, U("Закрыть телефон"), font(22, true), Qt::white, Qt::AlignCenter);
            p.drawImage(QPointF(ox, oy), asset(QStringLiteral("body")));
        }

        // -- the incoming call: blurred face, ripples, red / green buttons
        if (!s.phoneCall.isEmpty()) {
            p.fillRect(canvas.rect(), QColor(0, 0, 0, 0xaa));
            p.drawImage(QPointF(sx, sy), asset(QStringLiteral("wall")));
            const QString faceName = s.phoneCallFace.isEmpty() ? QString()
                                     : (s.phoneCallFace.contains(QLatin1Char(' ')) ? s.phoneCallFace : s.phoneCallFace + QStringLiteral(" normal pioneer"));
            const QImage face = faceName.isEmpty() ? QImage() : faceThumb(faceName, 200);
            if (!face.isNull()) {         // the blurred caller, faded out softly (the game: AlphaMask with «softmask»)
                QImage glow = blurred(face.scaled(472, 472)).convertToFormat(QImage::Format_ARGB32_Premultiplied);
                QPainter gp(&glow);
                gp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                gp.drawImage(0, 0, asset(QStringLiteral("softmask")));
                gp.end();
                p.save();
                p.setOpacity(0.55);
                p.drawImage(QRectF(sx, sy + 160, SW, SW), glow);
                p.restore();
            }
            frame9(p, asset(QStringLiteral("shade")), QRectF(sx, sy, SW, SH), 60);
            statusBar(QStringLiteral("14:26"));
            text(QRectF(sx, sy + 70, SW, 26), U("Входящий вызов"), font(20, false), QColor(255, 255, 255, 170), Qt::AlignHCenter | Qt::AlignTop);
            text(QRectF(sx, sy + 96, SW, 56), s.phoneCall, font(46, true), Qt::white, Qt::AlignHCenter | Qt::AlignTop);
            text(QRectF(sx, sy + 156, SW, 24), U("мобильный"), font(18, false), QColor(255, 255, 255, 128), Qt::AlignHCenter | Qt::AlignTop);
            const QPointF c(sx + SW / 2, sy + 250 + 160);
            const QImage ringImg = asset(QStringLiteral("ring"));
            for (const auto& rz : {std::pair<qreal, qreal>{0.8, 0.8}, {1.0, 0.5}, {1.2, 0.25}}) {
                p.save();
                p.setOpacity(rz.second);
                const qreal d = 300 * rz.first;
                p.drawImage(QRectF(c.x() - d / 2, c.y() - d / 2, d, d), ringImg);
                p.restore();
            }
            const QRectF av(c.x() - 100, c.y() - 100, 200, 200);
            if (!face.isNull()) {
                p.save();
                QPainterPath clip;
                clip.addEllipse(av);
                p.setClipPath(clip);
                p.drawImage(av, face);
                p.restore();
            } else {
                avatar(s.phoneCall, 200, av.topLeft());
            }
            p.drawImage(QPointF(sx + 70, sy + 770), asset(QStringLiteral("call_no")));
            p.drawImage(QPointF(sx + 306, sy + 770), asset(QStringLiteral("call_yes")));
            text(QRectF(sx + 70, sy + 872, 96, 24), U("Сбросить"), font(18, false), Qt::white, Qt::AlignHCenter | Qt::AlignTop);
            text(QRectF(sx + 306, sy + 872, 96, 24), U("Принять"), font(18, false), Qt::white, Qt::AlignHCenter | Qt::AlignTop);
            p.drawImage(QPointF(ox, oy), asset(QStringLiteral("body")));
        }

        // -- «пуш»: a banner at the top of the game screen
        if (!s.pushWho.isEmpty()) {
            const QRectF bn((W - 620) / 2.0, 24, 620, 96);
            frame9(p, asset(QStringLiteral("banner")), bn, 26);
            avatar(s.pushWho, 52, QPointF(bn.left() + 18, bn.top() + 22));
            text(QRectF(bn.left() + 86, bn.top() + 16, 300, 28), s.pushWho, font(22, true), Qt::white);
            text(QRectF(bn.left() + 86 + QFontMetricsF(font(22, true)).horizontalAdvance(s.pushWho) + 10, bn.top() + 20, 100, 22), U("сейчас"), font(16, false), dim);
            text(QRectF(bn.left() + 86, bn.top() + 48, 510, 40), s.pushText, font(20, false), QColor(0xdd, 0xe8, 0xf5));
        }
    }

    // ---- map, notify toast, floating thought
    if (s.map && m_es) {
        // as ES draws it (control/mapclass.rpyc): the closed map, the open places cut from
        // map_available.jpg, a chibi at the place's top-left corner
        const QImage base = load(QStringLiteral("images/maps/map.jpg"));
        const QImage open = load(QStringLiteral("images/maps/map_available.jpg"));
        if (!base.isNull()) p.drawImage(canvas.rect(), base);
        else if (!open.isNull()) p.drawImage(canvas.rect(), open);
        for (const QString& z : s.mapZones) {
            const QStringList f = z.split(QLatin1Char('|'));
            for (const EsMapZone& mz : esMapZones()) {
                if (mz.id != f.value(0)) continue;
                const QRect r(mz.x1, mz.y1, mz.x2 - mz.x1, mz.y2 - mz.y1);
                if (!open.isNull()) p.drawImage(r, open, open.width() == W ? r : QRect(r.x() * open.width() / W, r.y() * open.height() / H,
                                                                                 r.width() * open.width() / W, r.height() * open.height() / H));
                const QString cf = esChibiFile(f.value(2));
                const QImage chibi = cf.isEmpty() ? QImage() : QImage(m_dataDir + QStringLiteral("/mod_assets/chibi/") + cf + QStringLiteral(".png"));
                // V1 (genry_camp_map): the face beside the place's name
                if (!chibi.isNull()) p.drawImage(QPointF(mz.cx - chibi.width() / 2.0, mz.cy - chibi.height() / 2.0), chibi);
            }
        }
        // the caption over the map, as genry_camp_map draws it
        QFont cf(m_family);
        cf.setPixelSize(36);
        const QString cap = QStringLiteral("Куда пойти?");
        const qreal tw = QFontMetricsF(cf).horizontalAdvance(cap);
        const QRectF cr(W / 2.0 - tw / 2 - 34, 34, tw + 68, QFontMetricsF(cf).height() + 24);
        p.fillRect(cr, QColor(0, 0, 0, 180));
        outlineText(p, QPointF(cr.left() + 34, cr.top() + 12 + QFontMetricsF(cf).ascent()), cap, cf, QColor(0xf1, 0xec, 0xe0), Qt::black, 1);
    }
    if (!s.notifyText.isEmpty()) {
        QFont tf(m_family);
        tf.setPixelSize(22);
        QFont bf(m_family);
        bf.setPixelSize(19);
        const QStringList lines = wrap(s.notifyText, bf, 405);
        const qreal h = 14 * 2 + QFontMetricsF(tf).height() + 4 + lines.size() * QFontMetricsF(bf).height();
        const QRectF r(W - 520 - 28, H * 0.08 * (1 - 0) - 0, 520, h);
        p.fillRect(r, QColor(0x11, 0x18, 0x27, 237));
        p.setFont(tf);
        outlineText(p, QPointF(r.left() + 18, r.top() + 14 + QFontMetricsF(tf).ascent()), s.notifyTitle, tf, QColor(0x7d, 0xd3, 0xfc), Qt::black, 1);
        qreal y = r.top() + 14 + QFontMetricsF(tf).height() + 4 + QFontMetricsF(bf).ascent();
        for (const QString& l : lines) {
            outlineText(p, QPointF(r.left() + 18, y), l, bf, QColor(0xee, 0xf6, 0xff), Qt::black, 1);
            y += QFontMetricsF(bf).height();
        }
    }
    if (!s.floating.isEmpty()) {
        QFont f(m_family);
        f.setPixelSize(38);
        const qreal tw = QFontMetricsF(f).horizontalAdvance(s.floating);
        outlineText(p, QPointF((W - tw) / 2, H * 0.18), s.floating, f, QColor(0xee, 0xf6, 0xff), Qt::black, 2);
    }

    // ---- genry_music_player (a modal list of the mod's tracks)
    if (!s.musicPlayer.isEmpty()) {
        p.fillRect(canvas.rect(), QColor(0, 0, 0, 0xcc));
        QFont title(m_family), item(m_family);
        title.setPixelSize(34);
        item.setPixelSize(24);
        const QFontMetricsF tm(title), im(item);
        const int shown = qMin(int(s.musicPlayer.size()), 10);
        const qreal h = 26 * 2 + tm.height() + 18 + shown * (im.height() + 8) + 10 + im.height();
        const QRectF r((W - 760) / 2.0, (H - h) / 2, 760, h);
        p.fillRect(r, QColor(0x0f, 0x14, 0x1b));
        qreal y = r.top() + 26;
        p.setFont(title);
        p.setPen(QColor(0x7d, 0xd3, 0xfc));
        p.drawText(QPointF(r.left() + 30, y + tm.ascent()), U("Музыкальный плеер"));
        y += tm.height() + 18;
        p.setFont(item);
        for (int i = 0; i < shown; ++i) {
            p.setPen(i == 0 ? QColor(0x7d, 0xd3, 0xfc) : QColor(0xee, 0xf6, 0xff));
            p.drawText(QPointF(r.left() + 30, y + im.ascent()), s.musicPlayer[i]);
            y += im.height() + 8;
        }
        y += 10;
        p.setPen(QColor(0xee, 0xf6, 0xff));
        p.drawText(QPointF(r.left() + 30, y + im.ascent()), U("Стоп"));
        p.drawText(QPointF(r.left() + 30 + im.horizontalAdvance(U("Стоп")) + 24, y + im.ascent()), U("Закрыть"));
    }

    // ---- achievement popup (top right, the mod's own picture)
    if (!s.achievement.isEmpty()) {
        QImage pic = background(QFileInfo(s.achievement).completeBaseName().toLower());
        const QRectF r(W * 0.98 - 420, H * 0.08, 420, 130);
        p.fillRect(r, QColor(0x11, 0x18, 0x27, 237));
        QFont tf(m_family), bf(m_family);
        tf.setPixelSize(24);
        bf.setPixelSize(19);
        if (!pic.isNull()) p.drawImage(QRectF(r.left() + 14, r.top() + 14, 102, 102), pic.scaled(204, 204, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        else outlineText(p, QPointF(r.left() + 36, r.top() + 84), U("★"), tf, QColor(0xff, 0xdd, 0x7d), Qt::black, 1);
        outlineText(p, QPointF(r.left() + 132, r.top() + 50), U("Достижение"), tf, QColor(0xff, 0xdd, 0x7d), Qt::black, 1);
        outlineText(p, QPointF(r.left() + 132, r.top() + 86), QFileInfo(s.achievement).fileName(), bf, QColor(0xee, 0xf6, 0xff), Qt::black, 1);
    }

    // ---- NVL page (nvlначать): the lines so far, stacked like ES's nvl screen
    if (s.nvlMode && !s.nvlPage.isEmpty() && m_es) {
        const QString tod = s.timeOfDay == QLatin1String("prologue") ? QStringLiteral("prologue") : s.timeOfDay;
        frame9(p, load(QStringLiteral("images/gui/choice/%1/choice_box.png").arg(tod)), QRectF(0, 0, W, H), 50);
        QFont base(m_family);
        base.setPixelSize(28);
        const QFontMetricsF bm(base);
        p.setFont(base);
        qreal y = 150 + bm.ascent();
        for (const QString& entry : s.nvlPage) {
            const QString name = entry.section(QLatin1Char('|'), 0, 0), color = entry.section(QLatin1Char('|'), 1, 1);
            const QString text = plain(entry.section(QLatin1Char('|'), 2));
            qreal x = 175;
            if (!name.isEmpty()) {
                shadowText(p, QPointF(x, y), name + QLatin1Char(':'), QColor(color.isEmpty() ? QStringLiteral("#ffdd7d") : color));
                x += bm.horizontalAdvance(name + QStringLiteral(": "));
            }
            const QStringList rows = wrap(text, base, W - 175 - x);
            for (int k = 0; k < rows.size(); ++k) {
                shadowText(p, QPointF(k == 0 ? x : 175, y), rows[k], QColor(0xff, 0xdd, 0x7d));
                y += bm.height() + 2;
            }
            y += 10;
            if (y > H - 150) break;
        }
    }

    // ---- video: a cutscene card / a video background
    if (!s.videoCard.isEmpty() || !s.videoBg.isEmpty()) {
        const bool cut = !s.videoCard.isEmpty();
        if (cut) p.fillRect(canvas.rect(), Qt::black);
        QFont f(m_family);
        f.setPixelSize(cut ? 44 : 26);
        const QString t = (cut ? U("▶ Видеоролик: ") : U("▶ видеофон: ")) + (cut ? s.videoCard : s.videoBg);
        const qreal tw = QFontMetricsF(f).horizontalAdvance(t);
        outlineText(p, cut ? QPointF((W - tw) / 2, H / 2.0) : QPointF(40, H - 60), t, f, QColor(0xee, 0xf6, 0xff), Qt::black, 2);
    }

    // ================= V1 features: the same sizes and colours as their Ren'Py screens
    auto panel = [&](qreal w, qreal h) {
        p.fillRect(canvas.rect(), QColor(0, 0, 0, 0xcc));
        const QRectF r((W - w) / 2, (H - h) / 2, w, h);
        p.fillRect(r, QColor(0x0f, 0x14, 0x1b, 0xf2));
        return r;
    };
    auto font = [&](int px) {
        QFont f(m_family);
        f.setPixelSize(px);
        return f;
    };
    auto closeButton = [&](const QRectF& r) {
        const QFont f = font(28);
        p.setFont(f);
        p.setPen(QColor(0xee, 0xf6, 0xff));
        p.drawText(QRectF(r.left(), r.bottom() - 32 - QFontMetricsF(f).height(), r.width(), QFontMetricsF(f).height()), Qt::AlignHCenter, U("Закрыть"));
    };
    // «выбор на время»: options in a row near the bottom, the white bar melting to the centre
    if (!s.choices.isEmpty() && s.choiceStyle == QLatin1String("timed")) {
        p.fillRect(canvas.rect(), QColor(0, 0, 0, 0x55));
        const QFont f = font(30);
        const QFontMetricsF fm(f);
        QVector<qreal> widths;
        qreal total = 0;
        for (const QString& c : s.choices) {
            widths << qMax<qreal>(320, fm.horizontalAdvance(c) + 60);
            total += widths.last();
        }
        total += 26 * (s.choices.size() - 1);
        const qreal bh = fm.height() + 36;
        const qreal y = H * 0.88 - bh / 2;
        p.fillRect(QRectF((W - 900 * 0.7) / 2, y - 26 - 6, 900 * 0.7, 6), QColor(255, 255, 255, 0xee));   // ~30% of the time gone
        qreal x = (W - total) / 2;
        p.setFont(f);
        for (int i = 0; i < s.choices.size(); ++i) {
            const QRectF r(x, y, widths[i], bh);
            const bool locked = !s.choiceHints.value(i).isEmpty();
            p.fillRect(r, locked ? QColor(0, 0, 0, 0x70) : i == s.choiceHover ? QColor(0x26, 0x20, 0x18, 0xe8) : QColor(0, 0, 0, 0xb8));
            p.setPen(locked ? QColor(0x8a, 0x8a, 0x8a) : i == s.choiceHover ? QColor(0xff, 0xd2, 0x7d) : QColor(0xff, 0xff, 0xff));
            p.setFont(f);
            p.drawText(locked ? r.adjusted(0, 0, 0, -16) : r, Qt::AlignCenter, s.choices[i]);
            if (locked) {
                p.setFont(font(20));
                p.drawText(r.adjusted(0, 26, 0, 0), Qt::AlignCenter, s.choiceHints[i]);
            }
            x += widths[i] + 26;
        }
        const QFont tf = font(22);
        p.setFont(tf);
        p.setPen(QColor(0xff, 0xff, 0xff, 0xb0));
        p.drawText(QRectF(0, y - 70, W, 30), Qt::AlignHCenter, U("%1 сек").arg(s.choiceSeconds.section(QLatin1Char('.'), 0, 0)));
    }
    // the mod's main menu: every style the way menuScreenLines (Compiler.cpp) builds it for the game
    if (s.modMenuOpen) {
        const QString st = s.modMenuStyle;
        const QString title = s.modMenuTitle.isEmpty() ? U("Мой мод") : s.modMenuTitle;
        const QStringList buttons = s.modMenuButtons.isEmpty() ? QStringList{U("Начать"), U("Выход")} : s.modMenuButtons;
        const bool hasAuthor = !s.modMenuAuthor.isEmpty();
        const QString author = U("автор: ") + s.modMenuAuthor;
        auto fontOf = [&](const QString& family, int px, bool italic = false) {
            QFont f(family);
            f.setPixelSize(px);
            f.setItalic(italic);
            return f;
        };
        // a column of centred lines: the first button drawn hovered
        auto centred = [&](qreal cx, qreal y, const QStringList& rows, const QFont& f, const QColor& c, const QColor& hot, qreal gap) {
            const QFontMetricsF fm(f);
            for (int i = 0; i < rows.size(); ++i) {
                const qreal w = fm.horizontalAdvance(rows[i]);
                outlineText(p, QPointF(cx - w / 2, y + fm.ascent()), rows[i], f, i == 0 ? hot : c, Qt::transparent, 0);
                y += fm.height() + gap;
            }
            return y;
        };
        auto softColumn = [&](int width, const QColor& c, int step) {
            p.fillRect(QRectF(0, 0, width, H), c);
            for (int k = 0; k < 10; ++k) {
                QColor e = c;
                e.setAlpha(int(c.alpha() * (10 - k) / 11.0));
                p.fillRect(QRectF(width + step * k, 0, step, H), e);
            }
        };
        auto panel = [&]() {
            // the dark column with a soft edge, corbel title (wraps at 500), author, the first button hovered
            softColumn(540, QColor(0x0b, 0x0f, 0x0c, 0xd8), 16);
            QFont tf(m_header), bf(m_header);
            tf.setPixelSize(64);
            bf.setPixelSize(46);
            const QFont af = font(22);
            const QFontMetricsF tm(tf), bm(bf), am(af);
            const QStringList titleRows = wrap(title, tf, 500);
            const qreal h = titleRows.size() * tm.height() + 8 + (hasAuthor ? am.height() + 8 : 0) + 36 + buttons.size() * (bm.height() + 8);
            qreal y = (H - h) / 2;
            const QImage logo = s.modMenuLogo.isEmpty() ? QImage() : background(QFileInfo(s.modMenuLogo).completeBaseName().toLower());
            if (!logo.isNull()) {
                const QImage sc = logo.scaled(460, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                p.drawImage(QPointF(90, y - sc.height() - 10), sc);
            }
            for (const QString& row : titleRows) {
                outlineText(p, QPointF(90, y + tm.ascent()), row, tf, QColor(0xff, 0xd2, 0x7d), QColor(0, 0, 0, 0x88), 1.5);
                y += tm.height();
            }
            y += 8;
            if (hasAuthor) {
                outlineText(p, QPointF(90, y + am.ascent()), author, af, QColor(0xa9, 0xb9, 0xa4), Qt::black, 0);
                y += am.height() + 8;
            }
            y += 36;
            for (int i = 0; i < buttons.size(); ++i) {
                const bool hot = i == 0;
                const qreal x = 90 + (hot ? 18 : 0);
                if (hot) outlineText(p, QPointF(x, y + bm.ascent()), U("—"), bf, QColor(0xff, 0xd2, 0x7d), Qt::black, 0);
                outlineText(p, QPointF(x + bm.horizontalAdvance(U("—")) + 12, y + bm.ascent()), buttons[i], bf,
                            hot ? QColor(0xff, 0xd2, 0x7d) : QColor(0xee, 0xf6, 0xff), Qt::black, 0);
                y += bm.height() + 8;
            }
        };
        if (st == QLatin1String("es")) {
            // ES's own title board + the owl; the mod's name on a dark strip at the bottom
            const QImage board = load(QStringLiteral("images/gui/title_menu/mainmenu_ground.jpg"));
            if (!board.isNull()) p.drawImage(canvas.rect(), board);
            const QImage owl = load(QStringLiteral("images/gui/title_menu/owl_idle.png"));
            if (!owl.isNull()) p.drawImage(QPointF(135, 606), owl);
            const QFont tf = fontOf(m_header, 40);
            const qreal tw = QFontMetricsF(tf).horizontalAdvance(title);
            const QRectF strip(W / 2.0 - tw / 2 - 30, H * 0.985 - QFontMetricsF(tf).height() - 20, tw + 60, QFontMetricsF(tf).height() + 20);
            p.fillRect(strip, QColor(0, 0, 0, 0xa0));
            outlineText(p, QPointF(strip.left() + 30, strip.top() + 10 + QFontMetricsF(tf).ascent()), title, tf, QColor(0xff, 0xd2, 0x7d), Qt::transparent, 0);
        } else if (st == QLatin1String("7dl")) {
            // 7ДЛ: a heroine big on the right, a softer column on the left, white title, buttons glowing under the cursor
            p.fillRect(canvas.rect(), QColor(0, 0, 0, 0x38));
            if (!s.modMenuHeroes.isEmpty()) {
                const QImage h = sprite(s.modMenuHeroes.first(), s.spriteTime);
                if (!h.isNull()) p.drawImage(QPointF(W * 0.8 - h.width() * 0.8, H - h.height()), h);
            }
            softColumn(560, QColor(0x0b, 0x0f, 0x0c, 0xb0), 16);
            const QFont tf = fontOf(m_header, 70), bf = fontOf(m_header, 42);
            qreal y = H * 0.55 - (QFontMetricsF(tf).height() + 34 + buttons.size() * (QFontMetricsF(bf).height() + 6)) / 2;
            outlineText(p, QPointF(150, y + QFontMetricsF(tf).ascent()), title, tf, Qt::white, QColor(0xff, 0xd2, 0x7d, 0x40), 3);
            y += QFontMetricsF(tf).height() + 34;
            for (int i = 0; i < buttons.size(); ++i) {
                outlineText(p, QPointF(150, y + QFontMetricsF(bf).ascent()), buttons[i], bf, i == 0 ? QColor(0xff, 0xd2, 0x7d) : QColor(0xe8, 0xe8, 0xe8),
                            i == 0 ? QColor(0xff, 0xd2, 0x7d, 0x66) : Qt::transparent, i == 0 ? 2 : 0);
                y += QFontMetricsF(bf).height() + 6;
            }
        } else if (st == QLatin1String("custom")) {
            // «свой»: the same parts the game builds (menuScreenLines «custom»); the first button drawn hovered
            const MenuParts& mp = s.modMenuParts;
            const QString lay = mp.layout.isEmpty() ? QStringLiteral("left") : mp.layout;
            const QString look = mp.look.isEmpty() ? QStringLiteral("text") : mp.look;
            const QColor accent = pyIsHexColor(mp.accent) ? QColor(mp.accent) : QColor(0xff, 0xd2, 0x7d);
            const QColor shade(0x0b, 0x0f, 0x0c, 0xc8);
            if (lay == QLatin1String("left")) {
                softColumn(560, shade, 16);
            } else if (lay == QLatin1String("right")) {
                p.fillRect(QRectF(W - 560, 0, 560, H), shade);
                for (int k = 0; k < 10; ++k) {
                    QColor e = shade;
                    e.setAlpha(int(shade.alpha() * (10 - k) / 11.0));
                    p.fillRect(QRectF(W - 560 - 16 * (k + 1), 0, 16, H), e);
                }
            } else if (lay == QLatin1String("center")) {
                p.fillRect(canvas.rect(), QColor(0, 0, 0, 0x78));
            } else {
                QColor b(0x0b, 0x0f, 0x0c, 0xd0);
                p.fillRect(QRectF(0, H - 260, W, 260), b);
                for (int k = 0; k < 8; ++k) {
                    QColor e = b;
                    e.setAlpha(int(b.alpha() * (8 - k) / 9.0));
                    p.fillRect(QRectF(0, H - 260 - 16 * (k + 1), W, 16), e);
                }
            }
            if (!s.modMenuHeroes.isEmpty()) {
                const qreal xa = lay == QLatin1String("right") ? 0.2 : lay == QLatin1String("center") ? 0.0 : 0.8;
                const QImage h = sprite(s.modMenuHeroes.first(), s.spriteTime);
                if (!h.isNull()) p.drawImage(QPointF((W - h.width()) * xa, H - h.height()), h);
                if (lay == QLatin1String("center") && s.modMenuHeroes.size() > 1) {
                    const QImage h2 = sprite(s.modMenuHeroes[1], s.spriteTime);
                    if (!h2.isNull()) p.drawImage(QPointF((W - h2.width()) * 1.0, H - h2.height()), h2);
                }
            }
            static const QHash<QString, QPair<QColor, QColor>> esColors{
                {QStringLiteral("day"), {QColor(0x46, 0x61, 0x23), QColor(0x9d, 0xcd, 0x55)}}, {QStringLiteral("night"), {QColor(0x14, 0x56, 0x44), QColor(0x3c, 0xcf, 0xa2)}},
                {QStringLiteral("sunset"), {QColor(0x69, 0x65, 0x2f), QColor(0xdc, 0xd1, 0x68)}}, {QStringLiteral("prologue"), {QColor(0x49, 0x64, 0x63), QColor(0x98, 0xd8, 0xda)}}};
            const QString tod = esColors.contains(s.timeOfDay) ? s.timeOfDay : QStringLiteral("day");
            const bool es = look == QLatin1String("es"), plates = look == QLatin1String("plates"), neon = look == QLatin1String("neon");
            const bool bottom = lay == QLatin1String("bottom");
            const QFont tf = fontOf(m_header, es ? 60 : 70), af = font(22);
            const QFont bf = fontOf(m_header, es ? 44 : plates ? 40 : neon ? 48 : 46);
            const QFontMetricsF tm(tf), bm(bf), am(af);
            // one line at (x, y) aligned to the layout: 0 left edge, 1 right edge, 2 the centre
            auto line = [&](const QString& t, const QFont& f, qreal x, qreal y, const QColor& c, const QColor& glow, qreal glowW, int al) {
                const qreal w = QFontMetricsF(f).horizontalAdvance(t);
                const qreal lx = al == 0 ? x : al == 1 ? x - w : x - w / 2;
                outlineText(p, QPointF(lx, y + QFontMetricsF(f).ascent()), t, f, c, glow, glowW);
            };
            const int align = lay == QLatin1String("right") ? 1 : (lay == QLatin1String("center") || bottom) ? 2 : 0;
            const qreal ax = lay == QLatin1String("right") ? 1810 : (lay == QLatin1String("center") || bottom) ? W / 2.0 : 110;
            const QColor titleC = es ? esColors[tod].second : QColor(Qt::white);
            const QColor titleGlow = es ? QColor(Qt::transparent) : QColor(accent.red(), accent.green(), accent.blue(), 0x55);
            const qreal btnH = plates ? bm.height() + 28 : bm.height();
            const qreal gap = bottom ? 44 : plates ? 14 : 8;
            if (bottom) {
                line(title, tf, ax, 150, titleC, titleGlow, es ? 0 : 3, 2);
                if (hasAuthor) line(author, af, ax, 150 + tm.height() + 4, es ? esColors[tod].first : QColor(0xc8, 0xc8, 0xc8), Qt::transparent, 0, 2);
            }
            auto widthOf = [&](const QString& b) { return plates ? qMax(420.0, bm.horizontalAdvance(b) + 68) : bm.horizontalAdvance(b); };
            qreal blockW = 0;
            for (const QString& b : buttons) blockW = bottom ? blockW + widthOf(b) + (blockW > 0 ? gap : 0) : qMax(blockW, widthOf(b));
            const qreal blockH = bottom ? btnH : buttons.size() * btnH + (buttons.size() - 1) * gap;
            const qreal headH = bottom ? 0 : tm.height() + (hasAuthor ? am.height() + 6 : 0) + 30;
            if (!bottom) blockW = qMax(blockW, tm.horizontalAdvance(title));
            const qreal top = bottom ? H * 0.93 - blockH / 2 : H * (lay == QLatin1String("center") ? 0.55 : 0.5) - (headH + blockH) / 2;
            if (es) {                                // ES's choice box of the menu's hour around the block
                const qreal left = align == 0 ? 90 : align == 1 ? 1810 - blockW - 140 : W / 2.0 - blockW / 2 - 70;
                frame9(p, load(QStringLiteral("images/gui/choice/%1/choice_box.png").arg(tod)), QRectF(left, top - 40, blockW + 140, headH + blockH + 80), 50);
            }
            const qreal x0 = es ? (align == 0 ? 160 : align == 1 ? 1810 - 70 : W / 2.0) : ax;
            qreal y = top;
            if (!bottom) {
                line(title, tf, x0, y, titleC, titleGlow, es ? 0 : 3, align);
                y += tm.height();
                if (hasAuthor) {
                    line(author, af, x0, y + 6, es ? esColors[tod].first : QColor(0xc8, 0xc8, 0xc8), Qt::transparent, 0, align);
                    y += am.height() + 6;
                }
                y += 30;
            }
            qreal bx = W / 2.0 - blockW / 2;
            for (int i = 0; i < buttons.size(); ++i) {
                const bool hot = i == 0;
                const qreal bw = widthOf(buttons[i]);
                if (plates) {
                    const qreal px = bottom ? bx : align == 0 ? x0 : align == 1 ? x0 - bw : x0 - bw / 2;
                    p.fillRect(QRectF(px, y, bw, btnH), hot ? QColor(accent.red(), accent.green(), accent.blue(), 0x55) : QColor(0, 0, 0, 0xa8));
                    outlineText(p, QPointF(px + 34, y + 14 + bm.ascent()), buttons[i], bf, hot ? accent : QColor(Qt::white), Qt::transparent, 0);
                } else {
                    const QColor c = es ? (hot ? esColors[tod].second : esColors[tod].first) : hot ? accent : QColor(0xee, 0xf6, 0xff);
                    const QColor glow = neon ? QColor(accent.red(), accent.green(), accent.blue(), hot ? 0xaa : 0x55) : es ? QColor(Qt::transparent) : QColor(0, 0, 0, 0x88);
                    const qreal gw = neon ? (hot ? 7 : 3) : es ? 0 : 2;
                    if (bottom) line(buttons[i], bf, bx, y, c, glow, gw, 0);
                    else line(buttons[i], bf, x0, y, c, glow, gw, align);
                }
                if (bottom) bx += bw + gap;
                else y += btnH + gap;
            }
        } else if (st == QLatin1String("notebook")) {
            const QImage bg = load(QStringLiteral("images/gui/settings/preferences_bg.jpg"));
            if (!bg.isNull()) p.drawImage(canvas.rect(), bg);
            qreal y = 260;
            y = centred(960, y, wrap(title, fontOf(m_header, 62), 820), fontOf(m_header, 62), QColor(0x64, 0x48, 0x3c), QColor(0x64, 0x48, 0x3c), 0);
            if (hasAuthor) y = centred(960, y + 6, {author}, font(24), QColor(0x8f, 0x7a, 0x68), QColor(0x8f, 0x7a, 0x68), 0);
            centred(960, y + 26, buttons, fontOf(m_header, 46), QColor(0x64, 0x48, 0x3c), QColor(0xb3, 0x00, 0x1b), 10);
        } else if (st == QLatin1String("diary")) {
            const QImage bg = load(QStringLiteral("images/gui/settings/history_bg.jpg"));
            if (!bg.isNull()) p.drawImage(canvas.rect(), bg);
            const QFont tf = fontOf(m_header, 60, true), bf = fontOf(m_header, 44, true);
            const QColor ink(0x2d, 0x3f, 0x66);
            qreal y = 190;
            outlineText(p, QPointF(520, y + QFontMetricsF(tf).ascent()), title, tf, ink, Qt::transparent, 0);
            y += QFontMetricsF(tf).height() + 8;
            if (hasAuthor) {
                outlineText(p, QPointF(520, y + QFontMetricsF(font(24)).ascent()), author, font(24), QColor(0x5d, 0x6b, 0x86), Qt::transparent, 0);
                y += QFontMetricsF(font(24)).height() + 8;
            }
            y += 24;
            for (int i = 0; i < buttons.size(); ++i) {
                outlineText(p, QPointF(520, y + QFontMetricsF(bf).ascent()), QString::number(i + 1) + QStringLiteral(". ") + buttons[i], bf,
                            i == 0 ? QColor(0xb3, 0x00, 0x1b) : ink, Qt::transparent, 0);
                y += QFontMetricsF(bf).height() + 8;
            }
        } else if (st == QLatin1String("board")) {
            QString tod = s.timeOfDay;
            if (tod != QLatin1String("sunset") && tod != QLatin1String("night") && tod != QLatin1String("prologue")) tod = QStringLiteral("day");
            const QImage board = load(QStringLiteral("images/gui/ingame_menu/%1/ingame_menu.png").arg(tod));
            if (!board.isNull()) p.drawImage(QPointF((W - board.width()) / 2.0, (H - board.height()) / 2.0), board);
            const QFont tf = fontOf(m_header, 44), bf = fontOf(m_header, buttons.size() > 6 ? 32 : 38);
            const qreal h = QFontMetricsF(tf).height() + 12 + buttons.size() * (QFontMetricsF(bf).height() + 2);
            const qreal y = centred(W / 2.0, (H - h) / 2, {title}, tf, QColor(0x64, 0x48, 0x3c), QColor(0x64, 0x48, 0x3c), 0);
            centred(W / 2.0, y + 12, buttons, bf, QColor(0x64, 0x48, 0x3c), QColor(0xb3, 0x00, 0x1b), 2);
        } else if (st == QLatin1String("monitor")) {
            const QImage back = load(QStringLiteral("images/anim/backdrop/back.jpg"));
            if (!back.isNull()) p.drawImage(canvas.rect(), back);
            const QImage noise = load(QStringLiteral("images/anim/backdrop/1.png"));
            if (!noise.isNull()) p.drawImage(canvas.rect(), noise);
            const QFont tf = fontOf(m_family, 88);
            centred(W * 0.455, H * 0.28 - QFontMetricsF(tf).height() / 2, {title}, tf, QColor(216, 216, 216, 184), QColor(216, 216, 216, 184), 0);
            centred(W * 0.455, H * 0.36, buttons, fontOf(m_family, buttons.size() > 6 ? 26 : 32), QColor(0xcf, 0xd4, 0xdc, 0xb0), Qt::white, 0);
        } else if (st == QLatin1String("noir")) {
            Mat m = mul(mul(saturation(0.0), brightness(-0.1)), contrast(1.25));
            p.end();
            apply(canvas, m);
            p.begin(&canvas);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.setRenderHint(QPainter::TextAntialiasing);
            softColumn(820, QColor(0, 0, 0, 0xb8), 18);
            QFont tf(QStringLiteral("Times New Roman")), bf(QStringLiteral("Times New Roman"));
            tf.setPixelSize(80);
            bf.setPixelSize(44);
            const qreal h = QFontMetricsF(tf).height() + 10 + (hasAuthor ? 30 : 0) + 30 + buttons.size() * (QFontMetricsF(bf).height() + 6);
            qreal y = (H - h) / 2;
            outlineText(p, QPointF(120, y + QFontMetricsF(tf).ascent()), title, tf, QColor(0xf2, 0xf2, 0xf2), Qt::transparent, 0);
            y += QFontMetricsF(tf).height();
            p.fillRect(QRectF(120, y + 2, 240, 4), QColor(0xc0, 0x39, 0x2b));
            y += 10;
            if (hasAuthor) {
                QFont af(QStringLiteral("Times New Roman"));
                af.setPixelSize(24);
                outlineText(p, QPointF(120, y + QFontMetricsF(af).ascent()), author, af, QColor(0x9a, 0x9a, 0x9a), Qt::transparent, 0);
                y += 30;
            }
            y += 30;
            for (int i = 0; i < buttons.size(); ++i) {
                outlineText(p, QPointF(120, y + QFontMetricsF(bf).ascent()), buttons[i], bf, i == 0 ? QColor(0xe7, 0x4c, 0x3c) : QColor(0xd0, 0xd0, 0xd0),
                            Qt::transparent, 0);
                y += QFontMetricsF(bf).height() + 6;
            }
        } else if (st == QLatin1String("cinema")) {
            p.fillRect(QRectF(0, 0, W, 140), Qt::black);
            p.fillRect(QRectF(0, 940, W, 140), Qt::black);
            const QFont tf = fontOf(m_header, 88);
            const QFontMetricsF tm(tf);
            outlineText(p, QPointF(W / 2.0 - tm.horizontalAdvance(title) / 2, H * 0.46 - tm.height() / 2 + tm.ascent()), title, tf,
                        QColor(0xf5, 0xf5, 0xf5), QColor(0, 0, 0, 0x99), 3);
            if (hasAuthor) centred(W / 2.0, H * 0.555 - 14, {author}, font(24), QColor(0xcf, 0xcf, 0xcf), QColor(0xcf, 0xcf, 0xcf), 0);
            const QFont bf = fontOf(m_header, 34);
            const QFontMetricsF bm(bf);
            qreal total = 0;
            for (const QString& b : buttons) total += bm.horizontalAdvance(b) + 56;
            qreal x = (W - total + 56) / 2;
            for (int i = 0; i < buttons.size(); ++i) {
                outlineText(p, QPointF(x, 1010 - bm.height() / 2 + bm.ascent()), buttons[i], bf, i == 0 ? QColor(0xff, 0xd2, 0x7d) : QColor(0xcf, 0xcf, 0xcf),
                            Qt::transparent, 0);
                x += bm.horizontalAdvance(buttons[i]) + 56;
            }
        } else if (st == QLatin1String("map")) {
            // the camp map: every button lit on its own place, its caption written there
            const QImage base = load(QStringLiteral("images/maps/map.jpg"));
            const QImage open = load(QStringLiteral("images/maps/map_available.jpg"));
            const QImage lit = load(QStringLiteral("images/maps/map_selected.jpg"));
            if (!base.isNull()) p.drawImage(canvas.rect(), base);
            QStringList kinds = s.modMenuKinds;
            while (kinds.size() < buttons.size()) kinds << QString();
            const QStringList places = menuMapPlaces(kinds.mid(0, buttons.size()));
            const QFont cf = fontOf(m_header, 30);
            for (int i = 0; i < buttons.size(); ++i) {
                const EsMapZone* z = esMapZone(places.value(i));
                if (!z) continue;
                const QRect r(z->x1, z->y1, z->x2 - z->x1, z->y2 - z->y1);
                const QImage& src = i == 0 && !lit.isNull() ? lit : open;
                if (!src.isNull()) p.drawImage(r, src, src.width() == W ? r : QRect(r.x() * src.width() / W, r.y() * src.height() / H,
                                                                                 r.width() * src.width() / W, r.height() * src.height() / H));
            }
            for (int i = 0; i < buttons.size(); ++i) {
                const EsMapZone* z = esMapZone(places.value(i));
                if (!z) continue;
                const QFontMetricsF fm(cf);
                outlineText(p, QPointF(z->cx - fm.horizontalAdvance(buttons[i]) / 2, z->cy - fm.height() / 2 + fm.ascent()), buttons[i], cf, Qt::white,
                            QColor(0, 0, 0, 0xcc), 3);
            }
            const QFont hf = fontOf(m_header, 36);
            const qreal tw = QFontMetricsF(hf).horizontalAdvance(title);
            const QRectF cr(W / 2.0 - tw / 2 - 34, 34, tw + 68, QFontMetricsF(hf).height() + 24);
            p.fillRect(cr, QColor(0, 0, 0, 180));
            outlineText(p, QPointF(cr.left() + 34, cr.top() + 12 + QFontMetricsF(hf).ascent()), title, hf, QColor(0xf1, 0xec, 0xe0), Qt::black, 1);
        } else {
            panel();                     // «панель» and «живое» (its picture follows the player's clock in the game)
        }
    }
    // relationship meters screen
    if (s.showMeters && !s.meters.isEmpty()) {
        const qreal rowH = 38 + 16;
        const QRectF r = panel(44 * 2 + 280 + 22 + 380 + 22 + 60, 32 * 2 + 50 + 20 + s.meters.size() * rowH + 60);
        outlineText(p, QPointF(r.left() + 44, r.top() + 32 + 40), U("Отношения"), font(42), QColor(0xff, 0xd2, 0x7d), Qt::black, 0);
        qreal y = r.top() + 32 + 70;
        for (const QStringList& m : s.meters) {
            const double lo = m.value(3).toDouble(), hi = m.value(4).toDouble(), v = s.meterValues.value(m.value(2), lo);
            outlineText(p, QPointF(r.left() + 44, y + 28), m.value(0), font(28), QColor(m.value(1)), Qt::black, 0);
            const QRectF bar(r.left() + 44 + 280 + 22, y + 12, 380, 14);
            p.fillRect(bar, QColor(255, 255, 255, 0x1a));
            p.fillRect(QRectF(bar.left(), bar.top(), bar.width() * qBound(0.0, (v - lo) / qMax(1.0, hi - lo), 1.0), bar.height()), QColor(m.value(1)));
            outlineText(p, QPointF(bar.right() + 22, y + 28), pyRepr(v).remove(QStringLiteral(".0")), font(26), Qt::white, Qt::black, 0);
            y += rowH;
        }
        closeButton(r);
    }
    // inventory screen
    if (s.showInventory) {
        const int n = qMax(1, int(s.inventory.size()));
        const QRectF r = panel(900, 32 * 2 + 50 + 16 + n * (76 + 16) + 60);
        outlineText(p, QPointF(r.left() + 44, r.top() + 32 + 40), U("Инвентарь"), font(42), QColor(0xff, 0xd2, 0x7d), Qt::black, 0);
        qreal y = r.top() + 32 + 66;
        if (s.inventory.isEmpty()) outlineText(p, QPointF(r.left() + 44, y + 26), U("Пусто."), font(26), QColor(0xa9, 0xb9, 0xa4), Qt::black, 0);
        for (const QString& it : s.inventory) {
            p.fillRect(QRectF(r.left() + 44, y, 76, 76), QColor(0x27, 0x38, 0x2f));
            outlineText(p, QPointF(r.left() + 44 + 76 + 18, y + 48), it, font(28), Qt::white, Qt::black, 0);
            y += 76 + 16;
        }
        closeButton(r);
    }
    // the mod's gallery: the CGs it uses (in game only those already seen open up)
    if (s.showGallery) {
        p.fillRect(canvas.rect(), QColor(0, 0, 0, 0xe6));
        outlineText(p, QPointF(W / 2.0 - 80, 140), U("Галерея"), font(42), QColor(0xff, 0xd2, 0x7d), Qt::black, 0);
        const int cols = 4;
        const qreal tw = 345, th = 194, gap = 16;
        const qreal x0 = (W - (cols * tw + (cols - 1) * gap)) / 2;
        for (int i = 0; i < s.galleryCgs.size() && i < 12; ++i) {
            const QRectF cell(x0 + (i % cols) * (tw + gap), 180 + (i / cols) * (th + gap), tw, th);
            const QImage img = background(s.galleryCgs[i]);
            if (img.isNull()) p.fillRect(cell, QColor(0x1b, 0x2a, 0x22));
            else p.drawImage(cell, img);
        }
    }
    // the mod's achievements (Достижения 2.0): a twin of ES's gallery screen - history_bg, «★ Достижения n/m ★» in
    // settings_link, 3x3 cards in ES's 336x196 frames, the closed eye of an unseen one, «Назад»; the info line of the first
    if (s.showAchievements) {
        const QImage bg = load(QStringLiteral("images/gui/settings/history_bg.jpg"));
        if (!bg.isNull()) p.drawImage(QRectF(0, 0, W, H), bg);
        else p.fillRect(QRectF(0, 0, W, H), QColor(0x1a, 0x14, 0x10));
        QFont link(m_link);
        link.setPixelSize(60);
        int got = 0;
        for (const QString& a : s.achievements) if (a.section(QLatin1Char('|'), 1, 1) == QLatin1String("1")) ++got;
        const QString head = U(" Достижения %1/%2 ").arg(got).arg(s.achievements.size());
        const QFontMetricsF lm(link);
        const QImage star = load(QStringLiteral("images/gui/settings/star.png"));
        const qreal hw = lm.horizontalAdvance(head) + 2 * star.width();
        qreal hx = (W - hw) / 2;
        const qreal hy = H * 0.08;
        if (!star.isNull()) p.drawImage(QPointF(hx, hy - star.height() / 2.0 + 6), star);
        p.setFont(link);
        p.setPen(Qt::white);
        p.drawText(QRectF(hx + star.width(), hy - lm.height() / 2, lm.horizontalAdvance(head), lm.height()), Qt::AlignCenter, head);
        if (!star.isNull()) p.drawImage(QPointF(hx + star.width() + lm.horizontalAdvance(head), hy - star.height() / 2.0 + 6), star);
        const QImage frame = load(QStringLiteral("images/gui/gallery/thumbnail_idle.png"));
        const QImage eye = load(QStringLiteral("images/gui/gallery/not_opened_idle.png"));
        const QImage plate = load(QStringLiteral("images/misc/achievement2.png"));
        for (int i = 0; i < s.achievements.size() && i < 9; ++i) {
            const QStringList f = s.achievements[i].split(QLatin1Char('|'));
            const bool on = f.value(1) == QLatin1String("1"), hidden = f.value(2) == QLatin1String("h") && !on;
            const QPointF at(W * 0.09 + (i % 3) * 386, H * 0.18 + (i / 3) * 246);
            if (hidden) {
                if (!eye.isNull()) p.drawImage(at, eye);
                continue;
            }
            p.fillRect(QRectF(at.x() + 8, at.y() + 8, 320, 180), QColor(0x1c, 0x1f, 0x20));
            // its picture: the heroine's face, a background or CG (grey while not yet), else ES's ring with the heart
            QImage pic;
            const QString img = f.value(3);
            if (!img.isEmpty()) {
                QString kind;
                const QString name = choiceImage(img, &kind);
                if (kind == QLatin1String("sprite")) {
                    const QImage sp = sprite(name);
                    if (!sp.isNull()) pic = sp.copy(290, 200, 320, 180);
                } else {
                    const QImage b = background(name);
                    if (!b.isNull()) pic = b.scaled(320, 180, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                }
            }
            if (!pic.isNull()) {
                p.save();
                p.setOpacity(on ? 1.0 : 0.55);
                p.drawImage(QPointF(at.x() + 8, at.y() + 8), on ? pic : pic.convertToFormat(QImage::Format_Grayscale8));
                p.restore();
            } else if (!plate.isNull()) {
                p.save();
                p.setOpacity(on ? 1.0 : 0.45);
                const QImage ring = plate.copy(0, 0, 78, 95).scaled(117, 142, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                p.drawImage(QPointF(at.x() + 168 - ring.width() / 2.0, at.y() + 98 - ring.height() / 2.0), on ? ring : ring.convertToFormat(QImage::Format_Grayscale8));
                p.restore();
            }
            if (!frame.isNull()) p.drawImage(at, frame);
        }
        if (!s.achievements.isEmpty()) {
            const QStringList f = s.achievements[0].split(QLatin1Char('|'));
            const bool on = f.value(1) == QLatin1String("1"), hidden = f.value(2) == QLatin1String("h") && !on;
            QFont t(m_link);
            t.setPixelSize(42);
            p.setFont(t);
            p.setPen(on ? QColor(Qt::white) : QColor(0x90, 0x9c, 0xa3));
            p.drawText(QRectF(0, H * 0.86 - 30, W, 60), Qt::AlignCenter, hidden ? QStringLiteral("???") : f.value(0));
        }
        QFont back(m_link);
        back.setPixelSize(60);
        p.setFont(back);
        p.setPen(QColor(0x90, 0x9c, 0xa3));
        p.drawText(QRectF(W * 0.015, H * 0.92 - 40, 400, 80), Qt::AlignLeft | Qt::AlignVCenter, U("Назад"));
    }
    // «кодовыйзамок» (genry_codelock): ES's o_rly plate - the hint and the empty cells on the paper, the digits, «Ввести» /
    // «Уйти» and the tries in settings_link below
    if (!s.codeLock.isEmpty()) {
        const QStringList f = s.codeLock.split(QLatin1Char('|'));
        const QImage plate = load(QStringLiteral("images/gui/o_rly/base.png"));
        if (!plate.isNull()) p.drawImage(QRectF(0, 0, W, H), plate);
        else p.fillRect(canvas.rect(), QColor(0, 0, 0, 0xc0));
        QFont hint(m_header), cells(m_header), link(m_link), small(m_header);
        hint.setPixelSize(32);
        cells.setPixelSize(60);
        link.setPixelSize(54);
        small.setPixelSize(26);
        auto centre = [&](const QString& t, const QFont& fn, qreal yalign, const QColor& c) {
            const QFontMetricsF fm(fn);
            p.setFont(fn);
            p.setPen(c);
            p.drawText(QRectF(0, (H - fm.height()) * yalign, W, fm.height()), Qt::AlignHCenter | Qt::AlignVCenter, t);
        };
        centre(f.value(0), hint, 0.415, QColor(0x64, 0x48, 0x3c));
        QStringList blanks;
        for (int i = 0; i < qMax(1, f.value(1).toInt()); ++i) blanks << QStringLiteral("_");
        centre(blanks.join(QStringLiteral("  ")), cells, 0.5, QColor(0x3f, 0x2a, 0x1d));
        centre(QStringLiteral("1   2   3   4   5   6   7   8   9   0   ←"), link, 0.63, QColor(0x90, 0x9c, 0xa3));
        centre(U("Ввести          Уйти"), link, 0.735, QColor(0x90, 0x9c, 0xa3));
        if (f.value(2).toInt() > 0) centre(U("попыток осталось: %1").arg(f.value(2)), small, 0.81, QColor(0x90, 0x9c, 0xa3));
    }
    // ES's achievement plate (achievement2.png: the ring, the capsule, «Achievement unlocked», the title in lime)
    // where achievement_trans stops it, bottom right
    if (!s.achievementPlate.isEmpty()) {
        const QImage base = load(QStringLiteral("images/misc/achievement2.png"));
        QFont sub(m_family), ttl(m_family);
        sub.setPixelSize(22);
        ttl.setPixelSize(26);
        const int w = qMax(443, int(92 + QFontMetricsF(ttl).horizontalAdvance(s.achievementPlate) + 48));
        const QPointF at((W - w) * 0.95, (H - 95) * 0.97);
        if (!base.isNull()) {
            p.drawImage(at, base.copy(0, 0, 86, 95));
            p.drawImage(QRectF(at.x() + 86, at.y(), w - 86 - 43, 95), base.copy(80, 0, 4, 95));
            p.drawImage(QPointF(at.x() + w - 43, at.y()), base.copy(400, 0, 43, 95));
        } else {
            p.setBrush(QColor(0x3b, 0x3f, 0x40));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(at, QSizeF(w, 95)), 47, 47);
        }
        p.setFont(sub);
        p.setPen(Qt::white);
        p.drawText(QRectF(at.x() + 91, at.y() + 12, w - 100, 28), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Achievement unlocked"));
        p.setFont(ttl);
        p.setPen(QColor(0xb6, 0xff, 0x00));
        p.drawText(QRectF(at.x() + 91, at.y() + 42, w - 100, 34), Qt::AlignLeft | Qt::AlignVCenter, s.achievementPlate);
    }
    // ♥ / Инвентарь buttons of the mod, top right
    if (s.metersButton) {
        const QFont f = font(26);
        outlineText(p, QPointF(W * 0.985 - QFontMetricsF(f).horizontalAdvance(U("Отношения")), 24 + 26), U("Отношения"), f, QColor(0xff, 0xd2, 0x7d, 0xcc), Qt::black, 1);
    }
    if (s.inventoryButton) {
        const QFont f = font(26);
        outlineText(p, QPointF(W * 0.985 - QFontMetricsF(f).horizontalAdvance(U("Инвентарь")), 72 + 26), U("Инвентарь"), f, QColor(0xee, 0xf6, 0xff, 0xcc), Qt::black, 1);
    }
    // «Алиса это запомнит.» - top left
    if (!s.remember.isEmpty()) {
        const QFont f = font(28);
        const qreal tw = QFontMetricsF(f).horizontalAdvance(s.remember);
        const QRectF r(40, 40, 20 + 6 + 14 + tw + 20, 14 * 2 + 34);
        p.fillRect(r, QColor(0, 0, 0, 0xb0));
        p.fillRect(QRectF(r.left() + 20, r.top() + 15, 6, 32), QColor(0xff, 0xd2, 0x7d));
        outlineText(p, QPointF(r.left() + 20 + 6 + 14, r.top() + 14 + 26), s.remember, f, Qt::white, Qt::black, 0);
    }
    // a meter changed: «Алиса +2» with its bar, top left under «запомнит»
    if (s.meterPing.size() >= 6) {
        const QFont f = font(26);
        const QFontMetricsF fm(f);
        const double lo = s.meterPing[4].toDouble(), hi = s.meterPing[5].toDouble(), v = s.meterPing[3].toDouble();
        double d = 0;
        pyFloat(s.meterPing[2], &d);
        const QString delta = (d > 0 ? QStringLiteral("+") : QString()) + s.meterPing[2];
        const qreal w = qMax<qreal>(280, fm.horizontalAdvance(s.meterPing[0] + QStringLiteral("  ") + delta)) + 44;
        const QRectF r(40, 120, w, 14 * 2 + fm.height() + 8 + 8);
        p.fillRect(r, QColor(0, 0, 0, 0xb8));
        outlineText(p, QPointF(r.left() + 22, r.top() + 14 + fm.ascent()), s.meterPing[0], f, QColor(s.meterPing[1]), Qt::black, 0);
        outlineText(p, QPointF(r.left() + 22 + fm.horizontalAdvance(s.meterPing[0]) + 12, r.top() + 14 + fm.ascent()), delta, f,
                    d > 0 ? QColor(0x9b, 0xd3, 0x5a) : QColor(0xff, 0x6b, 0x6b), Qt::black, 0);
        const QRectF bar(r.left() + 22, r.top() + 14 + fm.height() + 8, 280, 8);
        p.fillRect(bar, QColor(255, 255, 255, 0x22));
        p.fillRect(QRectF(bar.left(), bar.top(), bar.width() * qBound(0.0, (v - lo) / qMax(1.0, hi - lo), 1.0), 8), QColor(s.meterPing[1]));
    }

    // ---- editor HUD: what plays and what happens on this line
    if (hud) {
        QFont f(m_family);
        f.setPixelSize(24);
        p.setFont(f);
        const QFontMetricsF fm(f);
        qreal x = 20;
        auto chip = [&](const QString& t, const QColor& c) {
            const qreal tw = fm.horizontalAdvance(t);
            p.fillRect(QRectF(x, 20, tw + 32, 44), QColor(0, 0, 0, 150));
            p.setPen(c);
            p.drawText(QRectF(x + 16, 20, tw + 8, 44), Qt::AlignVCenter, t);
            x += tw + 32 + 10;
        };
        if (!s.music.isEmpty()) chip(U("♪ ") + s.music, QColor(0xff, 0xdd, 0x7d));
        if (!s.ambience.isEmpty()) chip(U("≈ ") + s.ambience, QColor(0x9b, 0xd3, 0x5a));
        if (!s.sound.isEmpty()) chip(U("♬ ") + s.sound, QColor(0x8b, 0xe9, 0xfd));
        if (!s.moment.isEmpty()) chip(U("✦ ") + s.moment, QColor(0xff, 0xb8, 0x6c));
    }
    p.end();
    return canvas;
}

QImage Renderer::modsList(const QString& title, const QString& family, const QString& color, int size, bool bold, bool italic) const
{
    // screens.rpy `screen mods`: preferences_bg.jpg, the star-framed header (settings_link: gothic 60,
    // kerning 3) at yalign 0.08, the list in area (0.27, 0.24, 0.47, 0.70) of settings_text buttons
    // (corbel 36, #4d2e19); the mod's own text tags override font / colour / size.
    QImage canvas(W, H, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QImage bg = load(QStringLiteral("images/gui/settings/preferences_bg.jpg"));
    if (!bg.isNull()) p.drawImage(canvas.rect(), bg);
    QFont head(m_link);
    head.setPixelSize(60);
    head.setLetterSpacing(QFont::AbsoluteSpacing, 3);
    const QFontMetricsF hm(head);
    const QString header = U(" Моды и пользовательские сценарии ");
    const QImage star = load(QStringLiteral("images/gui/settings/star.png"));
    const qreal hw = hm.horizontalAdvance(header) + (star.isNull() ? 0 : 2 * star.width());
    qreal hx = (W - hw) / 2;
    const qreal hy = H * 0.08;
    if (!star.isNull()) { p.drawImage(QPointF(hx, hy - star.height() * 0.35), star); hx += star.width(); }
    p.setFont(head);
    p.setPen(QColor(0xff, 0xff, 0xff));
    p.drawText(QPointF(hx, hy + hm.ascent() * 0.5), header);
    if (!star.isNull()) p.drawImage(QPointF(hx + hm.horizontalAdvance(header), hy - star.height() * 0.35), star);

    const QRectF area(W * 0.27, H * 0.24, W * 0.47, H * 0.70);
    auto row = [&](qreal y, const QString& text, const QString& fam, const QColor& c, int px, bool b, bool i) {
        QFont f(fam);
        f.setPixelSize(px);
        f.setBold(b);
        f.setItalic(i);
        const QFontMetricsF fm(f);
        p.setFont(f);
        p.setPen(c);
        p.drawText(QRectF(area.left(), y, area.width(), fm.height() + 4), Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(text, Qt::ElideRight, area.width()));
        return fm.height() + 110;           // log_button rows: the text + ES's roomy button spacing
    };
    qreal y = area.top() + 40;
    y += row(y, title.isEmpty() ? U("Мой мод") : title, family.isEmpty() ? m_header : family,
             QColor(color.isEmpty() ? QStringLiteral("#4d2e19") : color), size > 0 ? size : 36, bold, italic);
    for (const char* other : {"Другой мод из Мастерской", "Ещё один мод"})
        y += row(y, U(other), m_header, QColor(0x4d, 0x2e, 0x19, 0x70), 36, false, false);
    p.end();
    return canvas;
}

QVector<QRectF> Renderer::choiceRects(const SceneState& s) const
{
    // the same layout render() draws (the ES menu, the buttons, the 7DL strips, the timed row)
    QVector<QRectF> out;
    const int n = int(s.choices.size());
    if (!n || s.screenMenu || s.choiceStyle == QLatin1String("phone")) return out;
    if (s.choiceStyle == QLatin1String("images")) {
        const int sw = W / n;
        for (int i = 0; i < n; ++i) out << QRectF(i * sw, 0, sw, H);
        return out;
    }
    if (s.choiceStyle == QLatin1String("timed")) {
        QFont f(m_family);
        f.setPixelSize(30);
        const QFontMetricsF fm(f);
        QVector<qreal> widths;
        qreal total = 0;
        for (const QString& c : s.choices) {
            widths << qMax<qreal>(320, fm.horizontalAdvance(c) + 60);
            total += widths.last();
        }
        total += 26 * (n - 1);
        const qreal bh = fm.height() + 36, y = H * 0.88 - bh / 2;
        qreal x = (W - total) / 2;
        for (int i = 0; i < n; ++i) {
            out << QRectF(x, y, widths[i], bh);
            x += widths[i] + 26;
        }
        return out;
    }
    if (s.choiceStyle == QLatin1String("buttons")) {
        QFont f(m_family);
        f.setPixelSize(28);
        const qreal bw = 680, bh = QFontMetricsF(f).height() + 36, gap = 16;
        const qreal total = n * bh + (n - 1) * gap;
        qreal y = H * 0.45 - total / 2;
        for (int i = 0; i < n; ++i) {
            out << QRectF((W - bw) / 2, y, bw, bh);
            y += bh + gap;
        }
        return out;
    }
    QFont hf(m_header), hintF(m_header);
    hf.setPixelSize(37);
    hintF.setPixelSize(22);
    const qreal rowH = QFontMetricsF(hf).height() + 12, hintH = QFontMetricsF(hintF).height();
    qreal boxH = 100 + n * rowH;
    for (const QString& h : s.choiceHints) if (!h.isEmpty()) boxH += hintH;
    qreal y = (H - boxH) / 2 + 50;
    for (int i = 0; i < n; ++i) {
        const qreal h = rowH + (s.choiceHints.value(i).isEmpty() ? 0 : hintH);
        out << QRectF(75, y, W - 150, h);
        y += h;
    }
    return out;
}

} // namespace gb
