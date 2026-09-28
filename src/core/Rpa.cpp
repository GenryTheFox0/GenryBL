#include "Rpa.h"
#include "Pickle.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QtEndian>

#include <algorithm>

namespace gb {
namespace {

// zlib stream -> bytes. qUncompress wants a 4-byte big-endian size hint in front;
// the real size is unknown, so start generous and grow if Qt refuses.
QByteArray inflate(const QByteArray& z)
{
    qint64 hint = qMax<qint64>(z.size() * 32, 64 * 1024);
    while (hint <= 512ll * 1024 * 1024) {
        QByteArray buf(4, Qt::Uninitialized);
        qToBigEndian<quint32>(quint32(hint), reinterpret_cast<uchar*>(buf.data()));
        buf += z;
        QByteArray out = qUncompress(buf);
        if (!out.isEmpty())
            return out;
        hint *= 8;
    }
    return {};
}

} // namespace

bool RpaArchive::open(const QString& path, QString* error)
{
    m_path = path;
    m_index.clear();
    if (m_file.isOpen()) m_file.close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("не открыть %1").arg(path);
        return false;
    }
    const QString head = QString::fromLatin1(m_file.readLine(256)).trimmed();
    const QStringList parts = head.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    qint64 offset = 0, key = 0;
    bool ok = false;
    if (head.startsWith(QLatin1String("RPA-3.0")) || head.startsWith(QLatin1String("RPA-3.2"))) {
        if (parts.size() >= 3) {
            offset = parts[1].toLongLong(&ok, 16);
            bool ok2 = false;
            key = parts[2].toLongLong(&ok2, 16);
            ok = ok && ok2;
        }
        m_format = parts.value(0);
    } else if (head.startsWith(QLatin1String("RPA-2.0"))) {
        if (parts.size() >= 2) offset = parts[1].toLongLong(&ok, 16);
        m_format = QStringLiteral("RPA-2.0");
    }
    if (!ok) {
        if (error) *error = QStringLiteral("%1: неизвестный заголовок '%2'").arg(path, head.left(40));
        return false;
    }
    m_file.seek(offset);
    const QByteArray raw = inflate(m_file.readAll());
    if (raw.isEmpty()) {
        if (error) *error = QStringLiteral("%1: индекс не распаковался (zlib)").arg(path);
        return false;
    }
    QString perr;
    PyRef root = unpickle(raw, &perr);
    if (!root || root->kind != PyObj::Dict) {
        if (error) *error = QStringLiteral("%1: индекс не pickle-dict: %2").arg(path, perr);
        return false;
    }
    for (const auto& kv : root->dict) {
        const PyRef& entries = kv.second;
        if (!kv.first || !entries || entries->items.isEmpty()) continue;
        const PyRef& t = entries->items.first();
        if (!t || t->items.size() < 2) continue;
        RpaEntry e;
        e.offset = t->items[0]->i ^ key;
        e.length = t->items[1]->i ^ key;
        if (t->items.size() > 2 && t->items[2]) e.prefix = t->items[2]->bytes;
        QString name = kv.first->text();
        name.replace(QLatin1Char('\\'), QLatin1Char('/'));
        m_index.insert(name, e);
    }
    return true;
}

QByteArray RpaArchive::read(const QString& p)
{
    auto it = m_index.constFind(p);
    if (it == m_index.constEnd()) return {};
    const RpaEntry& e = *it;
    m_file.seek(e.offset);
    return e.prefix + m_file.read(e.length - e.prefix.size());
}

QString Vfs::findGameDir(const QString& esRoot)
{
    if (esRoot.isEmpty()) return {};
    const QString a = QDir(esRoot).filePath(QStringLiteral("game"));
    if (QFileInfo(a).isDir()) return QDir::cleanPath(a);
    return {};
}

QString Vfs::workshopDirFor(const QString& esRoot)
{
    // ES: os.path.exists("../../workshop/content/331470/") with cwd = the game root
    if (esRoot.isEmpty()) return {};
    const QString ws = QDir::cleanPath(QDir(esRoot).absoluteFilePath(QStringLiteral("../../workshop/content/331470")));
    return QFileInfo(ws).isDir() ? ws : QString();
}

namespace {
QString g_bundledArchive, g_bundledId;
}

void Vfs::setBundled(const QString& archive, const QString& workshopId)
{
    g_bundledArchive = archive;
    g_bundledId = workshopId;
}

