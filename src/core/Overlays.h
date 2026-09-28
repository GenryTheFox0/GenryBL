// GenryBL V1 - overlays on any ES sprite, drawn by code like the weather: румянец (blush), пот (sweat),
// слёзы (tears), мокрая (wet). The face is found from the sprite's own emotion layer (its opaque box =
// eyes to mouth), the blush follows the skin tone, so it never paints over eyes or hair.
//   показать dv smile pioneer румянец пот left   ->   show dv smile pioneer genry_ov_blush_sweat at left
// The mod gets `image dv smile pioneer genry_ov_blush_sweat = Fixed("dv smile pioneer", "<png>", fit_first=True)`
// and the PNG (the sprite's full canvas, transparent but the overlay) from the builder.
#pragma once
#include <QImage>
#include <QString>
#include <QStringList>

namespace gb {

class EsAssets;

const QStringList& overlayKinds();                    // blush sweat tears wet (the canonical order)
QString overlayKind(const QString& word);             // "румянец" / "blush" -> "blush", "" = not an overlay word
QString overlayWord(const QString& kind);             // "blush" -> "румянец"
// "dv smile pioneer румянец пот" -> "dv smile pioneer genry_ov_blush_sweat" (unchanged without overlay words)
QString withOverlays(const QString& imageWords);
// "dv smile pioneer genry_ov_blush_sweat" -> base "dv smile pioneer", kinds {blush, sweat}
bool splitOverlays(const QString& image, QString* base, QStringList* kinds);
QString overlayFile(const QString& image);            // "dv_smile_pioneer__blush_sweat.png"
QImage makeOverlay(const EsAssets& es, const QString& baseSprite, const QStringList& kinds);

} // namespace gb
