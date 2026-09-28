#include "EsAssets.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace gb {

bool EsAssets::load(const QString& catalogJson, const QString& esRoot, QString* error)
{
    QFile f(catalogJson);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("нет каталога БЛ: %1").arg(catalogJson);
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject sp = root[QStringLiteral("sprites")].toObject();
    for (auto it = sp.begin(); it != sp.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        EsSprite s;
        s.w = o[QStringLiteral("w")].toInt(900);
        s.h = o[QStringLiteral("h")].toInt(1080);
        for (const QJsonValue& l : o[QStringLiteral("layers")].toArray()) {
            const QJsonArray a = l.toArray();
            s.layers.push_back({a.at(0).toInt(), a.at(1).toInt(), a.at(2).toString()});
        }
        const QJsonArray t = o[QStringLiteral("tint")].toArray();
        if (t.size() == 3) {
            s.hasTint = true;
            for (int i = 0; i < 3; ++i) s.tint[i] = t.at(i).toDouble();
        }
        s.opacity = o[QStringLiteral("opacity")].toDouble(1.0);
        sprites.insert(it.key(), s);
    }
    const QJsonObject im = root[QStringLiteral("images")].toObject();
    for (auto it = im.begin(); it != im.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        EsImage i;
        i.path = o[QStringLiteral("path")].toString();
        i.color = o[QStringLiteral("color")].toString();
        i.sepia = o[QStringLiteral("sepia")].toBool();
        const QJsonArray t = o[QStringLiteral("tint")].toArray();
        if (t.size() == 3) {
            i.hasTint = true;
            for (int k = 0; k < 3; ++k) i.tint[k] = t.at(k).toDouble();
        }
        images.insert(it.key(), i);
    }
    auto strMap = [&](const char* key, QMap<QString, QString>& dst) {
        const QJsonObject o = root[QLatin1String(key)].toObject();
        for (auto it = o.begin(); it != o.end(); ++it) dst.insert(it.key(), it.value().toString());
    };
    strMap("music", music);
    strMap("sounds", sounds);
    strMap("ambience", ambience);
    const QJsonObject ch = root[QStringLiteral("characters")].toObject();
    for (auto it = ch.begin(); it != ch.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        EsCharacter c;
        c.id = it.key();
        c.name = o[QStringLiteral("name")].toString();
        const QJsonObject cols = o[QStringLiteral("colors")].toObject();
        for (auto k = cols.begin(); k != cols.end(); ++k) c.colors.insert(k.key(), k.value().toString());
        characters.insert(c.id, c);
    }
    m_root = esRoot;
    QString err;
    if (!m_vfs.mount(esRoot, &err)) {
        if (error) *error = err;
        return false;
    }
    // The Steam build ships without some pictures its scripts still declare (the 18+ CGs):
    // anything whose file is not really there is dropped, so it is never offered or previewed.
    for (auto it = images.begin(); it != images.end();) {
        if (!it->path.isEmpty() && !m_vfs.has(it->path)) { missingImages.insert(it.key()); it = images.erase(it); }
        else ++it;
    }
    for (auto it = sprites.begin(); it != sprites.end();) {
        bool ok = true;
        for (const EsLayer& l : it->layers) if (!m_vfs.has(l.path)) { ok = false; break; }
        if (!ok) { missingImages.insert(it.key()); it = sprites.erase(it); }
        else ++it;
    }
    // same for music_list / sfx_* / ambience_* ids whose .ogg the Steam build lacks
    // (doubt_everyone, pioneer2_loudspeaker_tape): "play music" of them fails in game
    auto dropAudio = [&](QMap<QString, QString>& m) {
        for (auto it = m.begin(); it != m.end();) {
            if (!m_vfs.has(it.value())) { missingAudio.insert(it.key()); it = m.erase(it); }
            else ++it;
        }
    };
    dropAudio(music);
    dropAudio(sounds);
    dropAudio(ambience);
    // the patch's pictures the Steam scripts do not declare: there when the patch is subscribed
    for (const EsPatchImage& p : esPatchImages()) {
        if (p.declared || images.contains(p.name)) continue;
        if (m_vfs.has(p.path)) {
            EsImage i;
            i.path = p.path;
            images.insert(p.name, i);
        } else {
            missingImages.insert(p.name);
        }
    }
    // what the workshop really changes (the 18+ patch: its CGs + the old character bodies)
    for (auto it = images.cbegin(); it != images.cend(); ++it) {
        const QString src = it->path.isEmpty() ? QString() : m_vfs.sourceOf(it->path);
        if (!src.isEmpty()) m_imageSource.insert(it.key(), src);
    }
    for (auto it = sprites.cbegin(); it != sprites.cend(); ++it)
        for (const EsLayer& l : it->layers) {
            const QString src = m_vfs.sourceOf(l.path);
            if (!src.isEmpty()) { m_imageSource.insert(it.key(), src); break; }
        }
    m_ready = true;
    return true;
}

