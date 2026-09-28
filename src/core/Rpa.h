// GenryBL V1 - Ren'Py archive reader + a VFS over an Everlasting Summer install (+ its Steam Workshop).
#pragma once
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>

namespace gb {

struct RpaEntry {
    qint64 offset = 0;
    qint64 length = 0;
    QByteArray prefix;
};

class RpaArchive {
public:
    bool open(const QString& path, QString* error);
    QString format() const { return m_format; }
    QString path() const { return m_path; }
    QStringList files() const { return m_index.keys(); }
    bool contains(const QString& p) const { return m_index.contains(p); }
    QByteArray read(const QString& p);

private:
    QString m_path;
    QString m_format;
    QHash<QString, RpaEntry> m_index;
    QFile m_file;
};

// Everything the game can load, resolved in Everlasting Summer's own order
// (its renpy/main.py + loader.py):
//   1. loose files under game/
//   2. loose files in Steam Workshop item folders (searchpath order)
//   3. archives - ES collects the .rpa of game/ and of every workshop folder, then
//      REVERSES the list, so workshop archives beat game/archive.rpa. That is how the
//      18+ patch (i8_data.rpa) swaps the character bodies and brings its CGs.
class Vfs {
public:
    static QString findGameDir(const QString& esRoot);
    static QString workshopDirFor(const QString& esRoot);   // steamapps/workshop/content/331470
    bool mount(const QString& esRoot, QString* error);
    bool isMounted() const { return !m_game.isEmpty(); }
    QString root() const { return m_root; }
    QString gameDir() const { return m_game; }
    bool has(const QString& path) const;
    QByteArray read(const QString& path) const;
    // the Steam build's own file: game/ (loose or its archives), never a workshop item (not the 18+ patch)
    QByteArray readBase(const QString& path) const;
    QStringList archiveFiles() const;          // union of all archive paths
    const std::vector<std::unique_ptr<RpaArchive>>& archives() const { return m_arcs; }
    // "" = the game itself, otherwise the workshop item id the file really comes from
    QString sourceOf(const QString& path) const;
    QStringList workshopItems() const { return m_workshopIds; }
    // an archive that ships with the program and stands in for a workshop item nobody subscribed to here
    // (GenryBL: the 18+ patch in data/patch); set before mount(). A subscribed item always wins.
    static void setBundled(const QString& archive, const QString& workshopId);
    bool bundledMounted() const { return m_bundledMounted; }

private:
    bool m_bundledMounted = false;
    QString m_root;
    QString m_game;
    std::vector<std::unique_ptr<RpaArchive>> m_arcs;   // ES priority order: first wins
    QVector<QString> m_arcSource;                      // workshop id per archive ("" = game/)
    QHash<QString, int> m_owner;                // path -> archive index
    QHash<QString, QString> m_wsLoose;          // images/ sound/ fonts/ path -> absolute file (first folder wins)
    QHash<QString, QString> m_wsLooseSource;    // same path -> workshop id
    QStringList m_workshopIds;
    mutable QMutex m_lock;
};

} // namespace gb
