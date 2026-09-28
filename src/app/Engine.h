// GenryBL V1 - the one object QML talks to. Owns Everlasting Summer's assets (read
// in place from archive.rpa) and THE renderer; every picture in the app - the editor
// preview, emotion faces, backgrounds, project covers, the ES main menu art in the
// launcher - comes through the "gb" image provider.
#pragma once
#include "Builder.h"
#include "Cinema.h"
#include "EsAssets.h"
#include "Forms.h"
#include "Library.h"
#include "Renderer.h"
#include "Scene.h"
#include "Wardrobe.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <functional>

class QJSEngine;
class QNetworkAccessManager;
class QQmlEngine;

class Engine : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString startupError READ startupError NOTIFY readyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)
    Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY gameRunningChanged)
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(QString currentProject READ currentProject NOTIFY currentProjectChanged)
    Q_PROPERTY(QString currentProjectName READ currentProjectName NOTIFY currentProjectChanged)
    Q_PROPERTY(QVariantList commands READ commands CONSTANT)
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
    Q_PROPERTY(QVariantList cast READ cast NOTIFY assetsChanged)
    Q_PROPERTY(QVariantList backgrounds READ backgrounds NOTIFY assetsChanged)
    Q_PROPERTY(QVariantList cgs READ cgs NOTIFY assetsChanged)
    Q_PROPERTY(QVariantList music READ music NOTIFY assetsChanged)
    Q_PROPERTY(QVariantList sounds READ sounds NOTIFY assetsChanged)
    Q_PROPERTY(QVariantList ambience READ ambience NOTIFY assetsChanged)
    Q_PROPERTY(QString appRoot READ appRoot CONSTANT)
    Q_PROPERTY(QString esRoot READ esRoot NOTIFY readyChanged)
    // «Обновить»: {state: ""|checking|latest|available|downloading|starting|error|dev, text, version, source: github|steam, progress 0..1}
    Q_PROPERTY(QVariantMap updateInfo READ updateInfo NOTIFY updateChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(int libraryState READ libraryState NOTIFY libraryChanged)   // 0 not scanned, 1 scanning, 2 ready
    Q_PROPERTY(int wardrobeState READ wardrobeState NOTIFY wardrobeChanged) // 0 / 1 loading / 2 ready
    Q_PROPERTY(bool needsEs READ needsEs NOTIFY readyChanged)            // the first start: where is the game?
    Q_PROPERTY(bool release READ isRelease CONSTANT)                     // the public build
    Q_PROPERTY(QString editionBadge READ editionBadge CONSTANT)          // "" in the public build, «сборка разработчика» otherwise
    Q_PROPERTY(bool ageOk READ ageOk WRITE setAgeOk NOTIFY ageChanged)     // «мне есть 18» said once
    Q_PROPERTY(bool patchInstalled READ patchInstalled NOTIFY assetsChanged)   // the 18+ patch 1118110148 is there (either way)
    Q_PROPERTY(bool patchBundled READ patchBundled NOTIFY assetsChanged)       // … as GenryBL's own copy (data/patch)
    Q_PROPERTY(QString mode READ mode CONSTANT)                          // "" | install | uninstall (Installer.cpp)
    Q_PROPERTY(bool esArt READ esArt NOTIFY esArtChanged)                // the installer found the game: it wears its art

public:
    static Engine* boot();
    static Engine* instance() { return s_instance; }
    static Engine* create(QQmlEngine*, QJSEngine*);

    bool ready() const { return m_ready; }
    int libraryState() const { return m_libState; }
    int wardrobeState() const { return m_wardrobeState.load(); }
    bool needsEs() const { return m_needsEs; }
#ifdef GB_RELEASE
    bool isRelease() const { return true; }
    QString editionBadge() const { return {}; }
#else
    bool isRelease() const { return false; }
    QString editionBadge() const { return QString::fromUtf8("сборка разработчика"); }
#endif
    bool ageOk() const { return m_settings.value(QStringLiteral("age18")).toString() == QLatin1String("true"); }
    void setAgeOk(bool v) { m_settings.setValue(QStringLiteral("age18"), v ? QStringLiteral("true") : QStringLiteral("false")); emit ageChanged(); }
    bool patchInstalled() const { return m_es.ready() && m_es.hentaiPatch(); }
    bool patchBundled() const { return m_es.ready() && m_es.hentaiPatchBundled(); }
    // the setup screen: the game Steam has here ("" = none), and «this is the folder» (url or path) -> "" ok / what is wrong
    Q_INVOKABLE QString detectEs() const;
    Q_INVOKABLE QString useEsRoot(const QString& folder);
    // the «18+» folder: everything of the Workshop 18+ patch - [{id, name, card, have}] and the game's «body» sprites
    Q_INVOKABLE QVariantList patchImages() const;
    Q_INVOKABLE QVariantList patchSprites() const;

    // ---- the installer (GenryBL.exe --install from GenryBL_Setup.exe) and the uninstaller (--uninstall)
    QString mode() const;
    Q_INVOKABLE QString defaultInstallDir() const;
    Q_INVOKABLE bool esFolderOk(const QString& folder) const;
    bool esArt() const { return m_es.ready(); }
    Q_INVOKABLE QString loadEsArt(const QString& folder);                  // "" ok / what is wrong; the game's pictures, fonts, music
    Q_INVOKABLE QVariantMap installCheck(const QString& folder) const;     // {ok, error, dir, needMB, freeMB, existing}
    Q_INVOKABLE void install(const QString& folder, const QString& esRoot, bool desktop, bool startMenu);   // -> installProgress / installFinished
    Q_INVOKABLE bool launchInstalled(const QString& folder);
    Q_INVOKABLE void uninstall(bool removeProjects);                      // -> installFinished
    // Updater.cpp: the newest GenryBL on GitHub (releases/latest) or in the Steam Workshop item (Steam keeps it fresh);
    // startUpdate runs that installer over this very folder (mods and settings stay) and restarts the program
    QVariantMap updateInfo() const { return m_update; }
    Q_INVOKABLE void checkUpdates(bool manual);
    Q_INVOKABLE void startUpdate();
    QString startupError() const { return m_startupError; }
    bool busy() const { return m_busy; }
    QString busyText() const { return m_busyText; }
    bool gameRunning() const { return m_gamePid != 0; }
    QVariantList projects() const { return m_projects; }
    QString currentProject() const { return m_current; }
    QString currentProjectName() const;
    QVariantList commands() const;
    QStringList categories() const;
    QVariantList cast() const;
    QVariantList backgrounds() const;
    QVariantList cgs() const;
    QVariantList music() const;
    QVariantList sounds() const;
    QVariantList ambience() const;
    QString appRoot() const { return m_root; }
    QString esRoot() const { return m_es.esRoot(); }
    QString version() const { return QStringLiteral("V1.0.2"); }

    // ---- story tools ----
    Q_INVOKABLE QString compile(const QString& text) const;
    Q_INVOKABLE QVariantList lint(const QString& text) const;
    Q_INVOKABLE QVariantMap sceneInfo(const QString& text, int line) const;
    // choiceHover: which option of a 7DL picture menu is drawn lit
    Q_INVOKABLE QString previewUrl(const QString& text, int line, const QString& extra = QString(), int choiceHover = 0);
    Q_INVOKABLE QVariantList lineStarts(const QString& text) const;
    Q_INVOKABLE int lineAt(const QString& text, int pos) const;
    Q_INVOKABLE QStringList sceneNames(const QString& text) const;
    Q_INVOKABLE QStringList spriteNames(const QString& tag) const;          // "smile pioneer", ...
    Q_INVOKABLE QStringList spriteEmotions(const QString& tag) const;       // unique emotion words
    Q_INVOKABLE QStringList spriteOutfits(const QString& tag) const;
    Q_INVOKABLE QVariantMap suggest(const QString& lineText, int col, const QString& fullText) const;

    // ---- projects ----
    Q_INVOKABLE void refreshProjects();
    Q_INVOKABLE QString createProject(const QString& name);
    Q_INVOKABLE bool openProject(const QString& id);
    Q_INVOKABLE QString loadStory(const QString& id) const;
    Q_INVOKABLE bool saveStory(const QString& id, const QString& text);
    Q_INVOKABLE bool renameProject(const QString& id, const QString& name);
    Q_INVOKABLE QString duplicateProject(const QString& id);
    Q_INVOKABLE bool trashProject(const QString& id);
    Q_INVOKABLE QString projectDir(const QString& id) const;
    Q_INVOKABLE QString coverUrl(const QString& id) const;
    // kind: "bg" | "cg" | "sprite"; name: Ren'Py image name (for sprites "genry_x smile")
    Q_INVOKABLE QString importImage(const QString& fileUrl, const QString& kind, const QString& name);
    Q_INVOKABLE QString importAudio(const QString& fileUrl);
    // voice lines: every file its own name ("audio/001_2.ogg"), converted to ogg in order -> audioImported
    Q_INVOKABLE QStringList importAudioFiles(const QVariantList& fileUrls);
    Q_INVOKABLE QVariantList storySpeakers(const QString& text) const;      // [{name, color}]: story's own first, then ES cast
    // the mod's line in ES «Моды и пользовательские сценарии»: {name, font, color, size, style}
    Q_INVOKABLE QVariantMap modTitle(const QString& text) const;
    Q_INVOKABLE QString applyModTitle(const QString& text, const QVariantMap& title) const;   // story with the @mod_* lines set
    Q_INVOKABLE QVariantList modFonts();                                    // [{ref, label, family}]: the game's + the project's
    Q_INVOKABLE QString importFont(const QString& fileUrl);                 // -> "fonts/x.ttf"
    Q_INVOKABLE QString modsListUrl(const QVariantMap& title);             // image of the ES mods page
    Q_INVOKABLE QVariantList customImages() const;

    // ---- command forms (data/forms.json): the palette, fine-tuning windows, lines <-> values ----
    const gb::Forms& forms() const { return m_forms; }
    Q_INVOKABLE QVariantMap commandForm(const QString& id) const { return m_forms.form(id); }
    Q_INVOKABLE QVariantMap formDefaults(const QString& id, const QVariantMap& preset = {}) const { return m_forms.defaults(id, preset); }
    Q_INVOKABLE QString buildCommand(const QString& id, const QVariantMap& values) const { return m_forms.build(id, values); }
    Q_INVOKABLE QVariantMap parseCommand(const QString& line) const { return m_forms.parse(line); }   // {id, values} | {}
    // «пиши как сценарий»: pasted text -> story lines {lines, notes: [{line, warn, text}]}; one play line -> its commands
    Q_INVOKABLE QVariantMap convertScreenplay(const QString& text, bool expand) const;
    Q_INVOKABLE QVariantMap expandScreenplayLine(const QString& storyText, int line) const;   // line 1-based; {lines: [] = not a play line}
    Q_INVOKABLE QString screenplayKind(const QString& lineText) const;
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE QVariantMap storyNames(const QString& text) const;         // {scenes, vars, meters, items}
    Q_INVOKABLE QVariantList videos() const;                               // the game's video/ + the project's
    Q_INVOKABLE QVariantList projectAudio() const;                         // [{path: "audio/x.ogg", title}]
    Q_INVOKABLE QVariantList projectFiles(const QString& subdir) const;    // [{path: "images/x.png", title, url}]
    Q_INVOKABLE QString importFile(const QString& fileUrl, const QString& subdir);   // -> "images/x.png"
    Q_INVOKABLE QString importVideo(const QString& fileUrl);               // -> "video/x.webm" (mp4 converted)
    Q_INVOKABLE QVariantList mapZones() const;                             // [{id, title, x1, y1, x2, y2}] of the camp map
    Q_INVOKABLE QVariantList chibis() const;                               // [{id, name, icon}] (icon: file url)

    // ---- the Workshop as an asset library (src/core/Library): pictures of every installed mod ----
    Q_INVOKABLE void libraryScan();                                        // once, in the background -> libraryChanged
    Q_INVOKABLE QVariantList libraryItems() const;                         // [{id, title, count}]
    Q_INVOKABLE QVariantMap libraryList(const QString& id, const QString& folder, const QString& query, int limit = 600) const;
    Q_INVOKABLE QString libraryImport(const QString& ref, const QString& kind, const QString& name);   // -> image name in the project
    // «гардероб мастерской» (src/core/Wardrobe): workshop outfits on the game's bodies
    Q_INVOKABLE QVariantList wardrobeOutfits(const QString& tag) const;    // [{id, adult, body, dists, source, title}]
    Q_INVOKABLE QStringList wardrobeLooks(const QString& tag, const QString& outfit, const QString& dist = QString()) const;   // "smile nude", ...
    // «удалить навсегда» (work/wardrobe_hidden.txt): a sprite, an emotion of every outfit, a whole outfit - and back
    Q_INVOKABLE void hideSprite(const QString& tag, const QString& name);      // name as the grid shows it: "smile nude"
    Q_INVOKABLE void hideEmotion(const QString& tag, const QString& emotion);
    Q_INVOKABLE void hideOutfit(const QString& tag, const QString& outfit);
    Q_INVOKABLE QVariantList hiddenSprites(const QString& tag) const;          // [{key, label}]
    Q_INVOKABLE void unhideSprite(const QString& key);
    // «поправить лицо» (work/wardrobe_faces.txt): the face layers of a wardrobe sprite moved by hand
    Q_INVOKABLE bool wardrobeKnows(const QString& image) const;               // a sprite the wardrobe puts together
    Q_INVOKABLE QVariantMap faceShiftOf(const QString& tag, const QString& name, const QString& dist) const;   // {dx, dy, scope}
    // scope: look (this sprite) | outfit (every emotion of the outfit) | face (this emotion in every outfit); 0,0 = remove
    Q_INVOKABLE void setFaceShift(const QString& tag, const QString& name, const QString& dist, const QString& scope, int dx, int dy);
    Q_INVOKABLE QString faceFixUrl(const QString& image, int dx, int dy, bool head) const;   // a try before saving

    // ---- «кино-режим» (src/core/Cinema): the mod plays inside the constructor ----
    // {kind: say|choice|note|card|timed|video|end, line, frame, speaker, color, text, typed, whatColor, options,
    //  optionOk, seconds, music, musicKey, ambience, ambienceKey, sounds, popups: [{title, text}], video, moment, note}
    Q_INVOKABLE QVariantMap cinemaStart(const QString& text, int line);
    Q_INVOKABLE QVariantMap cinemaNext(int option = -1);

    // ---- build / run ----
    Q_INVOKABLE void play(const QString& id, const QString& text, int line);
    Q_INVOKABLE void stopGame();
    Q_INVOKABLE void engineCheck(const QString& id, const QString& text);     // ES's own Ren'Py lint
    // «Экспорт»: build -> the game's own lint (also makes the .rpyc) -> every file the mod needs is inside it ->
    // "zip" = the archive for players, "workshop" = the folder for the game's Workshop uploader (+ preview.jpg),
    // both in Documents\GenryBL -> exportFinished; the file is shown selected in Explorer
    Q_INVOKABLE void exportMod(const QString& id, const QString& text, const QString& kind);
    Q_INVOKABLE void revealFile(const QString& path) const;
    Q_INVOKABLE QString exportDir() const;
    Q_INVOKABLE void openFolder(const QString& path) const;
    Q_INVOKABLE QString audioUrl(const QString& gamePath) const;
    Q_INVOKABLE QString font() const { return m_renderer.fontFamily(); }
    Q_INVOKABLE QVariant setting(const QString& key, const QVariant& def = QVariant()) const;
    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);

    QImage providerImage(const QString& id, const QSize& requested);

