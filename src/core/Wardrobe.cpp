// GenryBL V1 - «Гардероб мастерской» (see Wardrobe.h).
#include "Wardrobe.h"
#include "EsAssets.h"
#include "Library.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <atomic>
#include <climits>

namespace gb {

namespace {

std::atomic<const Wardrobe*> g_wardrobe{nullptr};

QString U(const char* s) { return QString::fromUtf8(s); }

// sprites/<dist>/<tag>/<tag>_<pose>_<part>.png (ES: images/sprites/…, the resource pack: sprites/…)
const QRegularExpression& layerRe()
{
    static const QRegularExpression re(QStringLiteral("(?:^|/)sprites?/(normal|close|far)/([a-z]+)/([a-z]+)_(\\d+)_([^/]+)\\.png$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// a part becomes a word of a Ren'Py image name: plain latin, not a word Ren'Py or ES already use
bool usablePart(const QString& p)
{
    static const QRegularExpression ok(QStringLiteral("^[a-z][a-z0-9_]*$"));
    static const QSet<QString> reserved{QStringLiteral("close"), QStringLiteral("far"), QStringLiteral("at"), QStringLiteral("with"),
                                        QStringLiteral("as"), QStringLiteral("behind"), QStringLiteral("onlayer"), QStringLiteral("zorder"),
                                        QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("center"), QStringLiteral("truecenter"),
                                        QStringLiteral("fleft"), QStringLiteral("fright"), QStringLiteral("cleft"), QStringLiteral("cright")};
    return ok.match(p).hasMatch() && !reserved.contains(p) && !p.startsWith(QLatin1String("genry_"));
}

// hats, glasses, things in the hands: drawn over the face
bool accessoryName(const QString& p)
{
    static const QRegularExpression re(QStringLiteral("^(?:glasses\\w*|panama|hat|cap|mask|gas_mask|red_nose|stethoscope|scarf|backpack|rose|"
                                                      "umbrella\\w*|headphones|bow|crown|flower|wreath|tattoo|hand\\w*|holds_out_hands\\w*|vedro)$"));
    return re.match(p).hasMatch();
}

QString distKey(const QString& d) { return d.isEmpty() ? QStringLiteral("normal") : d; }

int defaultWidth(const QString& dk)
{
    if (dk == QLatin1String("close")) return 1050;
    if (dk == QLatin1String("far")) return 630;
    return 900;
}

QRect alphaBox(const QImage& src, int step = 1)
{
    const QImage img = src.format() == QImage::Format_ARGB32 || src.format() == QImage::Format_ARGB32_Premultiplied
                           ? src
                           : src.convertToFormat(QImage::Format_ARGB32);
    int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < img.height(); y += step) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += step) {
            if (qAlpha(row[x]) < 24) continue;
            x0 = qMin(x0, x);
            x1 = qMax(x1, x);
            y0 = qMin(y0, y);
            y1 = qMax(y1, y);
        }
    }
    return x1 < 0 ? QRect() : QRect(QPoint(x0, y0), QPoint(x1, y1));
}

// ---- the figure test: is a «clothes» picture a whole second figure (its own head) next to the body?

struct Mask {                         // opaque pixels at half resolution
    int w = 0, h = 0, fullW = 0;
    std::vector<uint8_t> on;
};

Mask halfMask(const QImage& src)
{
    Mask m;
    if (src.isNull()) return m;
    const QImage img = src.format() == QImage::Format_ARGB32 || src.format() == QImage::Format_ARGB32_Premultiplied
                           ? src
                           : src.convertToFormat(QImage::Format_ARGB32);
    m.fullW = img.width();
    m.w = img.width() / 2;
    m.h = img.height() / 2;
    m.on.assign(size_t(m.w) * size_t(m.h), 0);
    for (int y = 0; y < m.h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y * 2));
        for (int x = 0; x < m.w; ++x) m.on[size_t(y) * m.w + x] = qAlpha(row[x * 2]) >= 24;
    }
    return m;
}

// every pixel within r (a square) of an opaque one
std::vector<uint8_t> dilate(const Mask& m, int r)
{
    std::vector<uint8_t> tmp(m.on.size()), out(m.on.size());
    std::vector<int> pre(size_t(qMax(m.w, m.h)) + 1);
    for (int y = 0; y < m.h; ++y) {
        for (int x = 0; x < m.w; ++x) pre[x + 1] = pre[x] + m.on[size_t(y) * m.w + x];
        for (int x = 0; x < m.w; ++x) tmp[size_t(y) * m.w + x] = pre[qMin(m.w, x + r + 1)] - pre[qMax(0, x - r)] > 0;
    }
    for (int x = 0; x < m.w; ++x) {
        for (int y = 0; y < m.h; ++y) pre[y + 1] = pre[y] + tmp[size_t(y) * m.w + x];
        for (int y = 0; y < m.h; ++y) out[size_t(y) * m.w + x] = pre[qMin(m.h, y + r + 1)] - pre[qMax(0, y - r)] > 0;
    }
    return out;
}

struct BodyShape {
    Mask mask;
    std::vector<uint8_t> near;        // within 16 px of the body
    int top = -1;                     // its first opaque row (half resolution)
};

BodyShape bodyShape(const QImage& img)
{
    BodyShape b;
    b.mask = halfMask(img);
    if (b.mask.on.empty()) return b;
    b.near = dilate(b.mask, 8);
    for (int y = 0; y < b.mask.h && b.top < 0; ++y)
        for (int x = 0; x < b.mask.w; ++x)
            if (b.mask.on[size_t(y) * b.mask.w + x]) { b.top = y; break; }
    return b;
}

// head:  its pixels in the body's head rows (the top 20%) far from the body - a second head - per body head pixel, ‰
// out:   its pixels far from the body anywhere, per its pixel, ‰
// torso: the median shift of its row centres against the body's (45-80% down), ‰ of the width
struct FigureTest {
    int head = -1, out = 0, torso = 0;
};

