// GenryBL V1 - the Steam Workshop as an asset library: every picture of every installed
// Everlasting Summer mod (loose files and the insides of its .rpa archives), browsable by
// folder, searchable across all mods, readable for thumbnails and «take into my mod».
// A picture is addressed as "<workshop id>/<virtual path>", where an archive is a folder:
// "354397869/sam.rpa/samantha/image/menu/sam_menu_start.png".
#pragma once
#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

namespace gb {

class RpaArchive;

struct LibItem {
    QString id, title;
    QString dir;                 // the workshop folder
    QStringList images;          // virtual paths, sorted
};

class Library {
public:
    Library();
    ~Library();
    void scan(const QString& workshopDir);           // slow (tens of thousands of files): run off the UI thread
    bool scanned() const;
    QVector<LibItem> items() const;                   // mods that have pictures, biggest first
    // one folder of one mod: its subfolders (with picture counts) and the pictures right in it;
    // with a query: every picture of the mod (or of all mods when id is empty) whose path matches
    struct Listing {
        QVector<QPair<QString, int>> folders;
        QStringList files;                           // full refs "<id>/<vpath>"
        int total = 0;                               // before the limit
    };
    Listing list(const QString& id, const QString& folder, const QString& query, int limit) const;
    QByteArray read(const QString& ref) const;       // "<id>/<vpath>"
    QString titleOf(const QString& id) const;

private:
    mutable QMutex m_mx;
    QVector<LibItem> m_items;
    QHash<QString, int> m_byId;
    mutable QHash<QString, std::shared_ptr<RpaArchive>> m_arcs;   // archive path -> open archive
    bool m_scanned = false;
};

} // namespace gb
