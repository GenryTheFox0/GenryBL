#include "Library.h"
#include "Rpa.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>

namespace gb {
namespace {

bool isPicture(const QString& p)
{
    const QString l = p.toLower();
    return l.endsWith(QLatin1String(".png")) || l.endsWith(QLatin1String(".jpg")) || l.endsWith(QLatin1String(".jpeg")) ||
           l.endsWith(QLatin1String(".webp"));
}

// the text inside a .rpyc (RPC2: slot table, slot 1 = zlib'd pickle; the strings are readable enough to grep)
QString rpycText(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024 * 1024) return {};
    const QByteArray data = f.readAll();
    QByteArray z;
    if (data.startsWith("RENPY RPC2")) {
        int pos = 10;
        while (pos + 12 <= data.size()) {
            const quint32 slot = qFromLittleEndian<quint32>(data.constData() + pos);
            const quint32 start = qFromLittleEndian<quint32>(data.constData() + pos + 4);
            const quint32 len = qFromLittleEndian<quint32>(data.constData() + pos + 8);
            pos += 12;
            if (slot == 0) break;
            if (slot == 1 && qint64(start) + len <= data.size()) { z = data.mid(int(start), int(len)); break; }
        }
    } else {
        z = data;
    }
    if (z.isEmpty()) return {};
    QByteArray sized(4, '\0');
    qToBigEndian<quint32>(quint32(qMin<qint64>(qint64(z.size()) * 12, 200 * 1024 * 1024)), sized.data());
    return QString::fromUtf8(qUncompress(sized + z));
}

QString cleanTitle(QString t)
{
    static const QRegularExpression tags(QStringLiteral("\\{[^}]*\\}"));
    t.remove(tags);
    return t.simplified().left(70);
}

// mods without a mods[...] title of their own
QString knownTitle(const QString& id)
{
    static const QHash<QString, QString> t{
        {QStringLiteral("2519236508"), QStringLiteral("Ресурсы для моддеров: спрайты, фоны, CG")},
        {QStringLiteral("3030968304"), QStringLiteral("Сборник: Саманта, Современный Совёнок и др.")},
        {QStringLiteral("354397869"), QStringLiteral("Саманта")},
        {QStringLiteral("3665061135"), QStringLiteral("Спрайты в купальниках")},
        {QStringLiteral("488525738"), QStringLiteral("Зимняя сказка")},
        {QStringLiteral("401412543"), QStringLiteral("Мику")},
        {QStringLiteral("3376009752"), QStringLiteral("Решайся")},
        {QStringLiteral("3737128938"), QStringLiteral("Своё главное меню")},
        {QStringLiteral("3130079690"), QStringLiteral("3D режим (параллакс)")},
    };
    return t.value(id);
}

}   // namespace

QString compiledScriptText(const QString& path) { return rpycText(path); }
QString workshopKnownTitle(const QString& id) { return knownTitle(id); }

Library::Library() = default;
Library::~Library() = default;

bool Library::scanned() const
{
    QMutexLocker lock(&m_mx);
    return m_scanned;
}

