import QtQuick
import QtQuick.Controls
import QtQuick.Window
import GenryBL

ApplicationWindow {
    id: win
    width: 1600
    height: 900
    // a 1920x1080 laptop at 150 % Windows zoom leaves ~1280x688 for a window: the old 1180x700 minimum stuck out
    minimumWidth: 1000
    minimumHeight: 600
    visible: true
    color: Theme.bg
    // a developer build says so in the title (the public one does not)
    readonly property string edition: Engine.editionBadge ? "  ·  " + Engine.editionBadge : ""
    title: Engine.mode === "install" ? qsTr("Установка GenryBL") : Engine.mode === "uninstall" ? qsTr("Удаление GenryBL") : (Engine.currentProject && stack.depth && stack.currentItem && stack.currentItem.objectName === "editor"
            ? Engine.currentProjectName + " — GenryBL " + Engine.version
            : "GenryBL " + Engine.version + qsTr(" — конструктор модов «Бесконечного лета»")) + edition

    // --shot mode: park the window off-screen so the user's desktop is never touched
    Component.onCompleted: {
        if (Engine.mode !== "") { width = 1280; height = 720 }
        if (shotPage) {
            x = -4000; y = -4000; width = Engine.mode !== "" ? 1280 : 1600; height = Engine.mode !== "" ? 720 : 900
            if (shotSize) { const wh = shotSize.split("x"); width = parseInt(wh[0]); height = parseInt(wh[1]) }
        }
        if (!shotPage && Engine.mode === "") applyWindowSize(Engine.setting("windowSize", ""))
        if (!Engine.ready) return
        win.start()
    }
    // «Окно» (Инструменты): "" = auto (1600x900, the whole screen when that does not fit), "WxH", "full"
    function applyWindowSize(v) {
        const aw = Screen.desktopAvailableWidth, ah = Screen.desktopAvailableHeight
        let w = 1600, h = 900
        if (v && v !== "full") { const wh = v.split("x"); w = parseInt(wh[0]); h = parseInt(wh[1]) }
        if (v === "full" || w > aw || h > ah - 32) { win.visibility = Window.Maximized; return }
        if (win.visibility === Window.Maximized || win.visibility === Window.FullScreen) win.visibility = Window.Windowed
        win.width = w
        win.height = h
        win.x = Screen.virtualX + Math.round((aw - w) / 2)
        win.y = Screen.virtualY + Math.max(0, Math.round((ah - h) / 2))
    }
    function start() {
        if (!Engine.ageOk && (!shotPage || shotPage === "age")) ageGate.open()
        if (shotPage.indexOf("editor") === 0) {
            const p = Engine.projects.length ? Engine.projects[0].id : Engine.createProject("Проверка")
            openEditor(p, false)
        } else {
            const screens = { "projects": "mods", "gallery": "gallery", "settings": "settings" }
            stack.push(launcherComp, { intro: shotPage === "" || shotPage === "launcher", startScreen: screens[shotPage] || "" })
        }
    }

    function openEditor(id, animated, line, find) {
        if (!Engine.openProject(id)) return
        stack.replace(null, editorComp, { projectId: id, startLine: line || 0, startFind: find || "" }, animated === false ? StackView.Immediate : StackView.Transition)
    }
    function openLauncher(panel) {
        stack.replace(null, launcherComp, { intro: false, startScreen: panel || "" })
    }

    Component {
        id: launcherComp
        Launcher {
            onOpenEditor: (id) => win.openEditor(id)
            onPlayProject: (id) => { Engine.openProject(id); Engine.play(id, Engine.loadStory(id), 1) }
        }
    }
    Component {
        id: editorComp
        Editor { objectName: "editor"; onBack: win.openLauncher("mods"); onOpenOther: (id, line, find) => win.openEditor(id, true, line, find) }
    }

    StackView {
        id: stack
        anchors.fill: parent
        replaceEnter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 420; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale"; from: 1.04; to: 1; duration: 520; easing.type: Easing.OutCubic }
            }
        }
        replaceExit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 320 }
                NumberAnimation { property: "scale"; from: 1; to: 0.97; duration: 320 }
            }
        }
    }

    // GenryBL_Setup.exe started us as the installer (or «Programs and Features» as the uninstaller)
    Loader {
        anchors.fill: parent
        z: 45
        active: Engine.mode !== ""
        sourceComponent: InstallerView {}
    }
    // the first start: the game was not found through Steam - the user shows the folder
    Loader {
        anchors.fill: parent
        z: 40
        active: Engine.needsEs
        sourceComponent: SetupScreen {}
    }
    Connections {
        target: Engine
        function onReadyChanged() { if (Engine.ready && stack.depth === 0) win.start() }
    }
    AgeGate { id: ageGate }

    // startup failure (no data folder, a broken game)
    Rectangle {
        anchors.fill: parent
        visible: !Engine.ready && !Engine.needsEs && Engine.mode === ""
        color: Theme.bg
        Column {
            anchors.centerIn: parent
            spacing: 12
            Text { text: qsTr("GenryBL не может стартовать"); color: Theme.text; font.pixelSize: 28; font.bold: true }
            Text { text: Engine.startupError; color: Theme.bad; font.pixelSize: 18 }
        }
    }

    // build progress pill
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: Engine.busy ? 14 : -60
        z: 50
        width: busyRow.implicitWidth + 36
        height: 44
        radius: 22
        color: Theme.panel2
        border.color: Theme.accent
        Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        Row {
            id: busyRow
            anchors.centerIn: parent
            spacing: 12
            BusyIndicator { width: 26; height: 26; running: Engine.busy; anchors.verticalCenter: parent.verticalCenter }
            Text { text: Engine.busyText; color: Theme.text; font.family: Theme.ui; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
        }
    }

    Toasts { id: toasts; anchors.fill: parent; z: 60 }
    // «Играть» -> the game fell: where, in the writer's words; «К строке» opens that line (the editor, or the project)
    CrashDialog {
        id: crashDialog
        onGotoLine: (project, line) => {
            const ed = stack.currentItem
            if (ed && ed.objectName === "editor" && ed.projectId === project) ed.gotoStoryLine(line)
            else win.openEditor(project, true, line)
        }
    }
    Connections {
        target: Engine
        function onGameCrashed(c) { crashDialog.show(c) }
        function onToast(text, level) { toasts.show(text, level) }
        function onBuildFinished(ok, message) {
            toasts.show(message, ok ? 0 : 2)
            if (ok) Music.stopTheme()     // the game plays its own music
        }
    }
}