signals:
    void readyChanged();
    void busyChanged();
    void gameRunningChanged();
    void projectsChanged();
    void currentProjectChanged();
    void assetsChanged();
    void libraryChanged();
    void wardrobeChanged();
    void ageChanged();
    void installProgress(int done, int total, const QString& file);
    void installFinished(bool ok, const QString& message);
    void esArtChanged();
    void toast(const QString& text, int level);
    void buildFinished(bool ok, const QString& message);
    void engineCheckFinished(bool clean, const QStringList& lines);
    void audioImported(const QStringList& rels, bool ok);
    void updateChanged();
    void exportFinished(bool ok, const QString& message, const QString& path);

private:
    explicit Engine(QObject* parent = nullptr);
    bool start(const QString& esRoot);
    bool m_needsEs = false;
    void setBusy(bool b, const QString& text = QString());
    QString ffmpegPath() const;
    QString fontFamilyFor(const QString& ref);          // registers the font once ("es:fonts/x.ttf" | "fonts/x.ttf")
    QHash<QString, QString> m_fontFamilies;
    QMutex m_fontMx;
    gb::BuildEnv envFor(const QString& id) const;
    gb::CompileOptions options() const;
    gb::SceneState coverScene(const QString& id) const;
    QString assetsDir(const QString& id) const;
    QString labelAtLine(const QString& text, int line) const;

    static Engine* s_instance;
    QString m_root;
    QVariantMap m_update;
    QNetworkAccessManager* m_net = nullptr;
    QString m_updUrl, m_updSums, m_updLocal;            // GitHub setup + SHA256SUMS.txt / the Workshop's setup
    qint64 m_updSize = 0;
    void setUpdate(const QString& state, const QString& text, double progress = 0);
    void runUpdater(const QString& setup);
    bool m_ready = false;
    QString m_startupError;
    bool m_busy = false;
    QString m_busyText;
    QVariantList m_projects;
    QString m_current;
    gb::EsAssets m_es;
    gb::Forms m_forms;
    gb::Library m_library;
    int m_libState = 0;
    gb::Wardrobe m_wardrobe;
    // «удалить навсегда» / «поправить лицо»: the lists that ship with the program (data/, read-only here) and the
    // user's own (listDir()); the wardrobe gets both
    QSet<QString> m_hidden, m_hiddenShipped;
    QHash<QString, QPoint> m_faceShifts, m_faceShiftsShipped;
    QString listDir() const;
    void applyFaceShifts();
    mutable int m_fixNonce = 0;
    void setHidden(const QSet<QString>& keys, const QString& toast);
    bool spriteHidden(const QString& tag, const QString& name) const;
    gb::Cinema m_cinema;
    QVariantMap cinemaMap(const gb::CinemaStop& c);
    std::atomic<int> m_wardrobeState{0};
    void startWardrobe();
    void waitForWardrobe(const std::function<void(const QString&)>& log) const;   // worker threads: a build right after start
    QString saveProjectImage(const QImage& img, const QString& kind, const QString& name);   // assets/images/<name>.png
    gb::Renderer m_renderer;
    mutable QSettings m_settings;
    QMutex m_sceneMx;
    QHash<int, gb::SceneState> m_scenes;
    int m_sceneKey = 0;
    qint64 m_gamePid = 0;
    QTimer m_watch;
};
