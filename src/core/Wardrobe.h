// GenryBL V1 - «Гардероб мастерской»: every picture of the Steam Workshop laid out like ES's own
// sprite layers (sprites/<normal|close|far>/<tag>/<tag>_<pose>_<part>.png - the modders' resource
// pack, 7DL, bkrr, …) becomes an outfit, a body or a face that composes with the game's own layers:
//   показать dv smile nude          ->  dv_4_body (game) + dv_4_nude (workshop) + dv_4_smile (game)
//   показать mi shy_smile towel close
// The mod gets the sprite the way ES defines its own (sprites.rpy): a ConditionSwitch of
// im.Composite((w,1080), …) with the sunset / night tint, and only the workshop layers it really uses
// (images/genry_wardrobe/<workshop id>/<dist>/<tag>/<file>.png).
// 18+ parts (nude, topless, panties, towel…) and the workshop's bodies are indexed for the adult heroines only:
// Ульяна and everyone else always wear the Steam build's own body (images/genry_wardrobe/base/…, never the patch's).
// A «clothes» picture that is really a whole second figure (its own head and face - Лена «boy», Мику
// «camisole_far») is worn alone: no body under it, no emotion over it (показать un boy).
#pragma once
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QReadWriteLock>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gb {

class EsAssets;
class Library;

struct WardrobeLayer {
    QString src;         // "" = the game (path = game path), "base" = the Steam build's own file (game path, copied
                         // into the mod so the 18+ patch cannot swap it), "patch" = the 18+ patch's body of a grown-up
                         // heroine (game path, copied into the mod: players without the patch see it too), else the
                         // workshop item id
    QString path;        // game path / virtual path inside the workshop item
};

struct WardrobeLook {
    QString tag, dist;   // dist: "" | close | far
    int pose = 0;
    int w = 900, h = 1080;
    QVector<WardrobeLayer> layers;   // bottom-up: body, clothes, face, accessories
    QVector<int> kinds;              // Wardrobe::Kind of each layer
    QPoint faceShift;                // «поправить лицо»: where the face layers are moved (the user's fix)
    bool adult = false;
    bool workshop = false;           // at least one layer comes from the workshop
};

class Wardrobe {
public:
    enum Kind { None, Body, Clothes, Face, Accessory };
    struct Outfit {
        QString part;
        bool adult = false;
        bool body = false;           // replaces the body layer (pibody, body2…); "body" = the bare body
        bool figure = false;         // a whole picture of the character: worn alone (looks = {part})
        QString source;              // workshop id of its first picture
        int poses = 0;
        QStringList dists;           // "" (normal) / close / far it is drawn for
    };

    // Slow (thousands of pictures): run off the UI thread. cacheFile keeps the picture boxes,
    // so later starts decode only what changed in the workshop.
    void build(const Library& lib, const EsAssets& es, const QString& cacheFile);
    bool ready() const { return m_ready; }
    int partCount() const { return m_count; }
    int decoded() const { return m_decoded; }

    // "dv smile nude close" -> its layers. False for ES's own sprite names (the game has them) and
    // for anything the wardrobe cannot put together (why = what is missing, in Russian).
    bool resolve(const QString& image, WardrobeLook* look = nullptr, QString* why = nullptr) const;
    // the whole sprite (untinted), preview; forcedShift = try a face shift before it is saved
    QImage compose(const QString& image, const QPoint* forcedShift = nullptr) const;
    QRect faceBox(const QString& image) const;              // the face layers' opaque box (overlays)
    QByteArray read(const WardrobeLayer& l) const;
    Kind kind(const QString& tag, const QString& part) const { return Kind(m_kind.value(tag + QLatin1Char('|') + part, None)); }
    bool hasTag(const QString& tag) const { return m_tags.contains(tag); }

    // the workshop outfits of a character (not the game's own), SFW first
    QVector<Outfit> outfits(const QString& tag) const;
    // every "<emotion> <outfit>" the character can wear it with at that distance ("" | close | far)
    QStringList looks(const QString& tag, const QString& outfit, const QString& dist = QString()) const;

    static bool isAdultPart(const QString& part);
    static bool adultAllowed(const QString& tag);          // the grown-up heroines (18+ parts, workshop bodies)
    // a figure (worn alone) or clothes with a face of their own (worn on the body): no emotion - «показать un boy»
    bool wornAlone(const QString& tag, const QString& part) const
    {
        return m_figures.contains(tag + QLatin1Char('|') + part) || m_ownFace.contains(tag + QLatin1Char('|') + part);
    }

    // `image dv smile nude = ConditionSwitch(...)` for the mod (layers under mods/<modId>/…)
    QString definition(const QString& image, const QString& modId) const;
    static QString modFile(const WardrobeLayer& l);        // "2519236508/normal/dv/dv_4_nude.png" (under images/genry_wardrobe/)
    QByteArray readModFile(const QString& rel) const;      // the picture a modFile() path stands for

    // «удалить навсегда»: the user's own list (Engine keeps it in work/wardrobe_hidden.txt), keys
    // "tag|look:smile nude" (one sprite), "tag|face:smile" (an emotion everywhere), "tag|outfit:nude"
    void setHidden(const QSet<QString>& keys);
    bool isHidden(const QString& tag, const QString& face, const QString& outfit) const;
    // «поправить лицо»: the user's shifts of the face layers (Engine: work/wardrobe_faces.txt), keys
    // "tag|dist|look:smile nude" before "tag|dist|outfit:nude" before "tag|dist|face:smile" (dist normal|close|far)
    void setFaceShifts(const QHash<QString, QPoint>& shifts);
    QPoint faceShift(const QString& tag, const QString& dist, const QString& face, const QString& outfit, QString* scope = nullptr) const;

private:
    const Library* m_lib = nullptr;
    const EsAssets* m_es = nullptr;
    bool m_ready = false;
    int m_count = 0, m_decoded = 0;
    QHash<QString, QVector<WardrobeLayer>> m_layers;   // "tag|dist|pose|part" -> candidates (game first)
    QHash<QString, int> m_kind;                         // "tag|part" -> Kind
    QHash<QString, QVector<int>> m_poses;               // "tag|dist" -> poses with a body
    QHash<QString, QStringList> m_partsAt;              // "tag|dist|pose" -> parts
    QHash<QString, int> m_esEmoPose;                    // "tag|emotion" -> ES pose (normal distance)
    QHash<QString, int> m_width;                        // "tag|dist" -> ES canvas width
    QHash<QString, QString> m_relRef;                   // modFile() -> "<id>/<vpath>"
    QSet<QString> m_esNames, m_gameParts, m_tags;       // ES sprite names; "tag|part" the game has; tags with workshop layers
    QSet<QString> m_figures;                            // "tag|part": a whole second figure, worn alone (no body)
    QSet<QString> m_ownFace;                            // "tag|part": clothes covering the face with one of their own
    QSet<QString> m_badFace;                            // "tag|dist|pose|part|src": a piece of a face / off the head
    QSet<QString> m_esBodyTags;                         // characters ES itself shows bare («un … body»)
    mutable QReadWriteLock m_hideLock;
    QSet<QString> m_hidden;
    QHash<QString, QPoint> m_shifts;
};

// one wardrobe for the whole program (the preview, the compiler, lint, the builder); null until built
const Wardrobe* wardrobe();
void setWardrobe(const Wardrobe* w);

} // namespace gb