FigureTest figureTest(const Mask& l, const BodyShape& b)
{
    FigureTest t;
    if (l.on.empty() || b.top < 0) return t;
    const int w = qMin(l.w, b.mask.w), h = qMin(l.h, b.mask.h);
    const int band = qMin(h, b.top + int(0.2 * b.mask.h));
    qint64 headFar = 0, head = 0, far = 0, all = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t bi = size_t(y) * b.mask.w + x;
            const bool on = l.on[size_t(y) * l.w + x];
            const bool away = on && !b.near[bi];
            all += on;
            far += away;
            if (y >= b.top && y < band) {
                head += b.mask.on[bi];
                headFar += away;
            }
        }
    t.head = head ? int(1000 * headFar / head) : 0;
    t.out = all ? int(1000 * far / all) : 0;
    std::vector<int> d;
    for (int y = int(0.45 * h); y < int(0.8 * h); y += 2) {
        int lx0 = -1, lx1 = -1, bx0 = -1, bx1 = -1;
        for (int x = 0; x < w; ++x) {
            if (l.on[size_t(y) * l.w + x]) { if (lx0 < 0) lx0 = x; lx1 = x; }
            if (b.mask.on[size_t(y) * b.mask.w + x]) { if (bx0 < 0) bx0 = x; bx1 = x; }
        }
        if (lx0 >= 0 && bx0 >= 0) d.push_back((lx0 + lx1) - (bx0 + bx1));   // the centre shift in full pixels
    }
    if (d.size() >= 5) {
        std::nth_element(d.begin(), d.begin() + qsizetype(d.size() / 2), d.end());
        t.torso = int(1000LL * d[d.size() / 2] / qMax(1, l.fullW));
    }
    return t;
}

// the numbers above that make a picture a figure of its own: a second head beside the body's, and it stands
// apart (shifted) or not on the body at all. Clothes with hair or a hood stay clothes (they sit on the body).
bool isFigureTest(const FigureTest& t) { return t.head >= 120 && (qAbs(t.torso) >= 50 || t.out >= 600); }

// the grown-up heroines: Алиса, Славя, Лена, Мику, Женя, Ольга Дмитриевна, Виола, Юля. Everyone else (Ульяна first of
// all) wears the Steam build's own body in the wardrobe - never a workshop body, never the 18+ patch's
bool grownUp(const QString& tag)
{
    static const QSet<QString> grown{QStringLiteral("dv"), QStringLiteral("sl"), QStringLiteral("un"), QStringLiteral("mi"),
                                     QStringLiteral("mz"), QStringLiteral("mt"), QStringLiteral("cs"), QStringLiteral("uv")};
    return grown.contains(tag);
}

} // namespace

const Wardrobe* wardrobe() { return g_wardrobe.load(); }
void setWardrobe(const Wardrobe* w) { g_wardrobe.store(w); }

bool Wardrobe::isAdultPart(const QString& part)
{
    static const QRegularExpression re(QStringLiteral("nude|naked|topless|panties|pantsu|underwe|lingerie|towel|bdsm|succub|censor|rebelfak|"
                                                      "(?:^|_)(?:undress\\w*|nue|pan|bra|sheet|noshirt|open_shirt)(?:_|\\d|$)"));
    return re.match(part.toLower()).hasMatch();
}

bool Wardrobe::adultAllowed(const QString& tag)
{
    // both builds: the workshop's 18+ the user is subscribed to, for the grown-up heroines only
    return grownUp(tag);
}

QString Wardrobe::modFile(const WardrobeLayer& l)
{
    if (l.src.isEmpty()) return {};
    const auto m = layerRe().match(l.path);
    if (!m.hasMatch()) return {};
    return l.src + QLatin1Char('/') + m.captured(1).toLower() + QLatin1Char('/') + m.captured(2).toLower() + QLatin1Char('/') +
           QFileInfo(l.path).fileName().toLower();
}