const QVector<EsPatchImage>& esPatchImages()
{
    static const QVector<EsPatchImage> list = [] {
        QVector<EsPatchImage> v;
        for (const char* n : {"d2_mt_undressed", "d2_mt_undressed_2", "d2_sl_swim", "d3_sl_bathhouse", "d5_dv_us_wash", "d5_dv_us_wash_2",
                              "d5_dv_us_wash_3", "d5_dv_us_wash_4", "d6_dv_hentai", "d6_dv_hentai_2", "d6_sl_swim", "d6_sl_hentai_1",
                              "d6_sl_hentai_2", "d7_sl_morning", "d7_sl_morning_2", "d7_un_hentai", "d7_un_hentai_3", "miku_h_1_cenz",
                              "miku_h_2_cenz", "uvao_h_cenz"})
            v.push_back({QStringLiteral("cg ") + QLatin1String(n), QStringLiteral("images/cg/%1.jpg").arg(QLatin1String(n)), true});
        for (const char* n : {"miku_h_1", "miku_h_2", "uvao_h"})
            v.push_back({QStringLiteral("cg ") + QLatin1String(n), QStringLiteral("images/cg/%1.jpg").arg(QLatin1String(n)), false});
        for (const char* n : {"12_2ch_old", "12_uvao_old", "1_2ch_old"})
            v.push_back({QStringLiteral("cg card_") + QLatin1String(n), QStringLiteral("images/cards/%1.png").arg(QLatin1String(n)), false});
        return v;
    }();
    return list;
}

const EsPatchImage* esPatchImage(const QString& name)
{
    for (const EsPatchImage& p : esPatchImages())
        if (p.name == name) return &p;
    return nullptr;
}

bool esPatchInFolder(const EsPatchImage& p) { return !p.name.contains(QLatin1String("_us_")); }

bool EsAssets::hentaiPatch() const
{
    return m_vfs.workshopItems().contains(QLatin1String(kHentaiPatchId)) || m_vfs.bundledMounted();
}

QString EsAssets::imageSource(const QString& name) const
{
    return m_imageSource.value(name);
}

QStringList EsAssets::spriteTags() const
{
    static const QStringList order{QStringLiteral("dv"), QStringLiteral("sl"), QStringLiteral("un"), QStringLiteral("us"), QStringLiteral("mi"),
                                   QStringLiteral("mt"), QStringLiteral("el"), QStringLiteral("sh"), QStringLiteral("mz"), QStringLiteral("uv"),
                                   QStringLiteral("cs"), QStringLiteral("pi")};
    QStringList tags;
    for (auto it = sprites.begin(); it != sprites.end(); ++it) {
        const QString t = it.key().section(QLatin1Char(' '), 0, 0);
        if (!tags.contains(t)) tags << t;
    }
    std::sort(tags.begin(), tags.end(), [&](const QString& a, const QString& b) {
        const int ia = int(order.indexOf(a)), ib = int(order.indexOf(b));
        if (ia != ib) return (ia < 0 ? 99 : ia) < (ib < 0 ? 99 : ib);
        return a < b;
    });
    return tags;
}

