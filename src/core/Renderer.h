// GenryBL V1 - THE preview renderer. One renderer for the editor, the pickers, the
// project covers and the launcher: it draws the frame Everlasting Summer will draw,
// from the game's own assets and screen layout (1920x1080, say screen at 174,916).
#pragma once
#include "Scene.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>

namespace gb {

class EsAssets;

class Renderer {
public:
    static constexpr int W = 1920;
    static constexpr int H = 1080;

    void setAssets(const EsAssets* es, const QString& dataDir);
    // Project pictures by Ren'Py image name ("genry_alisa_casual", "bg my_room") -> file
    void setCustomImages(const QHash<QString, QString>& nameToFile);
    QHash<QString, QString> customImages() const;
    void dropSpriteCache();                  // new pictures appeared (the workshop wardrobe got ready)

    QImage render(const SceneState& s, bool hud = false) const;           // 1920x1080
    // ES «Моды и пользовательские сценарии» page with the mod's title as ES draws it
    // (settings_text: corbel 36 #4d2e19 unless the mod's {font}/{color}/{size}/{b}/{i} say otherwise)
    QImage modsList(const QString& title, const QString& family, const QString& color, int size, bool bold, bool italic) const;
    QString headerFamily() const { return m_header; }
    QImage sprite(const QString& image, const QString& spriteTime = QStringLiteral("day")) const;
    QImage spriteThumb(const QString& image, int box) const;               // body, cropped
    QImage faceThumb(const QString& image, int box) const;                 // head & shoulders
    QImage background(const QString& name) const;                          // 1920x1080
    QImage gameFile(const QString& path) const;                            // any image in the game
    QString fontFamily() const { return m_family; }

private:
    QImage load(const QString& gamePath) const;
    QImage phonePicture(const QString& name) const;
    const EsAssets* m_es = nullptr;
    QString m_dataDir;
    QString m_family = QStringLiteral("Calibri");
    QString m_header = QStringLiteral("Corbel");
    QString m_link = QStringLiteral("Century Gothic");   // ES link_font (fonts/gothic.ttf)
    QHash<QString, QString> m_custom;
    mutable QMutex m_mx;
    mutable QHash<QString, QImage> m_files, m_sprites, m_bgs;
};

} // namespace gb