void Wardrobe::build(const Library& lib, const EsAssets& es, const QString& cacheFile)
{
    m_lib = &lib;
    m_es = &es;
    auto layerKey = [](const QString& tag, const QString& dk, int pose, const QString& part) {
        return tag + QLatin1Char('|') + dk + QLatin1Char('|') + QString::number(pose) + QLatin1Char('|') + part;
    };
    auto add = [this, &layerKey](const QString& tag, const QString& dk, int pose, const QString& part, const WardrobeLayer& l) {
        QVector<WardrobeLayer>& v = m_layers[layerKey(tag, dk, pose, part)];
        for (const WardrobeLayer& x : v)
            if (x.src == l.src) return false;
        v.push_back(l);
        if (v.size() == 1) m_partsAt[tag + QLatin1Char('|') + dk + QLatin1Char('|') + QString::number(pose)] << part;
        return true;
    };
    auto gameHas = [this, &layerKey](const QString& tag, const QString& dk, int pose, const QString& part) {
        for (const WardrobeLayer& l : m_layers.value(layerKey(tag, dk, pose, part)))
            if (l.src.isEmpty()) return true;
        return false;
    };

    // ---- the game's own layers, from the ES catalog: their kind is known from the sprite names
    for (auto it = es.sprites.cbegin(); it != es.sprites.cend(); ++it) {
        m_esNames.insert(it.key());
        const QStringList words = it.key().split(QLatin1Char(' '));
        const QString emo = words.value(1);
        if (words.contains(QLatin1String("body"))) m_esBodyTags.insert(words[0]);
        const QString nameDist = words.size() > 2 && (words.last() == QLatin1String("close") || words.last() == QLatin1String("far")) ? words.last()
                                                                                                                                    : QStringLiteral("normal");
        if (!m_width.contains(words[0] + QLatin1Char('|') + nameDist)) m_width.insert(words[0] + QLatin1Char('|') + nameDist, it->w);
        for (int i = 0; i < it->layers.size(); ++i) {
            const EsLayer& el = it->layers[i];
            const auto m = layerRe().match(el.path);
            if (!m.hasMatch() || el.x || el.y) continue;
            const QString dk = m.captured(1).toLower(), tag = m.captured(2).toLower(), part = m.captured(5).toLower();
            if (m.captured(3).toLower() != tag || !usablePart(part)) continue;
            const int pose = m.captured(4).toInt();
            add(tag, dk, pose, part, {QString(), el.path});
            const QString tp = tag + QLatin1Char('|') + part;
            const Kind k = i == 0 || part == QLatin1String("body") ? Body : part == emo ? Face : accessoryName(part) ? Accessory : Clothes;
            if (!m_kind.contains(tp) || k == Face) m_kind.insert(tp, k);
            m_gameParts.insert(tp);
            if (k == Face && dk == QLatin1String("normal") && !m_esEmoPose.contains(tp)) m_esEmoPose.insert(tp, pose);
        }
    }

    // ---- the workshop: the modders' resource pack first, then the biggest mods
    QVector<LibItem> items = lib.items();
    std::stable_sort(items.begin(), items.end(), [](const LibItem& a, const LibItem& b) {
        return (a.id == QLatin1String("2519236508")) > (b.id == QLatin1String("2519236508"));
    });
    QHash<QString, QString> kindRef;      // "tag|part" -> the picture whose box decides its kind ("<id>/<vpath>")
    QHash<QString, QString> itemDir;
    for (const LibItem& item : items) {
        itemDir.insert(item.id, item.dir);
        if (item.id == QLatin1String(EsAssets::kHentaiPatchId)) continue;     // its bodies ARE the game's (Vfs)
        for (const QString& vp : item.images) {
            const auto m = layerRe().match(vp);
            if (!m.hasMatch()) continue;
            const QString dk = m.captured(1).toLower(), tag = m.captured(2).toLower(), part = m.captured(5).toLower();
            if (m.captured(3).toLower() != tag || !usablePart(part)) continue;
            // inside a mod's .rpa under images/: ES mounts it over its own files - the game path already is it
            const int arc = int(vp.indexOf(QLatin1String(".rpa/"), 0, Qt::CaseInsensitive));
            if (arc >= 0 && es.vfs().has(vp.mid(arc + 5))) continue;
            const int pose = m.captured(4).toInt();
            if (isAdultPart(part) && !adultAllowed(tag)) continue;
            // no 18+ for this character here (Ульяна, the boys, unknown characters): none of the workshop's bodies over
            // a pose the Steam game draws itself (the resource pack's old uncensored ones, «в белом платье и лифоне»…)
            // and no body worn as an outfit - the game's own body stays
            if (!adultAllowed(tag) && part.contains(QLatin1String("body")) &&
                (part != QLatin1String("body") || gameHas(tag, dk, pose, QStringLiteral("body"))))
                continue;
            // an empty «body» (a placeholder of some pack): a sprite on it would be a face in the air
            if (part.contains(QLatin1String("body")) && arc < 0 && QFileInfo(item.dir + QLatin1Char('/') + vp).size() < 4096) continue;
            const WardrobeLayer l{item.id, vp};
            const QString rel = modFile(l);
            if (rel.isEmpty() || m_relRef.contains(rel)) continue;
            if (!add(tag, dk, pose, part, l)) continue;
            m_relRef.insert(rel, item.id + QLatin1Char('/') + vp);
            m_tags.insert(tag);
            ++m_count;
            const QString tp = tag + QLatin1Char('|') + part;
            if (m_kind.contains(tp)) continue;
            if (part.contains(QLatin1String("body"))) { m_kind.insert(tp, Body); continue; }
            if (accessoryName(part)) { m_kind.insert(tp, Accessory); continue; }
            QString& ref = kindRef[tp];
            if (ref.isEmpty() || (dk == QLatin1String("normal") && !ref.contains(QLatin1String("/normal/")))) ref = item.id + QLatin1Char('/') + vp;
        }
    }
    auto stampOf = [&itemDir](const WardrobeLayer& l) -> QString {
        if (l.src.isEmpty()) return QStringLiteral("g");
        const int arc = int(l.path.indexOf(QLatin1String(".rpa/"), 0, Qt::CaseInsensitive));
        const QFileInfo fi(itemDir.value(l.src) + QLatin1Char('/') + (arc >= 0 ? l.path.left(arc + 4) : l.path));
        return QString::number(fi.size()) + QLatin1Char('_') + QString::number(fi.lastModified().toSecsSinceEpoch());
    };
    auto refLayer = [](const QString& ref) { return WardrobeLayer{ref.section(QLatin1Char('/'), 0, 0), ref.section(QLatin1Char('/'), 1)}; };
    auto boxOf = [](const QVector<int>& v) { return v[2] < 0 ? QRect() : QRect(QPoint(v[0], v[1]), QPoint(v[2], v[3])); };
    auto boxNums = [](const QRect& b, int last) {
        return b.isNull() ? QVector<int>{0, 0, -1, -1, last} : QVector<int>{b.left(), b.top(), b.right(), b.bottom(), last};
    };
    auto areaOf = [](const Mask& m) {
        int n = 0;
        for (const uint8_t x : m.on) n += x;
        return n * 4;
    };

    // ---- what the pictures look like (boxes, the figure test, faces), cached on disk: later starts decode only what changed
    QHash<QString, QString> cache, fresh;      // key -> "stamp\tnumbers"
    {
        QFile f(cacheFile);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            for (const QString& line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
                const QStringList c = line.split(QLatin1Char('\t'));
                if (c.size() == 3) cache.insert(c[0], c[1] + QLatin1Char('\t') + c[2]);
            }
    }
    auto cached = [&cache](const QString& key, const QString& stamp, int count) {
        QVector<int> v;
        const QString hit = cache.value(key);
        if (hit.section(QLatin1Char('\t'), 0, 0) != stamp) return v;
        const QStringList n = hit.section(QLatin1Char('\t'), 1).split(QLatin1Char(' '));
        if (n.size() != count) return v;
        for (const QString& s : n) v << s.toInt();
        return v;
    };
    auto keep = [&fresh](const QString& key, const QString& stamp, const QVector<int>& v) {
        QStringList n;
        for (const int x : v) n << QString::number(x);
        fresh.insert(key, stamp + QLatin1Char('\t') + n.join(QLatin1Char(' ')));
    };
    m_decoded = 0;

    // ---- (1) the game's own faces per pose: where a face belongs there (their box together) and how big one is
    struct PoseFaces {
        QStringList paths;
        QRect box;
        int area = 0;
    };
    QHash<QString, PoseFaces> poseFaces;      // "tag|dist|pose"
    for (auto it = m_partsAt.cbegin(); it != m_partsAt.cend(); ++it) {
        const QString tag = it.key().section(QLatin1Char('|'), 0, 0);
        PoseFaces pf;
        for (const QString& part : *it)
            if (kind(tag, part) == Face)
                for (const WardrobeLayer& l : m_layers.value(it.key() + QLatin1Char('|') + part))
                    if (l.src.isEmpty()) pf.paths << l.path;
        if (pf.paths.size() < 3) continue;
        pf.paths.sort();
        poseFaces.insert(it.key(), pf);
    }
    auto measure = [&es, &areaOf](PoseFaces& pf) {
        QVector<int> areas;
        for (const QString& p : pf.paths) {
            const QImage img = QImage::fromData(es.vfs().read(p));
            const Mask m = halfMask(img);
            if (m.on.empty()) continue;
            const QRect b = alphaBox(img, 2);
            if (b.isNull()) continue;
            pf.box = pf.box.united(b);
            areas << areaOf(m);
        }
        std::sort(areas.begin(), areas.end());
        pf.area = areas.isEmpty() ? 0 : areas[areas.size() / 2];
    };
    {
        QVector<QString> missKeys;
        QVector<PoseFaces> miss;
        for (auto it = poseFaces.begin(); it != poseFaces.end(); ++it) {
            const QVector<int> v = cached(QStringLiteral("G:") + it.key(), QStringLiteral("g%1").arg(it->paths.size()), 5);
            if (v.isEmpty()) { missKeys << it.key(); miss << *it; continue; }
            it->box = boxOf(v);
            it->area = v[4];
        }
        QtConcurrent::blockingMap(miss, [&measure](PoseFaces& pf) { measure(pf); });
        for (int i = 0; i < miss.size(); ++i) poseFaces[missKeys[i]] = miss[i];
        for (auto it = poseFaces.cbegin(); it != poseFaces.cend(); ++it)
            keep(QStringLiteral("G:") + it.key(), QStringLiteral("g%1").arg(it->paths.size()), boxNums(it->box, it->area));
    }

    // ---- (2) faces vs clothes: where the picture is opaque - a face sits where the game's faces are (or high and small)
    struct KindJob {
        QString tp, ref, stamp;
        QRect box;
        int h = 0;
    };
    QVector<KindJob> kindJobs, kindMiss;
    for (auto it = kindRef.cbegin(); it != kindRef.cend(); ++it) {
        KindJob j{it.key(), *it, stampOf(refLayer(*it)), {}, 0};
        const QVector<int> v = cached(QStringLiteral("K:") + j.ref, j.stamp, 5);
        if (v.isEmpty()) { kindMiss << j; continue; }
        j.box = boxOf(v);
        j.h = v[4];
        kindJobs << j;
    }
    m_decoded += int(kindMiss.size());
    QtConcurrent::blockingMap(kindMiss, [&lib](KindJob& j) {
        const QImage img = QImage::fromData(lib.read(j.ref));
        j.h = img.height();
        j.box = img.isNull() ? QRect() : alphaBox(img, 2);
    });
    kindJobs += kindMiss;
    QHash<QString, KindJob> kindOf;           // "tag|part" -> its picture
    for (const KindJob& j : kindJobs) {
        keep(QStringLiteral("K:") + j.ref, j.stamp, boxNums(j.box, j.h));
        if (j.h <= 0) continue;                         // unreadable: never offered
        // an empty picture is an outfit too: the resource pack's «nude» = no clothes over its own body.
        // A face is at most 40% tall (7ДЛ's «ai_*» are whole heads with the hair: faces too) and lies where the game's
        // faces of that pose are (Ульяна is short: her face is lower than anyone's) - or, at a pose of the workshop, high
        bool face = !j.box.isNull() && j.box.height() < 0.4 * j.h;
        if (face) {
            const auto m = layerRe().match(refLayer(j.ref).path);
            const auto pf = m.hasMatch() ? poseFaces.constFind(m.captured(2).toLower() + QLatin1Char('|') + m.captured(1).toLower() + QLatin1Char('|') +
                                                               QString::number(m.captured(4).toInt()))
                                         : poseFaces.constEnd();
            if (pf != poseFaces.constEnd() && !pf->box.isNull()) {
                const int mx = int(0.05 * pf->box.width() + 0.03 * 900), my = int(0.05 * j.h);
                face = pf->box.adjusted(-mx, -my, mx, my).contains(j.box) || j.box.bottom() < 0.55 * j.h;
            } else
                face = j.box.bottom() < 0.55 * j.h;
        }
        m_kind.insert(j.tp, face ? Face : Clothes);
        kindOf.insert(j.tp, j);
    }

    // ---- (3) «clothes» that are a whole second figure (Лена «boy», Мику «camisole_far»): worn alone, no body under them;
    struct FigJob {
        QString tp, ref, stamp, poseKey;
        WardrobeLayer body;
        FigureTest t;
    };
    QVector<FigJob> figJobs, figMiss;
    for (auto it = kindOf.cbegin(); it != kindOf.cend(); ++it) {
        const KindJob& j = *it;
        if (m_kind.value(j.tp) != Clothes || j.box.isNull() || j.box.top() >= 0.5 * j.h) continue;
        const WardrobeLayer l = refLayer(j.ref);
        const auto m = layerRe().match(l.path);
        if (!m.hasMatch()) continue;
        const QString poseKey = m.captured(2).toLower() + QLatin1Char('|') + m.captured(1).toLower() + QLatin1Char('|') + QString::number(m.captured(4).toInt());
        const QVector<WardrobeLayer> bodies = m_layers.value(poseKey + QStringLiteral("|body"));
        if (bodies.isEmpty()) continue;
        FigJob f{j.tp, j.ref, {}, poseKey, bodies.first(), {}};
        for (const WardrobeLayer& b : bodies)
            if (b.src == l.src) { f.body = b; break; }
        f.stamp = j.stamp + QLatin1Char('|') + f.body.src + QLatin1Char('/') + f.body.path;
        const QVector<int> v = cached(QStringLiteral("X:") + f.ref, f.stamp, 3);
        if (v.isEmpty()) { figMiss << f; continue; }
        f.t.head = v[0];
        f.t.out = v[1];
        f.t.torso = v[2];
        figJobs << f;
    }
    if (!figMiss.isEmpty()) {
        // the bodies the new pictures are measured against
        QHash<QString, int> bodyIdx;
        QVector<QPair<WardrobeLayer, BodyShape>> shapes;
        for (const FigJob& f : figMiss) {
            const QString bk = f.body.src + QLatin1Char('/') + f.body.path;
            if (bodyIdx.contains(bk)) continue;
            bodyIdx.insert(bk, int(shapes.size()));
            shapes.push_back({f.body, {}});
        }
        QtConcurrent::blockingMap(shapes, [this](QPair<WardrobeLayer, BodyShape>& s) { s.second = bodyShape(QImage::fromData(read(s.first))); });
        const QVector<QPair<WardrobeLayer, BodyShape>>& cs = shapes;
        const QHash<QString, int>& bi = bodyIdx;
        QtConcurrent::blockingMap(figMiss, [&lib, &cs, &bi](FigJob& f) {
            f.t = figureTest(halfMask(QImage::fromData(lib.read(f.ref))), cs[bi.value(f.body.src + QLatin1Char('/') + f.body.path)].second);
        });
        m_decoded += int(figMiss.size());
        figJobs += figMiss;
    }
    for (const FigJob& f : figJobs) {
        keep(QStringLiteral("X:") + f.ref, f.stamp, {f.t.head, f.t.out, f.t.torso});
        if (isFigureTest(f.t)) m_figures.insert(f.tp);
    }

    // ---- (4) the workshop's faces: a piece of one (a blush, closed eyes, the «*_new» mouths) is no emotion - the
    //      sprite would have no face - and neither is a face hanging off the head (it misses where the game's faces are)
    struct FaceJob {
        QString key, ref, stamp, poseKey;
        QRect box;
        int area = 0;
    };
    QVector<FaceJob> faceJobs, faceMiss;
    for (auto it = m_layers.cbegin(); it != m_layers.cend(); ++it) {
        const QString tag = it.key().section(QLatin1Char('|'), 0, 0), part = it.key().section(QLatin1Char('|'), 3);
        if (kind(tag, part) != Face) continue;
        for (const WardrobeLayer& l : *it) {
            if (l.src.isEmpty()) continue;
            FaceJob f{it.key() + QLatin1Char('|') + l.src, l.src + QLatin1Char('/') + l.path, stampOf(l), it.key().section(QLatin1Char('|'), 0, 2), {}, 0};
            const QVector<int> v = cached(QStringLiteral("F:") + f.ref, f.stamp, 5);
            if (v.isEmpty()) { faceMiss << f; continue; }
            f.box = boxOf(v);
            f.area = v[4];
            faceJobs << f;
        }
    }
    QtConcurrent::blockingMap(faceMiss, [&lib, &areaOf](FaceJob& f) {
        const QImage img = QImage::fromData(lib.read(f.ref));
        if (img.isNull()) return;
        f.box = alphaBox(img, 2);
        f.area = areaOf(halfMask(img));
    });
    m_decoded += int(faceMiss.size());
    faceJobs += faceMiss;
    QHash<QString, QVector<int>> wsAreas;     // poses the game does not draw: how big the workshop's faces there are
    for (const FaceJob& f : faceJobs) {
        keep(QStringLiteral("F:") + f.ref, f.stamp, boxNums(f.box, f.area));
        if (!poseFaces.contains(f.poseKey) && f.area > 0) wsAreas[f.poseKey] << f.area;
    }
    for (QVector<int>& v : wsAreas) std::sort(v.begin(), v.end());
    for (const FaceJob& f : faceJobs) {
        const auto pf = poseFaces.constFind(f.poseKey);
        if (pf != poseFaces.constEnd() && pf->area > 0) {
            if (f.box.isNull() || f.area * 4 < pf->area || !f.box.intersects(pf->box)) m_badFace.insert(f.key);
            continue;
        }
        const QVector<int> v = wsAreas.value(f.poseKey);
        if (v.size() >= 5 && f.area * 4 < v[v.size() / 2]) m_badFace.insert(f.key);
    }

    {
        QDir().mkpath(QFileInfo(cacheFile).absolutePath());
        QFile f(cacheFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QString out;
            for (auto it = fresh.cbegin(); it != fresh.cend(); ++it) out += it.key() + QLatin1Char('\t') + *it + QLatin1Char('\n');
            f.write(out.toUtf8());
        }
    }

    // ---- poses per character and distance
    for (auto it = m_partsAt.cbegin(); it != m_partsAt.cend(); ++it) {
        const QStringList k = it.key().split(QLatin1Char('|'));
        QVector<int>& v = m_poses[k[0] + QLatin1Char('|') + k[1]];
        if (!v.contains(k[2].toInt())) v << k[2].toInt();
    }
    for (QVector<int>& v : m_poses) std::sort(v.begin(), v.end());
    m_ready = true;
}

