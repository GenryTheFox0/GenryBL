// GenryBL V1 - Телефон 3.0: every picture of the phone drawn by code (like Weather.cpp's
// particles). The builder writes them into the mod (genry/phone/<name>.png) for the
// Ren'Py screens in V1Screens.inc; the editor preview draws with the very same images.
#pragma once
#include <QImage>
#include <QString>
#include <QStringList>

namespace gb {

// geometry of the phone (body.png) - the Ren'Py screens and the preview use these numbers
constexpr int kPhoneW = 520, kPhoneH = 1020;             // body.png
constexpr int kPhoneScrX = 24, kPhoneScrY = 19;          // the screen inside the body
constexpr int kPhoneScrW = 472, kPhoneScrH = 982;

QStringList phoneAssetNames();
QImage phoneAsset(const QString& name);                  // "body", "wall", "bubble_me", ...

} // namespace gb
