// GenryBL V1 - «Бесконечное лето» (the Center): every mod the game will load - subscribed in the Steam Workshop
// (steamapps/workshop/content/331470/<id>) and put by hand into game/mods - with what the game's own Mods menu
// knows about it (its entries: mods["label"] = u"Name"), its cover, its size, whether GenryBL made it, and the same
// entry coming from two places (the game keeps only one of them). Plus the game's own launch into one entry, the way
// its Mods menu does it: persistent.jump_to + Start.
#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

namespace gb {

struct HubEntry {
    QString label;             // the label the Mods menu starts
    QString title;             // its name there, text tags gone
};

struct HubMod {
    QString source;            // workshop | local
    QString id;                // the Workshop id / the folder in game/mods
    QString dir;
    QString title;             // the first entry's name, a known title, or the folder
    QVector<HubEntry> entries; // empty: a mod with no entry of its own (resources, a menu, a fix)
    QString preview;           // a cover picture in the mod, "" if none
    qint64 size = 0;
    int files = 0;
    bool genrybl = false;      // built by GenryBL (its owner mark is there)
    QStringList twins;         // other mods with the same entry label ("workshop:123" / "local:folder")
};

QVector<HubMod> scanGameMods(const QString& esRoot);

// The hook for «▶ Играть» on one entry: game/mods/_genry_play/_genry_play.rpy - when the game reaches its main menu it
// starts that entry the way the game's own Mods menu does, then deletes itself. Launching the game is the caller's
// (through Steam, so playtime and achievements count).
bool writePlayHook(const QString& esRoot, const QString& label, QString* err);
void removePlayHook(const QString& esRoot);

} // namespace gb