bool Wardrobe::resolve(const QString& image, WardrobeLook* look, QString* why) const
{
    if (!m_ready) return false;
    const QString name = image.simplified();
    if (m_esNames.contains(name)) return false;
    QStringList w = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (w.size() < 2) return false;
    const QString tag = w.takeFirst();
    QString dist;
    if (w.last() == QLatin1String("close") || w.last() == QLatin1String("far")) dist = w.takeLast();
    if (w.isEmpty()) return false;
    const QString dk = distKey(dist);
    QVector<int> kinds;
    int faces = 0, clothes = 0;
    bool hasBody = false, adult = false, figure = false;
    QString alone;                     // a figure / clothes with a face of their own: worn without an emotion
    for (int i = 0; i < w.size(); ++i) {
        if (w.indexOf(w[i]) != i) {
            if (why) *why = U("«%1» два раза").arg(w[i]);
            return false;
        }
        const int k = m_kind.value(tag + QLatin1Char('|') + w[i], None);
        if (k == None) {
            if (why) *why = U("в гардеробе нет «%1»").arg(w[i]);
            return false;
        }
        kinds << k;
        faces += k == Face;
        clothes += k == Clothes;
        hasBody = hasBody || k == Body;
        adult = adult || isAdultPart(w[i]) || (k == Body && w[i] != QLatin1String("body"));
        const QString tp = tag + QLatin1Char('|') + w[i];
        if (k == Clothes && (m_figures.contains(tp) || m_ownFace.contains(tp))) {
            alone = w[i];
            figure = m_figures.contains(tp);
        }
    }
    if (!alone.isEmpty() && w.size() > 1) {
        if (why) *why = U("«%1» — цельная картинка со своим лицом, эмоцию и другую одежду на неё не надеть: пиши просто %2 %1").arg(alone, tag);
        return false;
    }
    if (!faces && alone.isEmpty()) {
        if (why) *why = U("нужна эмоция (smile, normal, sad…) — без неё у спрайта не будет лица");
        return false;
    }
    if (hasBody && !clothes) {
        if (!adultAllowed(tag)) {
            if (why) *why = U("без одежды этого персонажа в гардеробе нет");
            return false;
        }
        adult = true;
    }
    QStringList faceWords, outfitWords;
    for (int i = 0; i < w.size(); ++i) (kinds[i] == Face ? faceWords : outfitWords) << w[i];
    const QString faceW = faceWords.join(QLatin1Char(' ')), outfitW = outfitWords.join(QLatin1Char(' '));
    if (isHidden(tag, faceW, outfitW)) {
        if (why) *why = U("ты удалил это из гардероба (вернуть: «Удалённые» в Персонажах)");
        return false;
    }
    const QPoint shift = faceShift(tag, dk, faceW, outfitW);
    auto at = [&](int pose, const QString& part) {
        return m_layers.value(tag + QLatin1Char('|') + dk + QLatin1Char('|') + QString::number(pose) + QLatin1Char('|') + part);
    };
    // the picture of a part at a pose: the worn mod's own first (its body and faces fit its clothes); never a face
    // that is only a piece of one or hangs off the head - unless the user moved it there himself
    auto choose = [&](int pose, const QString& part, const QString& src, int k) {
        const QVector<WardrobeLayer> c = at(pose, part);
        const QString base = tag + QLatin1Char('|') + dk + QLatin1Char('|') + QString::number(pose) + QLatin1Char('|') + part + QLatin1Char('|');
        auto fits = [&](const WardrobeLayer& l) { return k != Face || !shift.isNull() || !m_badFace.contains(base + l.src); };
        for (const WardrobeLayer& l : c)
            if (!src.isEmpty() && l.src == src && fits(l)) return l;
        for (const WardrobeLayer& l : c)
            if (fits(l)) return l;
        return WardrobeLayer{};
    };
    auto modSrcAt = [&](int pose) {
        for (int i = 0; i < w.size(); ++i)
            if (kinds[i] == Clothes || kinds[i] == Body) {
                const QVector<WardrobeLayer> c = at(pose, w[i]);
                if (!c.isEmpty()) return c.first().src;
            }
        return QString();
    };
    int best = -1, bestScore = INT_MIN;
    for (int p : m_poses.value(tag + QLatin1Char('|') + dk)) {
        int score = -p;
        bool ok = true;
        const QString src = modSrcAt(p);
        for (int i = 0; i < w.size() && ok; ++i) {
            const WardrobeLayer l = choose(p, w[i], src, kinds[i]);
            if (l.path.isEmpty()) { ok = false; break; }
            if (l.src.isEmpty()) score += 10;
            if (kinds[i] == Face && m_esEmoPose.value(tag + QLatin1Char('|') + w[i], -1) == p) score += 1000;
        }
        if (!ok || (!hasBody && !figure && at(p, QStringLiteral("body")).isEmpty())) continue;
        if (score > bestScore) { bestScore = score; best = p; }
    }
    if (best < 0) {
        if (why) {
            const QString bare = tag + QLatin1Char(' ') + w.join(QLatin1Char(' '));
            if (!dist.isEmpty() && resolve(bare)) *why = U("у этих частей нет варианта «%1»").arg(dist == QLatin1String("close") ? U("близко") : U("далеко"));
            else if (dist.isEmpty() && resolve(bare + QStringLiteral(" close"))) *why = U("это нарисовано только крупно — допиши close");
            else if (dist.isEmpty() && resolve(bare + QStringLiteral(" far"))) *why = U("это нарисовано только издалека — допиши far");
            else *why = U("эмоция и одежда нарисованы для разных поз (или лицо кривое) — выбери эмоцию из списка этой одежды");
        }
        return false;
    }
    if (!look) return true;

    const QString modSrc = modSrcAt(best);
    look->tag = tag;
    look->dist = dist;
    look->pose = best;
    look->w = m_width.value(tag + QLatin1Char('|') + dk, defaultWidth(dk));
    look->h = 1080;
    look->layers.clear();
    look->kinds.clear();
    if (!hasBody && !figure) {
        look->layers << choose(best, QStringLiteral("body"), modSrc, Body);
        look->kinds << Body;
    }
    for (const int order : {int(Body), int(Clothes), int(Face), int(Accessory)})
        for (int i = 0; i < w.size(); ++i)
            if (kinds[i] == order) {
                look->layers << choose(best, w[i], modSrc, order);
                look->kinds << order;
            }
    // Ульяна and the rest: the Steam build's own body, copied into the mod - the 18+ patch swaps the game's bodies.
    // The grown-up heroines: the 18+ patch's body (GenryBL's own copy or the subscribed one) goes into the mod too,
    // so every player sees what the preview shows, with or without the patch
    for (int i = 0; i < look->layers.size(); ++i) {
        WardrobeLayer& l = look->layers[i];
        if (look->kinds[i] != Body || !l.src.isEmpty()) continue;
        if (!grownUp(tag)) l.src = QStringLiteral("base");
        else if (m_es && m_es->vfs().sourceOf(l.path) == QLatin1String(EsAssets::kHentaiPatchId)) l.src = QStringLiteral("patch");
    }
    look->faceShift = shift;
    look->adult = adult;
    look->workshop = false;
    for (const WardrobeLayer& l : look->layers) look->workshop = look->workshop || !l.src.isEmpty();
    return true;
}