void Library::scan(const QString& workshopDir)
{
    static const QRegularExpression titleRe(QStringLiteral("mods\\s*\\[\\s*u?[\"']([^\"']+)[\"']\\s*\\]\\s*=\\s*u?[\"'](.+?)[\"']"));
    QVector<LibItem> items;
    QHash<QString, std::shared_ptr<RpaArchive>> arcs;
    for (const QString& id : QDir(workshopDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        LibItem it;
        it.id = id;
        it.dir = workshopDir + QLatin1Char('/') + id;
        QStringList rpyc;
        QDirIterator di(it.dir, QDir::Files, QDirIterator::Subdirectories);
        while (di.hasNext()) {
            const QString abs = di.next();
            const QString rel = abs.mid(it.dir.size() + 1);
            const QString low = rel.toLower();
            if (isPicture(rel)) {
                it.images << rel;
            } else if (low.endsWith(QLatin1String(".rpa"))) {
                auto arc = std::make_shared<RpaArchive>();
                QString err;
                if (!arc->open(abs, &err)) continue;
                for (const QString& inner : arc->files())
                    if (isPicture(inner)) it.images << rel + QLatin1Char('/') + inner;
                arcs.insert(abs, arc);
            } else if (it.title.isEmpty() && low.endsWith(QLatin1String(".rpy")) && QFileInfo(abs).size() < 8 * 1024 * 1024) {
                QFile f(abs);
                if (f.open(QIODevice::ReadOnly)) {
                    const QRegularExpressionMatch m = titleRe.match(QString::fromUtf8(f.readAll()));
                    if (m.hasMatch()) it.title = cleanTitle(m.captured(2));
                }
            } else if (low.endsWith(QLatin1String(".rpyc")) && rpyc.size() < 12) {
                rpyc << abs;
            }
        }
        if (it.images.isEmpty()) continue;
        for (int i = 0; it.title.isEmpty() && i < rpyc.size(); ++i) {
            const QRegularExpressionMatch m = titleRe.match(rpycText(rpyc[i]));
            if (m.hasMatch()) it.title = cleanTitle(m.captured(2));
        }
        if (it.title.isEmpty()) it.title = knownTitle(id);
        if (it.title.isEmpty()) {
            const QStringList top = QDir(it.dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            it.title = QStringLiteral("Мод ") + id + (top.isEmpty() ? QString() : QStringLiteral(" (") + top.first() + QLatin1Char(')'));
        }
        it.images.sort(Qt::CaseInsensitive);
        items << it;
    }
    std::sort(items.begin(), items.end(), [](const LibItem& a, const LibItem& b) { return a.images.size() > b.images.size(); });
    QMutexLocker lock(&m_mx);
    m_items = items;
    m_byId.clear();
    for (int i = 0; i < m_items.size(); ++i) m_byId.insert(m_items[i].id, i);
    m_arcs = arcs;
    m_scanned = true;
}

QVector<LibItem> Library::items() const
{
    QMutexLocker lock(&m_mx);
    return m_items;
}

QString Library::titleOf(const QString& id) const
{
    QMutexLocker lock(&m_mx);
    const int i = m_byId.value(id, -1);
    return i < 0 ? QString() : m_items[i].title;
}

Library::Listing Library::list(const QString& id, const QString& folder, const QString& query, int limit) const
{
    QMutexLocker lock(&m_mx);
    Listing out;
    // «dv swim» = both words, «naked|nude|hentai» = any of them
    QVector<QStringList> alts;
    for (const QString& a : query.toLower().split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
        const QStringList w = a.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (!w.isEmpty()) alts << w;
    }
    if (!alts.isEmpty()) {
        for (const LibItem& it : m_items) {
            if (!id.isEmpty() && it.id != id) continue;
            for (const QString& p : it.images) {
                const QString low = p.toLower();
                bool hit = false;
                for (const QStringList& words : alts) {
                    bool all = true;
                    for (const QString& w : words) if (!low.contains(w)) { all = false; break; }
                    if (all) { hit = true; break; }
                }
                if (!hit) continue;
                if (out.files.size() < limit) out.files << it.id + QLatin1Char('/') + p;
                ++out.total;
            }
        }
        return out;
    }
    const int i = m_byId.value(id, -1);
    if (i < 0) return out;
    const LibItem& it = m_items[i];
    const QString prefix = folder.isEmpty() ? QString() : folder + QLatin1Char('/');
    QHash<QString, int> counts;
    QStringList order;
    for (const QString& p : it.images) {
        if (!p.startsWith(prefix)) continue;
        const QString rest = p.mid(prefix.size());
        const int slash = int(rest.indexOf(QLatin1Char('/')));
        if (slash >= 0) {
            const QString sub = rest.left(slash);
            if (!counts.contains(sub)) order << sub;
            counts[sub] += 1;
        } else {
            if (out.files.size() < limit) out.files << it.id + QLatin1Char('/') + p;
            ++out.total;
        }
    }
    order.sort(Qt::CaseInsensitive);
    for (const QString& s : order) out.folders.push_back({s, counts.value(s)});
    return out;
}

QByteArray Library::read(const QString& ref) const
{
    const QString id = ref.section(QLatin1Char('/'), 0, 0);
    const QString vpath = ref.mid(id.size() + 1);
    QMutexLocker lock(&m_mx);
    const int i = m_byId.value(id, -1);
    if (i < 0 || vpath.contains(QLatin1String(".."))) return {};
    const QString dir = m_items[i].dir;
    // an archive is a folder of the virtual path: find the .rpa component
    const QStringList parts = vpath.split(QLatin1Char('/'));
    QString acc;
    for (int k = 0; k < parts.size() - 1; ++k) {
        acc += (k ? QStringLiteral("/") : QString()) + parts[k];
        if (!parts[k].endsWith(QLatin1String(".rpa"), Qt::CaseInsensitive)) continue;
        const auto arc = m_arcs.value(dir + QLatin1Char('/') + acc);
        if (arc) return arc->read(parts.mid(k + 1).join(QLatin1Char('/')));
    }
    QFile f(dir + QLatin1Char('/') + vpath);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace gb