bool Vfs::mount(const QString& esRoot, QString* error)
{
    QMutexLocker lock(&m_lock);
    m_bundledMounted = false;
    m_arcs.clear();
    m_arcSource.clear();
    m_owner.clear();
    m_wsLoose.clear();
    m_wsLooseSource.clear();
    m_workshopIds.clear();
    m_root = esRoot;
    m_game = findGameDir(esRoot);
    if (m_game.isEmpty()) {
        if (error) *error = QStringLiteral("в %1 нет папки game").arg(esRoot);
        return false;
    }

    // searchpath: game/, then every workshop item folder (os.listdir order = by name on NTFS)
    struct Folder { QString dir, id; };
    QVector<Folder> searchpath{{m_game, QString()}};
    const QString ws = workshopDirFor(esRoot);
    if (!ws.isEmpty()) {
        QDir wd(ws);
        for (const QString& id : wd.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            searchpath.push_back({wd.filePath(id), id});
            m_workshopIds << id;
        }
    }

    // archives: append per folder in sorted order, then reverse -> later folders first.
    // index_archives() finds each basename through transfn(), i.e. in the FIRST folder
    // that has it, so a repeated basename is just the same archive again.
    struct Arc { QString path, id, base; };
    QVector<Arc> order;
    for (const Folder& f : searchpath)
        for (const QString& name : QDir(f.dir).entryList({QStringLiteral("*.rpa")}, QDir::Files, QDir::Name))
            order.push_back({QDir(f.dir).filePath(name), f.id, name.toLower()});
    // the program's own copy of a workshop item this PC is not subscribed to: in front, like a workshop archive
    if (!g_bundledArchive.isEmpty() && !m_workshopIds.contains(g_bundledId) && QFileInfo::exists(g_bundledArchive)) {
        order.push_back({g_bundledArchive, g_bundledId, QStringLiteral("genry_bundled_") + QFileInfo(g_bundledArchive).fileName().toLower()});
        m_bundledMounted = true;
    }
    std::reverse(order.begin(), order.end());
    QSet<QString> seenBase;
    for (const Arc& a : order) {
        if (seenBase.contains(a.base)) continue;
        seenBase.insert(a.base);
        QString path = a.path, id = a.id;
        for (const Folder& f : searchpath) {                   // transfn: first folder wins
            const QString p = QDir(f.dir).filePath(a.base);
            if (QFileInfo::exists(p)) { path = p; id = f.id; break; }
        }
        auto arc = std::make_unique<RpaArchive>();
        QString err;
        if (!arc->open(path, &err)) {
            if (error) *error = err;
            continue;                    // a broken archive must not kill the whole mount
        }
        const int idx = int(m_arcs.size());
        for (const QString& f : arc->files())
            if (!m_owner.contains(f)) m_owner.insert(f, idx);  // load_from_archive: first archive wins
        m_arcs.push_back(std::move(arc));
        m_arcSource.push_back(id);
    }

    // loose workshop files can only shadow the game where they sit at the same relative
    // path, and every path the game itself uses starts with images/, sound/ or fonts/.
    for (int i = 1; i < searchpath.size(); ++i) {
        for (const QString& top : {QStringLiteral("images"), QStringLiteral("sound"), QStringLiteral("fonts")}) {
            const QString base = QDir(searchpath[i].dir).filePath(top);
            if (!QFileInfo(base).isDir()) continue;
            QDirIterator it(base, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString abs = it.next();
                const QString rel = QDir(searchpath[i].dir).relativeFilePath(abs);
                if (!m_wsLoose.contains(rel)) {
                    m_wsLoose.insert(rel, abs);
                    m_wsLooseSource.insert(rel, searchpath[i].id);
                }
            }
        }
    }
    return !m_arcs.empty();
}

bool Vfs::has(const QString& path) const
{
    QMutexLocker lock(&m_lock);
    if (m_owner.contains(path) || m_wsLoose.contains(path)) return true;
    return !m_game.isEmpty() && QFileInfo::exists(m_game + QLatin1Char('/') + path);
}

QByteArray Vfs::read(const QString& path) const
{
    QMutexLocker lock(&m_lock);
    if (!m_game.isEmpty()) {
        QFile loose(m_game + QLatin1Char('/') + path);
        if (loose.exists() && loose.open(QIODevice::ReadOnly))
            return loose.readAll();
    }
    auto ws = m_wsLoose.constFind(path);
    if (ws != m_wsLoose.constEnd()) {
        QFile loose(*ws);
        if (loose.open(QIODevice::ReadOnly)) return loose.readAll();
    }
    auto it = m_owner.constFind(path);
    if (it == m_owner.constEnd()) return {};
    return m_arcs[size_t(*it)]->read(path);
}

QByteArray Vfs::readBase(const QString& path) const
{
    QMutexLocker lock(&m_lock);
    if (!m_game.isEmpty()) {
        QFile loose(m_game + QLatin1Char('/') + path);
        if (loose.exists() && loose.open(QIODevice::ReadOnly)) return loose.readAll();
    }
    for (size_t i = 0; i < m_arcs.size(); ++i)
        if (m_arcSource.value(int(i)).isEmpty() && m_arcs[i]->contains(path)) return m_arcs[i]->read(path);
    return {};
}

QString Vfs::sourceOf(const QString& path) const
{
    QMutexLocker lock(&m_lock);
    if (!m_game.isEmpty() && QFileInfo::exists(m_game + QLatin1Char('/') + path)) return {};
    auto ws = m_wsLooseSource.constFind(path);
    if (ws != m_wsLooseSource.constEnd()) return *ws;
    auto it = m_owner.constFind(path);
    return it == m_owner.constEnd() ? QString() : m_arcSource.value(*it);
}

QStringList Vfs::archiveFiles() const
{
    QMutexLocker lock(&m_lock);
    return m_owner.keys();
}

} // namespace gb