QByteArray Wardrobe::read(const WardrobeLayer& l) const
{
    if (l.src == QLatin1String("base")) return m_es ? m_es->vfs().readBase(l.path) : QByteArray();
    if (l.src.isEmpty() || l.src == QLatin1String("patch")) return m_es ? m_es->vfs().read(l.path) : QByteArray();
    return m_lib ? m_lib->read(l.src + QLatin1Char('/') + l.path) : QByteArray();
}

QByteArray Wardrobe::readModFile(const QString& rel) const
{
    // base/<dist>/<tag>/<file>: the Steam build's own picture (not the 18+ patch's)
    if (rel.startsWith(QLatin1String("base/")))
        return m_es ? m_es->vfs().readBase(QStringLiteral("images/sprites/") + rel.mid(5)) : QByteArray();
    // patch/<dist>/<tag>/<file>: the 18+ patch's body of a grown-up heroine
    if (rel.startsWith(QLatin1String("patch/")))
        return m_es ? m_es->vfs().read(QStringLiteral("images/sprites/") + rel.mid(6)) : QByteArray();
    const QString ref = m_relRef.value(rel);
    return ref.isEmpty() || !m_lib ? QByteArray() : m_lib->read(ref);
}

QImage Wardrobe::compose(const QString& image, const QPoint* forcedShift) const
{
    WardrobeLook l;
    if (!resolve(image, &l)) return {};
    if (forcedShift) l.faceShift = *forcedShift;
    QImage out(l.w, l.h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    for (int i = 0; i < l.layers.size(); ++i) {
        const QImage img = QImage::fromData(read(l.layers[i]));
        if (!img.isNull()) p.drawImage(l.kinds[i] == Face ? l.faceShift : QPoint(), img);
    }
    return out;
}

QRect Wardrobe::faceBox(const QString& image) const
{
    WardrobeLook l;
    if (!resolve(image, &l)) return {};
    QRect box;
    for (int i = 0; i < l.layers.size(); ++i)
        if (l.kinds[i] == Face) box = box.united(alphaBox(QImage::fromData(read(l.layers[i]))));
    return box.isNull() ? box : box.translated(l.faceShift);
}

void Wardrobe::setFaceShifts(const QHash<QString, QPoint>& shifts)
{
    QWriteLocker lock(&m_hideLock);
    m_shifts = shifts;
}

QPoint Wardrobe::faceShift(const QString& tag, const QString& dist, const QString& face, const QString& outfit, QString* scope) const
{
    QReadLocker lock(&m_hideLock);
    const QString t = tag + QLatin1Char('|') + distKey(dist == QLatin1String("normal") ? QString() : dist) + QLatin1Char('|');
    const QString look = t + QStringLiteral("look:") + (face + QLatin1Char(' ') + outfit).trimmed();
    if (m_shifts.contains(look)) { if (scope) *scope = QStringLiteral("look"); return m_shifts.value(look); }
    if (!outfit.isEmpty() && m_shifts.contains(t + QStringLiteral("outfit:") + outfit)) {
        if (scope) *scope = QStringLiteral("outfit");
        return m_shifts.value(t + QStringLiteral("outfit:") + outfit);
    }
    if (m_shifts.contains(t + QStringLiteral("face:") + face)) { if (scope) *scope = QStringLiteral("face"); return m_shifts.value(t + QStringLiteral("face:") + face); }
    if (scope) scope->clear();
    return {};
}

void Wardrobe::setHidden(const QSet<QString>& keys)
{
    QWriteLocker lock(&m_hideLock);
    m_hidden = keys;
}

bool Wardrobe::isHidden(const QString& tag, const QString& face, const QString& outfit) const
{
    QReadLocker lock(&m_hideLock);
    if (m_hidden.isEmpty()) return false;
    const QString t = tag + QLatin1Char('|');
    if (m_hidden.contains(t + QStringLiteral("look:") + (face + QLatin1Char(' ') + outfit).trimmed())) return true;
    for (const QString& f : face.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (m_hidden.contains(t + QStringLiteral("face:") + f)) return true;
    if (!outfit.isEmpty() && m_hidden.contains(t + QStringLiteral("outfit:") + outfit)) return true;
    for (const QString& o : outfit.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (m_hidden.contains(t + QStringLiteral("outfit:") + o)) return true;
    return false;
}

QString Wardrobe::definition(const QString& image, const QString& modId) const
{
    WardrobeLook l;
    if (!resolve(image, &l)) return {};
    QString comp = QStringLiteral("im.Composite((%1,%2)").arg(l.w).arg(l.h);
    for (int i = 0; i < l.layers.size(); ++i) {
        const WardrobeLayer& layer = l.layers[i];
        const QPoint at = l.kinds[i] == Face ? l.faceShift : QPoint();
        comp += QStringLiteral(", (%1,%2), \"%3\"").arg(at.x()).arg(at.y())
                    .arg(layer.src.isEmpty() ? layer.path : QStringLiteral("mods/%1/genry/wardrobe/%2").arg(modId, modFile(layer)));
    }
    comp += QLatin1Char(')');
    // exactly how ES's sprites.rpy darkens its sprites in the evening and at night
    return QStringLiteral("image %1 = ConditionSwitch(\"persistent.sprite_time=='sunset'\", im.MatrixColor(%2, im.matrix.tint(0.94, 0.82, 1.0)), "
                          "\"persistent.sprite_time=='night'\", im.MatrixColor(%2, im.matrix.tint(0.63, 0.78, 0.82)), True, %2)")
        .arg(image.simplified(), comp);
}

QVector<Wardrobe::Outfit> Wardrobe::outfits(const QString& tag) const
{
    QHash<QString, Outfit> by;
    for (const char* d : {"normal", "close", "far"}) {
        const QString dk = QString::fromLatin1(d);
        const QString base = tag + QLatin1Char('|') + dk + QLatin1Char('|');
        for (int p : m_poses.value(tag + QLatin1Char('|') + dk)) {
            const QStringList parts = m_partsAt.value(base + QString::number(p));
            bool face = false;
            for (const QString& x : parts) face = face || kind(tag, x) == Face;
            for (const QString& x : parts) {
                const Kind k = kind(tag, x);
                if ((k != Clothes && k != Body) || m_gameParts.contains(tag + QLatin1Char('|') + x) || x == QLatin1String("body")) continue;
                const bool alone = m_figures.contains(tag + QLatin1Char('|') + x) || m_ownFace.contains(tag + QLatin1Char('|') + x);
                if (!face && !alone) continue;
                if (isHidden(tag, QString(), x)) continue;
                Outfit& o = by[x];
                if (o.part.isEmpty()) {
                    o.part = x;
                    o.adult = isAdultPart(x) || k == Body;     // a workshop body can be a bare one
                    o.body = k == Body;
                    o.figure = alone;
                    o.source = m_layers.value(base + QString::number(p) + QLatin1Char('|') + x).value(0).src;
                }
                const QString dist = dk == QLatin1String("normal") ? QString() : dk;
                if (!o.dists.contains(dist)) o.dists << dist;
                if (dk == QLatin1String("normal") || o.dists.size() == 1) ++o.poses;
            }
        }
    }
    // the bare body of the grown-up heroines (the 18+ patch's, when it is subscribed), where ES has none of its own
    if (adultAllowed(tag) && !m_esBodyTags.contains(tag) && !isHidden(tag, QString(), QStringLiteral("body"))) {
        Outfit o;
        o.part = QStringLiteral("body");
        o.adult = true;
        o.body = true;
        for (const char* d : {"normal", "close", "far"}) {
            const QString dk = QString::fromLatin1(d);
            for (int p : m_poses.value(tag + QLatin1Char('|') + dk)) {
                bool game = false;
                for (const WardrobeLayer& l : m_layers.value(tag + QLatin1Char('|') + dk + QLatin1Char('|') + QString::number(p) + QStringLiteral("|body")))
                    game = game || l.src.isEmpty();
                if (!game) continue;
                const QString dist = dk == QLatin1String("normal") ? QString() : dk;
                if (!o.dists.contains(dist)) o.dists << dist;
                if (dk == QLatin1String("normal")) ++o.poses;
            }
        }
        if (!o.dists.isEmpty()) by.insert(o.part, o);
    }
    QVector<Outfit> out;
    for (const Outfit& o : by) out << o;
    std::sort(out.begin(), out.end(), [](const Outfit& a, const Outfit& b) {
        if (a.adult != b.adult) return b.adult;
        return a.part < b.part;
    });
    return out;
}

QStringList Wardrobe::looks(const QString& tag, const QString& outfit, const QString& dist) const
{
    // a figure / clothes with a face of their own: the one look is the picture itself
    if (m_figures.contains(tag + QLatin1Char('|') + outfit) || m_ownFace.contains(tag + QLatin1Char('|') + outfit))
        return resolve(tag + QLatin1Char(' ') + outfit + (dist.isEmpty() ? QString() : QLatin1Char(' ') + dist)) ? QStringList{outfit} : QStringList{};
    QStringList game, other;
    QSet<QString> seen;
    const QString dk = distKey(dist);
    const QString base = tag + QLatin1Char('|') + dk + QLatin1Char('|');
    for (int p : m_poses.value(tag + QLatin1Char('|') + dk)) {
        if (m_layers.value(base + QString::number(p) + QLatin1Char('|') + outfit).isEmpty()) continue;
        for (const QString& x : m_partsAt.value(base + QString::number(p))) {
            if (kind(tag, x) != Face) continue;
            const QString n = x + QLatin1Char(' ') + outfit;
            if (seen.contains(n)) continue;
            seen.insert(n);
            if (!resolve(tag + QLatin1Char(' ') + n + (dist.isEmpty() ? QString() : QLatin1Char(' ') + dist))) continue;
            (m_gameParts.contains(tag + QLatin1Char('|') + x) ? game : other) << n;
        }
    }
    game.sort();
    other.sort();
    return game + other;
}

} // namespace gb
