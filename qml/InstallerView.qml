import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import GenryBL

// The installer (GenryBL.exe --install, started by GenryBL_Setup.exe) and the uninstaller
// (--uninstall, from «Programs and Features») in Everlasting Summer's own clothes, read from the
// game's archive: the INFORMATION board for a moment, then the notebook of the game's settings
// screen - the steps are written on its sheet, «ты точно взрослый?» is the game's own «are you
// sure» plate, the progress is its volume bar, the owl watches. Until the game is found (no Steam
// entry) a summer evening drawn by code asks where it is.
//   0 18+  1 where  2 the game  3 copying  4 done
Item {
    id: inst
    property int step: 0
    property string dir: Engine.defaultInstallDir()
    property string esRoot: Engine.detectEs()
    property bool desktop: true
    property bool startMenu: true
    property real progress: 0
    property string current: ""
    property string error: ""
    property string doneText: ""
    property bool removeProjects: false
    property bool askAge: false                 // the «are you sure» plate is up
    property bool intro: true                   // the INFORMATION board first
    property bool silent: false
    readonly property bool uninstalling: Engine.mode === "uninstall"
    readonly property var check: Engine.installCheck(dir)
    readonly property color ink: "#4d2e19"      // ES settings_text
    readonly property color red: "#b3261e"
    AgeGate { id: words }                       // the same words as the program's 18+ window

    function go(n) { error = ""; step = n; Sfx.click() }
    function startInstall() {
        step = 3
        progress = 0
        Engine.install(dir, esRoot, desktop, startMenu)
    }
    Connections {
        target: Engine
        function onInstallProgress(done, total, file) { inst.progress = done / Math.max(1, total); inst.current = file }
        function onInstallFinished(ok, message) {
            if (inst.silent) { console.warn("INSTALL " + (ok ? "OK " : "FAIL ") + message); Qt.exit(ok ? 0 : 2); return }
            if (ok) { inst.doneText = message; inst.step = 4 }
            else { inst.error = message; inst.step = inst.uninstalling ? 0 : 1 }
        }
        function onEsArtChanged() { if (Engine.esArt) Music.playTheme("sound/music/blow_with_the_fires.ogg", 0) }
    }
    // --silent-to <dir> [--es <game>] [--no-shortcuts]: install without questions (the release self-check);
    // --uninstall --silent: remove without questions (the mods stay)
    Component.onCompleted: {
        const a = Qt.application.arguments
        if (a.indexOf("--silent-to") >= 0 || (uninstalling && a.indexOf("--silent") >= 0)) {
            silent = true
            if (uninstalling) { Engine.uninstall(a.indexOf("--remove-mods") >= 0); return }
            dir = a[a.indexOf("--silent-to") + 1]
            if (a.indexOf("--es") >= 0) esRoot = a[a.indexOf("--es") + 1]
            desktop = startMenu = a.indexOf("--no-shortcuts") < 0
            startInstall()
            return
        }
        if (Engine.esArt && !shotPage) Music.playTheme("sound/music/blow_with_the_fires.ogg", 0)
        const pages = { "install-0": 0, "install-1": 1, "install-2": 2, "install-3": 3, "install-4": 4, "install-age": 0 }
        if (pages[shotPage] !== undefined) {
            intro = false
            step = pages[shotPage]
            askAge = shotPage === "install-age"
            if (step === 3) { progress = 0.62; current = "app/Qt6Quick.dll" }
        }
    }

    Rectangle { anchors.fill: parent; color: "black" }

    // ---------------------------------------------------------------- no game yet: where is it?
    Loader {
        anchors.fill: parent
        active: !Engine.esArt
        sourceComponent: Item {
            SummerBackdrop { anchors.fill: parent }
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 80, 860)
                height: findCol.implicitHeight + 64
                radius: 20
                color: "#e0111a15"
                border.color: "#66ffdd7d"
                ColumnLayout {
                    id: findCol
                    x: 36; y: 32
                    width: parent.width - 72
                    spacing: 14
                    Text { text: "GenryBL"; color: Theme.gold; font.family: Theme.riffic; font.pixelSize: 56; font.bold: true }
                    Text { text: "Где у тебя стоит «Бесконечное лето»?"; color: Theme.text; font.family: Theme.ui; font.pixelSize: 26; font.bold: true }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: "Через Steam не нашёл. Steam → Библиотека → Бесконечное лето → ⚙ → Управление → Просмотреть локальные файлы — вот эта папка (там Everlasting Summer.exe)."
                        color: Theme.dim; font.family: Theme.ui; font.pixelSize: 16
                    }
                    Text { visible: inst.error !== ""; text: "⚠ " + inst.error; color: Theme.bad; font.family: Theme.ui; font.pixelSize: 16; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    RowLayout {
                        PillButton { accent: true; text: "Выбрать папку…"; implicitHeight: 42; onClicked: esDialog.open() }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "<a href='steam://install/331470'>Нет игры? Она бесплатная в Steam</a>"
                            textFormat: Text.RichText
                            color: Theme.dim; linkColor: Theme.gold
                            font.family: Theme.ui; font.pixelSize: 15
                            onLinkActivated: (l) => Qt.openUrlExternally(l)
                        }
                        PillButton { dark: true; text: "Выйти"; onClicked: Qt.quit() }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- the game's own look
    Item {
        id: stage
        visible: Engine.esArt
        width: 1920; height: 1080
        readonly property real s: Math.min(inst.width / 1920, inst.height / 1080)
        scale: s
        transformOrigin: Item.TopLeft
        x: (inst.width - 1920 * s) / 2
        y: (inst.height - 1080 * s) / 2

        // ES link: corbel, ink; hover = pioneer red + the settings leaf in front
        component EsLink: Item {
            id: link
            property string text
            property int size: 40
            property bool enabledLink: true
            signal clicked()
            width: row.implicitWidth; height: size + 14
            opacity: enabledLink ? 1 : 0.35
            Row {
                id: row
                spacing: 10
                anchors.verticalCenter: parent.verticalCenter
                Image {
                    source: Engine.esArt ? "image://gb/file/images/gui/settings/leaf.png" : ""
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: area.containsMouse && link.enabledLink ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                }
                Text {
                    text: link.text
                    color: area.containsMouse && link.enabledLink ? inst.red : inst.ink
                    font.family: Theme.riffic; font.pixelSize: link.size; font.bold: true
                    Behavior on color { ColorAnimation { duration: 140 } }
                }
            }
            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: link.enabledLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: if (link.enabledLink) link.clicked()
            }
        }
        // a check on paper: an ink box, the settings leaf as the tick
        component EsCheck: Item {
            id: ec
            property string text
            property bool checked
            signal toggled(bool on)
            width: 720; height: 52
            Rectangle {
                y: 10; width: 32; height: 32; radius: 4
                color: "transparent"
                border.color: inst.ink; border.width: 3
                Image { anchors.centerIn: parent; source: Engine.esArt ? "image://gb/file/images/gui/settings/leaf.png" : ""; visible: ec.checked; scale: 1.1 }
            }
            Text { x: 50; y: 8; text: ec.text; color: inst.ink; font.family: Theme.ui; font.pixelSize: 30 }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); ec.toggled(!ec.checked) } }
        }
        // a path written on the paper's line
        component EsPath: Item {
            id: ep
            property alias text: field.text
            signal edited(string text)
            signal browse()
            width: 800; height: 58
            TextField {
                id: field
                width: 620; height: 52
                color: inst.ink
                font.family: Theme.mono; font.pixelSize: 24
                selectByMouse: true
                onEditingFinished: ep.edited(text)
                background: Rectangle { color: "transparent"; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: inst.ink; opacity: 0.6 } }
            }
            EsLink { x: 640; text: "Обзор…"; size: 32; onClicked: ep.browse() }
        }

        // the notebook of the settings screen
        Image {
            anchors.fill: parent
            source: Engine.esArt ? "image://gb/file/images/gui/settings/preferences_bg.jpg" : ""
        }
        // the owl watches from the notebook's corner
        Image {
            x: 1480; y: 780
            width: 250; height: 250
            fillMode: Image.PreserveAspectFit
            source: Engine.esArt ? "image://gb/file/images/gui/title_menu/" + (owlArea.containsMouse || inst.step === 4 ? "owl_hover.png" : "owl_idle.png") : ""
            MouseArea { id: owlArea; anchors.fill: parent; hoverEnabled: true }
        }

        // the sheet: x 522..1400, y 213..996
        Item {
            id: sheet
            x: 560; y: 236
            width: 800; height: 740

            // the title and the steps, like the game's settings menu
            Text { x: 0; y: 0; text: inst.uninstalling ? "Удаление GenryBL" : "Установка GenryBL"; color: inst.ink; font.family: Theme.riffic; font.pixelSize: 62; font.bold: true }
            // a red «18+» stamp on the corner of the sheet
            Rectangle {
                x: 690; y: -6
                width: 104; height: 104; radius: 52
                color: "transparent"
                border.color: inst.red; border.width: 5
                rotation: -12
                opacity: 0.85
                Text { anchors.centerIn: parent; text: "18+"; color: inst.red; font.family: Theme.riffic; font.pixelSize: 42; font.bold: true }
            }
            Row {
                visible: !inst.uninstalling
                y: 82
                spacing: 18
                Repeater {
                    model: ["18+", "Куда", "Игра", "Установка", "Готово"]
                    Row {
                        required property int index
                        required property var modelData
                        spacing: 4
                        Image {
                            source: Engine.esArt ? "image://gb/file/images/gui/settings/leaf.png" : ""
                            anchors.verticalCenter: parent.verticalCenter
                            opacity: index === inst.step ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 200 } }
                        }
                        Text {
                            text: modelData
                            color: index === inst.step ? inst.red : inst.ink
                            opacity: index <= inst.step ? 1 : 0.45
                            font.family: Theme.riffic; font.pixelSize: 26; font.bold: index === inst.step
                        }
                    }
                }
            }
            Rectangle { y: 128; width: 800; height: 2; color: inst.ink; opacity: 0.25 }

            // the pages: they slide in on the sheet
            Item {
                id: pages
                y: 150
                width: 800; height: 590
                clip: true
                component Page: Item {
                    required property int n
                    width: pages.width; height: pages.height
                    x: (n - inst.step) * 120
                    opacity: n === inst.step ? 1 : 0
                    visible: opacity > 0.01
                    Behavior on x { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
                    Behavior on opacity { NumberAnimation { duration: 320 } }
                }

                // ---- 0: 18+ and who answers (or the uninstaller)
                Page {
                    n: 0
                    Flickable {
                        visible: !inst.uninstalling
                        width: 800; height: 470
                        contentHeight: agreeText.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        Text {
                            id: agreeText
                            width: 780
                            wrapMode: Text.Wrap
                            text: words.words + "\n\n— Генри"
                            color: inst.ink; font.family: Theme.ui; font.pixelSize: 22; lineHeight: 1.05
                        }
                    }
                    Column {
                        visible: inst.uninstalling
                        spacing: 18
                        Text {
                            width: 780
                            wrapMode: Text.Wrap
                            text: "Программа, её настройки и кэш уйдут. Твои моды (папка projects) останутся, если не отметишь ниже."
                            color: inst.ink; font.family: Theme.ui; font.pixelSize: 28
                        }
                        EsCheck { text: "удалить и мои моды тоже"; checked: inst.removeProjects; onToggled: (on) => inst.removeProjects = on }
                    }
                    Text {
                        visible: inst.error !== ""
                        y: 470
                        width: 780; wrapMode: Text.Wrap
                        text: inst.error; color: inst.red; font.family: Theme.ui; font.pixelSize: 22
                    }
                    Row {
                        y: 520
                        spacing: 60
                        EsLink { text: inst.uninstalling ? "Не удалять" : "Выйти"; onClicked: Qt.quit() }
                        EsLink {
                            text: inst.uninstalling ? "Удалить" : "Мне есть 18 →"
                            onClicked: { Sfx.click(); if (inst.uninstalling) Engine.uninstall(inst.removeProjects); else inst.askAge = true }
                        }
                    }
                }

                // ---- 1: where
                Page {
                    n: 1
                    Column {
                        spacing: 16
                        Text { text: "Куда поставить?"; color: inst.ink; font.family: Theme.riffic; font.pixelSize: 40; font.bold: true }
                        Text {
                            width: 780; wrapMode: Text.Wrap
                            text: "Всё будет лежать в одной папке: программа, её данные и твои моды. Админ не нужен."
                            color: inst.ink; font.family: Theme.ui; font.pixelSize: 24
                        }
                        EsPath { text: inst.dir; onEdited: (t) => inst.dir = t; onBrowse: dirDialog.open() }
                        Text {
                            width: 780; wrapMode: Text.Wrap
                            text: inst.check.ok ? "Нужно " + inst.check.needMB + " МБ, свободно " + inst.check.freeMB + " МБ" +
                                                  (inst.check.existing ? ". Тут уже есть GenryBL — обновлю, моды и настройки не трону." : ".")
                                                : inst.check.error
                            color: inst.check.ok ? "#3f6b1f" : inst.red; font.family: Theme.ui; font.pixelSize: 22
                        }
                        Text { visible: inst.error !== ""; width: 780; wrapMode: Text.Wrap; text: inst.error; color: inst.red; font.family: Theme.ui; font.pixelSize: 22 }
                        EsCheck { text: "Ярлык на рабочем столе"; checked: inst.desktop; onToggled: (on) => inst.desktop = on }
                        EsCheck { text: "В меню «Пуск»"; checked: inst.startMenu; onToggled: (on) => inst.startMenu = on }
                    }
                    Row {
                        y: 520
                        spacing: 60
                        EsLink { text: "← Назад"; onClicked: inst.go(0) }
                        EsLink { text: "Дальше →"; enabledLink: inst.check.ok; onClicked: inst.go(2) }
                    }
                }

                // ---- 2: the game
                Page {
                    n: 2
                    Column {
                        spacing: 16
                        Text { text: "«Бесконечное лето»"; color: inst.ink; font.family: Theme.riffic; font.pixelSize: 40; font.bold: true }
                        Text {
                            width: 780; wrapMode: Text.Wrap
                            text: "Нашёл через Steam. Если у тебя их несколько или это не та — выбери свою папку (там лежит Everlasting Summer.exe)."
                            color: inst.ink; font.family: Theme.ui; font.pixelSize: 24
                        }
                        EsPath { text: inst.esRoot; onEdited: (t) => inst.esRoot = t; onBrowse: esDialog.open() }
                        Text {
                            text: Engine.esFolderOk(inst.esRoot) ? "✓ это оно" : "⚠ тут нет Everlasting Summer.exe и папки game"
                            color: Engine.esFolderOk(inst.esRoot) ? "#3f6b1f" : inst.red; font.family: Theme.ui; font.pixelSize: 22
                        }
                    }
                    Row {
                        y: 520
                        spacing: 60
                        EsLink { text: "← Назад"; onClicked: inst.go(1) }
                        EsLink { text: "Установить"; enabledLink: Engine.esFolderOk(inst.esRoot); onClicked: { Sfx.click(); inst.startInstall() } }
                    }
                }

                // ---- 3: copying on the game's volume bar
                Page {
                    n: 3
                    Column {
                        y: 120
                        spacing: 28
                        Text { text: "Ставлю GenryBL…"; color: inst.ink; font.family: Theme.riffic; font.pixelSize: 48; font.bold: true }
                        Item {
                            width: 780; height: 40
                            Image { width: parent.width; height: 34; source: Engine.esArt ? "image://gb/file/images/gui/settings/bar_null.png" : ""; fillMode: Image.Stretch }
                            Item {
                                width: parent.width * inst.progress; height: 34
                                clip: true
                                Behavior on width { NumberAnimation { duration: 160 } }
                                Image { width: 780; height: 34; source: Engine.esArt ? "image://gb/file/images/gui/settings/bar_full.png" : ""; fillMode: Image.Stretch }
                            }
                            Image {
                                x: Math.max(0, 780 * inst.progress - 26); y: -9
                                width: 52; height: 52
                                source: Engine.esArt ? "image://gb/file/images/gui/settings/htumb.png" : ""
                                Behavior on x { NumberAnimation { duration: 160 } }
                            }
                        }
                        Text {
                            width: 780; elide: Text.ElideMiddle
                            text: Math.round(inst.progress * 100) + "%   " + inst.current
                            color: inst.ink; font.family: Theme.mono; font.pixelSize: 22
                        }
                    }
                }

                // ---- 4: done
                Page {
                    n: 4
                    Column {
                        y: 60
                        spacing: 22
                        Text {
                            text: inst.uninstalling ? "Удалено" : "Готово!"
                            color: inst.ink; font.family: Theme.riffic; font.pixelSize: 80; font.bold: true
                            scale: inst.step === 4 ? 1 : 0.6
                            Behavior on scale { NumberAnimation { duration: 600; easing.type: Easing.OutBack } }
                        }
                        Text {
                            width: 780; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            text: inst.uninstalling ? inst.doneText
                                                    : "GenryBL стоит в " + inst.check.dir + "." + (inst.desktop ? " Ярлык — на рабочем столе." : "") +
                                                      "\nУдалить можно в «Параметры → Приложения».\n\nХороших модов, пионер."
                            color: inst.ink; font.family: Theme.ui; font.pixelSize: 26
                        }
                    }
                    Row {
                        y: 520
                        spacing: 60
                        EsLink { text: "Закрыть"; onClicked: Qt.quit() }
                        EsLink { visible: !inst.uninstalling; text: "Запустить GenryBL"; onClicked: { Engine.launchInstalled(inst.dir); Qt.quit() } }
                    }
                }
            }
        }

        // «are you sure»: the game's own plate (images/gui/o_rly/base.png, paper 516..1396 x 389..585)
        Item {
            anchors.fill: parent
            opacity: inst.askAge ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 260 } }
            MouseArea { anchors.fill: parent }
            Rectangle { anchors.fill: parent; color: "#99000000" }
            Rectangle { x: 522; y: 395; width: 868; height: 184; color: "#fbf5d0" }      // the plate's paper, opaque
            Image { anchors.fill: parent; source: Engine.esArt ? "image://gb/file/images/gui/o_rly/base.png" : "" }
            Text {
                x: 516; y: 412; width: 880
                horizontalAlignment: Text.AlignHCenter
                text: "Тебе точно есть 18?"
                color: inst.ink; font.family: Theme.riffic; font.pixelSize: 48; font.bold: true
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 500
                spacing: 160
                EsLink { text: "Да"; size: 48; onClicked: { inst.askAge = false; inst.go(1) } }
                EsLink { text: "Нет"; size: 48; onClicked: Qt.quit() }
            }
        }

        // ---- the intro: the INFORMATION board of the main menu, then into the notebook
        Item {
            anchors.fill: parent
            visible: opacity > 0.01
            opacity: inst.intro ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 900; easing.type: Easing.InOutQuad } }
            Image {
                anchors.centerIn: parent
                width: 1920; height: 1080
                source: Engine.esArt ? "image://gb/file/images/gui/title_menu/mainmenu_ground.jpg" : ""
                scale: inst.intro ? 1.14 : 1.02
                Behavior on scale { NumberAnimation { duration: 2600; easing.type: Easing.OutCubic } }
            }
            Rectangle { anchors.fill: parent; color: "#55000000" }
            Text {
                anchors.centerIn: parent
                text: "GenryBL"
                color: "#ffdd7d"
                style: Text.Raised; styleColor: "#aa000000"
                font.family: Theme.riffic; font.pixelSize: 140; font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 640
                text: "конструктор модов «Бесконечного лета»"
                color: "white"; style: Text.Raised; styleColor: "#aa000000"
                font.family: Theme.ui; font.pixelSize: 40
            }
            MouseArea { anchors.fill: parent; onClicked: inst.intro = false }
        }
        Timer { running: inst.intro && Engine.esArt; interval: 2600; onTriggered: inst.intro = false }
    }

    FolderDialog {
        id: dirDialog
        title: "Куда поставить GenryBL"
        onAccepted: inst.dir = decodeURIComponent(String(selectedFolder).replace("file:///", "")) + "/GenryBL"
    }
    FolderDialog {
        id: esDialog
        title: "Папка «Бесконечного лета» (там Everlasting Summer.exe)"
        onAccepted: {
            const f = decodeURIComponent(String(selectedFolder).replace("file:///", ""))
            if (Engine.esArt) { inst.esRoot = f; return }
            const e = Engine.loadEsArt(f)      // no game yet: this folder dresses the installer
            inst.error = e
            if (!e) { inst.esRoot = f; inst.intro = true }
        }
    }
}
