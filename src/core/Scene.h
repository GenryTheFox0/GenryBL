// GenryBL V1 - what the player sees after a given story line. It follows the same
// command semantics as the compiler (and ES's own transforms), so the one preview
// renderer can draw the frame the game will draw.
#pragma once
#include "Compiler.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gb {

class EsAssets;

struct SpriteShow {
    QString tag;          // Ren'Py tag ("dv", or the alias of "show ... as x")
    QString image;        // full image name ("dv smile pioneer")
    double xpos = 0.5, xanchor = 0.5;   // fractions (ES transforms: xalign N + xanchor 0.5)
    double ypos = 0.0, yanchor = 0.0;   // ES sprites hang from the top edge
    double zoom = 1.0, alpha = 1.0;
    bool mirror = false;
    bool timeTint = true; // genry_sprite_time_tint
};

struct PhoneMessage {
    QString side, name, text, time, status;
};

struct SceneState {
    QString timeOfDay = QStringLiteral("day");   // day / sunset / night / prologue
    QString spriteTime = QStringLiteral("day");  // persistent.sprite_time
    bool timeExplicit = false;                   // the story said «время» (a picture of another time swaps)
    QString bg;                                   // "bg ext_road_day", "cg d1", "black", custom name
    QVector<SpriteShow> sprites;                  // draw order
    bool dream = false;
    double dreamAlpha = 0.7;
    double dim = 0.0;                             // genry_black
    double noteDim = 0.0;                         // genry_note_dim (overlay)
    bool eyesClosed = false, sleepy = false;
    QString weather, filter;
    int weatherLevel = 2;                         // «слабо» 1 / обычно 2 / «сильно» 3
    double eyesSeconds = 2.0;                     // how long the lids close / open (the command's number, the game's 2 s)
    bool liveFx = false;                          // the cinema moves the weather and the lids itself: not baked in the frame
    bool windowHidden = false;
    QString music;                                // for the HUD
    // dialogue
    QString speakerId, speakerName, speakerColor, text, whatColor;
    bool thought = false;
    bool hideSayText = false;                     // «кино-режим»: the box and the name, the words are typed over it
    // transient: only on the line that shows them
    QString cardKind, cardText, cardSub;          // timeskip | title | chapter | credits
    QStringList choices;
    QString choiceStyle;                          // "es" game menu | "buttons" dark | "images" 7DL strips
    QStringList choiceImages, choiceKinds;        // per option: Ren'Py image name ("" = none), "sprite" | "bg"
    QStringList choiceHints;                      // per option: "" = open, else locked («[нужно …]») with this hint
    bool choiceAsked = false;                     // the question stays in the dialogue box under the menu (ES menu, buttons)
    int choiceHover = 0;                          // the strip drawn lit (-1 = none)
    QString menuTitle;                            // screen menu
    bool screenMenu = false;
    QString notifyTitle, notifyText;
    QString floating;
    bool flash = false, map = false;
    QStringList mapZones;                         // «карта»: "zone|scene|chibi" of the open places
    QString nvlText;
    bool nvlMode = false;                         // nvlначать / заметканvl: lines stack on an NVL page
    QStringList nvlPage;                          // "name|#color|text" of the page so far
    QStringList musicPlayer;                      // transient: genry_music_player track titles
    QString achievement;                          // transient: popup picture (as written in the story)
    QString videoCard;                            // transient: a cutscene video
    QString videoBg;                              // video background until the picture changes
    QString moment;                               // transient: shake / pixels / blink / a transition
    QString ambience;                             // playing ambience
    QString sound;                                // transient: the sound or voice this line plays
    // V1 features
    QString choiceSeconds;                        // «выбор на время N»
    QString remember;                             // transient: «Алиса это запомнит.»
    QStringList meterPing;                        // transient: {title, #color, delta, value, min, max}
    QVector<QStringList> meters;                  // «шкала»: {title, #color, var, min, max}
    QHash<QString, double> meterValues;           // var -> value, reading the story top-down
    bool showMeters = false, metersButton = false;
    QStringList inventory;                        // captions of the items held, top-down
    bool showInventory = false, inventoryButton = false;
    bool modMenuOpen = false;                     // cursor inside «менюмода … конецменюмода»
    QString modMenuTitle, modMenuLogo;
    QString modMenuStyle = QStringLiteral("panel");   // menuStyleKey of «менюмода <стиль>»
    QStringList modMenuHeroes;                    // «герои» of a 7ДЛ menu
    QStringList modMenuKinds;                     // menuButtonKind of every button («карта»: where it stands)
    QString modMenuAuthor;                        // @author / «автор …»; empty = no line
    MenuParts modMenuParts;                       // «менюмода свой»: кнопки / вид / цвет / частицы / появление
    QString playerName;                           // «имяигрока … | Имя»: the name the preview shows for Семён / [имя]
    bool playerShe = false;                       // @hero_gender она: «[проснулся/проснулась]» -> проснулась
    int textPages = 1;                            // the boxes the current line is shown in (splitForBox)
    int textPage = 1;                             // which of them is on screen (the cinema turns them)
    QStringList textBoxes;                        // all of them
    QStringList modMenuButtons;
    bool showGallery = false, showAchievements = false;
    QStringList galleryCgs;                       // "cg x" of the mod
    QStringList achievements;                     // "caption|got|h|picture": got 1/0, h = hidden until it comes
    QString achievementPlate;                     // transient: ES's achievement plate with this title slides in
    QString codeLock;                             // transient: «кодовыйзамок» - "hint|digits|tries" on ES's o_rly plate
    bool flashlight = false;                      // «фонарик»: the dark with a light (the preview puts it in the middle)
    double flashZoom = 1.5;
    QString flashColor = QStringLiteral("#050810");
    // phone
    bool phoneOpen = false;
    QString phoneContact, phoneTyping;
    QVector<PhoneMessage> phone;                  // side: me/them (+ _photo, _photo18, _voice) or call
    QString phoneCall, phoneCallFace;             // transient: the incoming call screen
    bool feedOpen = false;                        // transient: «лента»
    QString feedTitle;
    QVector<QStringList> feed;                    // «пост»: {who, text, image, likes, "c:who|text"...}, newest first
    bool phoneHome = false;                       // transient: «телефон дом»
    QString pushWho, pushText;                    // transient: «пуш Кто: текст»
    int line = 0;
    QHash<QString, QString> customSpeakers;       // персонаж id/name -> "Name|#color"
    QHash<QString, QString> strangers;            // V2.1 «прозвище»: speaker id -> the name shown until she names herself
};

// Walk the story up to `upto` lines (-1 = all). `es` gives names/colours of the cast.
SceneState sceneAt(const QString& storyText, int upto, const EsAssets* es = nullptr);

} // namespace gb
