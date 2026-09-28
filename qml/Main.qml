import QtQuick
import QtQuick.Controls
import QtQuick.Window
import GenryBL

ApplicationWindow {
    id: win
    width: 1600
    height: 900
    minimumWidth: 1180
    minimumHeight: 700
    visible: true
    color: Theme.bg
    // a developer build says so in the title (the public one does not)
    readonly property string edition: Engine.editionBadge ? "  ·  " + Engine.editionBadge : ""
    title: Engine.mode === "install" ? "Установка GenryBL" : Engine.mode === "uninstall" ? "Удаление GenryBL" : (Engine.currentProject && stack.depth && stack.currentItem && stack.currentItem.objectName === "editor"
            ? Engine.currentProjectName + " — GenryBL " + Engine.version
            : "GenryBL " + Engine.version + " — конструктор модов «Бесконечного лета»") + edition

    // --shot mode: park the window off-screen so the user's desktop is never touched
    Component.onCompleted: {
        if (Engine.mode !== "") { width = 1280; height = 720 }
        if (shotPage) { x = -4000; y = -4000; width = Engine.mode !== "" ? 1280 : 1600; height = Engine.mode !== "" ? 720 : 900 }
        if (!Engine.ready) return
        win.start()
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

    function openEditor(id, animated) {
        if (!Engine.openProject(id)) return
        stack.replace(null, editorComp, { projectId: id }, animated === false ? StackView.Immediate : StackView.Transition)
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
        Editor { objectName: "editor"; onBack: win.openLauncher("mods") }
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
            Text { text: "GenryBL не может стартовать"; color: Theme.text; font.pixelSize: 28; font.bold: true }
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
    Connections {
        target: Engine
        function onToast(text, level) { toasts.show(text, level) }
        function onBuildFinished(ok, message) {
            toasts.show(message, ok ? 0 : 2)
            if (ok) Music.stopTheme()     // the game plays its own music
        }
    }
}