QStringList EsAssets::spriteNames(const QString& tag) const
{
    QStringList out;
    const QString prefix = tag + QLatin1Char(' ');
    for (auto it = sprites.begin(); it != sprites.end(); ++it) {
        const QString& n = it.key();
        if (!n.startsWith(prefix) || n.endsWith(QLatin1String(" close")) || n.endsWith(QLatin1String(" far"))) continue;
        out << n.mid(prefix.size());
    }
    std::sort(out.begin(), out.end());
    return out;
}

QStringList EsAssets::backgrounds() const
{
    QStringList out;
    for (auto it = images.begin(); it != images.end(); ++it)
        if (it.key().startsWith(QLatin1String("bg "))) out << it.key().mid(3);
    std::sort(out.begin(), out.end());
    return out;
}

QStringList EsAssets::cgs() const
{
    QStringList out;
    for (auto it = images.begin(); it != images.end(); ++it)
        if (it.key().startsWith(QLatin1String("cg "))) out << it.key().mid(3);
    std::sort(out.begin(), out.end());
    return out;
}

const QVector<EsMapZone>& esMapZones()
{
    static const QVector<EsMapZone> z{
        {QStringLiteral("me_mt_house"), QStringLiteral("Мой домик"), 825, 47, 1005, 230},
        {QStringLiteral("estrade"), QStringLiteral("Эстрада"), 1039, 47, 1288, 230},
        {QStringLiteral("music_club"), QStringLiteral("Музклуб"), 541, 231, 711, 356},
        {QStringLiteral("square"), QStringLiteral("Площадь"), 825, 357, 1005, 665},
        {QStringLiteral("dining_hall"), QStringLiteral("Столовая"), 1006, 457, 1159, 665},
        {QStringLiteral("sport_area"), QStringLiteral("Спорткомплекс"), 1160, 457, 1578, 665},
        {QStringLiteral("beach"), QStringLiteral("Пляж"), 1160, 666, 1578, 871},
        {QStringLiteral("boat_station"), QStringLiteral("Лодочный причал"), 825, 666, 1005, 871},
        {QStringLiteral("clubs"), QStringLiteral("Клубы"), 418, 357, 711, 665},
        {QStringLiteral("library"), QStringLiteral("Библиотека"), 1160, 231, 1288, 456},
        {QStringLiteral("medic_house"), QStringLiteral("Медпункт"), 1039, 231, 1159, 456},
        {QStringLiteral("camp_entrance"), QStringLiteral("Ворота в лагерь"), 278, 357, 417, 665},
        {QStringLiteral("forest"), QStringLiteral("Лес"), 541, 47, 711, 230}};
    return z;
}

namespace {
struct Chibi { const char* id; const char* icon; const char* name; };
const Chibi kChibi[] = {{"?", "n00", "Незнакомец"}, {"me", "n01", "Семён"}, {"mi", "n02", "Мику"}, {"sh", "n03", "Шурик"},
                        {"el", "n04", "Электроник"}, {"mz", "n05", "Женя"}, {"mt", "n06", "Ольга Дмитриевна"}, {"uv", "n07", "Юля"},
                        {"un", "n08", "Лена"}, {"us", "n09", "Ульяна"}, {"dv", "n10", "Алиса"}, {"sl", "n11", "Славя"}, {"cs", "n12", "Виола"}};
}

QStringList esChibiIds()
{
    QStringList out;
    for (const Chibi& c : kChibi) out << QString::fromLatin1(c.id);
    return out;
}

QString esChibiFile(const QString& id)
{
    for (const Chibi& c : kChibi)
        if (id == QLatin1String(c.id)) return id == QLatin1String("?") ? QStringLiteral("unknown") : id;
    return {};
}

QString esChibiName(const QString& id)
{
    for (const Chibi& c : kChibi)
        if (id == QLatin1String(c.id)) return QString::fromUtf8(c.name);
    return {};
}

QString EsAssets::characterName(const QString& id) const { return characters.value(id).name; }

QString EsAssets::characterColor(const QString& id, const QString& timeOfDay) const
{
    const EsCharacter c = characters.value(id);
    const QString t = timeOfDay == QLatin1String("prologue") ? QStringLiteral("prolog") : timeOfDay;
    return c.colors.value(t, c.colors.value(QStringLiteral("day"), QStringLiteral("#ffdd7d")));
}

} // namespace gb
