import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// The mod editor. One preview for everything: the cursor line, a hovered palette command,
// a hovered emotion or background — all go through Engine.previewUrl and THE renderer.
Item {
    id: ed
    property string projectId
    signal back()
    property string hoverExtra: ""
    property var issues: []
    property string previewSrc: ""
    property var scene: ({})
    readonly property int errors: issues.filter(i => i.level >= 2).length
    readonly property int warnings: issues.filter(i => i.level === 1).length

    Component.onCompleted: {
        code.setText(Engine.loadStory(projectId))
        issues = Engine.lint(code.text)
        code.focusEditor()
        if (shotPage.indexOf("editor") === 0) code.gotoLine(shotPage === "editor-top" || shotPage === "editor-top-hover" ? 1 : 13)
        if (shotPage === "editor-top-hover") hoverExtra = "погода снег"
        if (shotPage === "editor-hover") hoverExtra = "показать un shy pioneer center dissolve"
        const tabs = { "editor-scene": [0, 1], "editor-chars": [1, 1], "editor-bgs": [2, 2], "editor-audio": [3, 0], "editor-rpy": [0, 2] }
        if (tabs[shotPage]) { leftTabs.currentIndex = tabs[shotPage][0]; infoTabs.currentIndex = tabs[shotPage][1] }
        if (shotPage === "editor-big") big.open = true
        // «гардероб мастерской»: shotArg = "tag outfit [dist]" (the wardrobe loads in the background: --late)
        if (shotPage === "editor-wardrobe") {
            leftTabs.currentIndex = 1
            const a = (shotArg || "dv casual").split(" ")
            Qt.callLater(() => { charBrowser.tag = a[0]; charBrowser.wardrobeOpen = true; charBrowser.outfit = a[1] || ""; charBrowser.dist = a[2] || "" })
        }
        if (shotPage === "editor-play") Qt.callLater(ed.play)
        if (shotPage === "editor-18") { leftTabs.currentIndex = 2; Qt.callLater(() => bgBrowser.adult = true) }
        // «поправить лицо»: shotArg = "tag emotion outfit" (the wardrobe loads in the background: --late)
        if (shotPage === "editor-facefix") {
            leftTabs.currentIndex = 1
            const a = (shotArg || "dv smile casual").split(" ")
            facefixShot.args = a
            facefixShot.start()
        }
        // «кино-режим»: shotArg = "line [clicks [pick]]"
        if (shotPage === "editor-cinema") Qt.callLater(() => {
            const a = (shotArg || "1 0").split(" ").map(Number)
            cinemaView.begin(code.text, a[0] || 1)
            for (let i = 0; i < (a[1] || 0); ++i) { if (cinemaView.stop.kind === "choice") cinemaView.pick(a[2] || 0); else { cinemaView.shown = 999; cinemaView.advance() } }
            cinemaView.shown = 999
        })
        if (shotPage === "editor-dialogue") Qt.callLater(() => { dialogue.openFor(code.text, code.currentLine); dialogue.fillDemo() })
        if (shotPage === "editor-title") Qt.callLater(() => { titleDialog.openFor(code.text); titleDialog.name = "Лето с последствиями"; titleDialog.colorHex = "#b3001b"; titleDialog.size = 44; titleDialog.bold = true })
        if (shotPage === "editor-choice") Qt.callLater(() => { choiceWizard.openFor(code.text, code.currentLine); choiceWizard.fillDemo() })
        if (shotPage === "editor-choice-timed") Qt.callLater(() => { choiceWizard.openFor(code.text, code.currentLine); choiceWizard.fillDemo(); choiceWizard.mode = "timed"; choiceWizard.refresh() })
        if (shotPage === "editor-choice-picker") Qt.callLater(() => { choiceWizard.openFor(code.text, code.currentLine); choiceWizard.fillDemo(); choiceWizard.openPickerDemo() })
        if (shotPage === "editor-screenplay") Qt.callLater(() => { screenplay.openFor(code.text, code.currentLine, ""); screenplay.fillDemo(); screenplay.current = Number(shotArg) || 4 })
        if (shotPage === "editor-form") Qt.callLater(() => cmdForm.openNew(shotArg || "show", {}, code.text, code.currentLine))
        if (shotPage === "editor-library") Qt.callLater(() => { libraryBrowser.open(); libraryBrowser.pendingSearch = shotArg })
        if (shotPage === "editor-map") Qt.callLater(() => { mapEditor.openNew(code.text, code.currentLine); mapEditor.toggle("dining_hall"); mapEditor.setField("dining_hall", "chibi", "dv"); mapEditor.hoverId = "beach" })
        if (shotPage === "editor-form-edit") Qt.callLater(() => { code.gotoLine(Number(shotArg) || 14); ed.editLine() })
        if (shotPage === "editor-dialogue-import") Qt.callLater(() => {
            dialogue.openFor(code.text, code.currentLine)
            dialogue.importUrls(shotArg.split("|").map(p => "file:///" + p))
        })
        refreshPreview()
    }

    function refreshPreview() {
        previewSrc = Engine.previewUrl(code.text, code.currentLine, hoverExtra)
        scene = Engine.sceneInfo(code.text, code.currentLine)
    }
    function insert(cmd) { hoverExtra = ""; code.insertLine(cmd) }
    // palette row -> its window: the choice and dialogue masters, or the command's form
    function openCommand(row) {
        hoverExtra = ""
        if (row.special === "choice") choiceWizard.openFor(code.text, code.currentLine)
        else if (row.special === "dialogue") dialogue.openFor(code.text, code.currentLine)
        else if (row.special === "screenplay") screenplay.openFor(code.text, code.currentLine, "")
        else if (row.id === "map") mapEditor.openNew(code.text, code.currentLine)
        else if (!row.hasFields) insert(row.templ)
        else cmdForm.openNew(row.id, row.preset, code.text, code.currentLine)
    }
    // the line under the cursor -> its form, filled with what the line says
    readonly property var lineForm: Engine.parseCommand(code.lineText(code.currentLine))
    // «Алиса (злая, слева): …» under the cursor -> the commands it stands for
    readonly property string playKind: Engine.screenplayKind(code.lineText(code.currentLine))
    function expandLine() {
        const r = Engine.expandScreenplayLine(code.text, code.currentLine)
        if (!r.lines.length) { Engine.toast("Эта строка — не сценарная: разворачивать нечего", 1); return }
        code.replaceLine(code.currentLine, r.lines.join("
"))
        const warn = r.notes.filter(n => n.warn)
        Engine.toast(warn.length ? "⚠ " + warn[0].text : "Развернул в " + r.lines.length + " стр.", warn.length ? 1 : 0)
    }
    function editLine() {
        if (playKind !== "" && playKind !== "prose" && !lineForm.id) { expandLine(); return }
        const p = Engine.parseCommand(code.lineText(code.currentLine))
        if (!p.id) { Engine.toast("Эту строку в форме не настроить — выбери команду в палитре", 1); return }
        if (p.id === "map") mapEditor.openEdit(p.values, code.text, code.currentLine)
        else cmdForm.openEdit(p.id, p.values, code.text, code.currentLine)
    }
    function save() { Engine.saveStory(projectId, code.text) }
    function play() { Engine.play(projectId, code.text, code.currentLine) }
    // «кино-режим»: the mod plays in the constructor from the cursor line (above the first scene: from the start)
    function cinema() { ed.save(); cinemaView.begin(code.text, code.currentLine) }
    function stepLine(dir) {
        const lines = code.text.split("\n")
        let n = code.currentLine
        do { n += dir } while (n >= 1 && n <= lines.length && lines[n - 1].trim() === "")
        code.gotoLine(Math.max(1, Math.min(lines.length, n)))
    }

    Timer { id: previewTimer; interval: 40; onTriggered: ed.refreshPreview() }
    Timer { id: facefixShot; property var args: []; interval: 9000; onTriggered: charBrowser.fixFace(args[0], args.slice(1).join(" "), "") }
    Timer { id: lintTimer; interval: 300; onTriggered: ed.issues = Engine.lint(code.text) }
    Timer { id: saveTimer; interval: 900; onTriggered: ed.save() }
    onHoverExtraChanged: previewTimer.restart()
    Connections {
        target: code
        function onEdited() { previewTimer.restart(); lintTimer.restart(); saveTimer.restart() }
        function onCurrentLineChanged() { previewTimer.restart() }
    }
    Connections {
        target: Engine
        function onAssetsChanged() { previewTimer.restart(); lintTimer.restart() }
    }

    Shortcut { sequences: [StandardKey.Save]; onActivated: { ed.save(); Engine.toast("Сохранено", 0) } }
    Shortcut { sequence: "F5"; onActivated: ed.play() }
    Shortcut { sequence: "F6"; enabled: !cinemaView.opened; onActivated: ed.cinema() }
    Shortcut { sequence: "Ctrl+D"; enabled: !dialogue.opened; onActivated: dialogue.openFor(code.text, code.currentLine) }
    Shortcut { sequence: "Ctrl+E"; enabled: !cmdForm.opened; onActivated: ed.editLine() }
    Shortcut { sequence: "Ctrl+Shift+E"; enabled: !cmdForm.opened; onActivated: ed.expandLine() }
    Shortcut { sequence: "Ctrl+Shift+V"; enabled: !screenplay.opened; onActivated: screenplay.openFor(code.text, code.currentLine, Engine.clipboardText()) }
    Shortcut { sequence: "Ctrl+K"; onActivated: { leftTabs.currentIndex = 0; palette.focusSearch() } }
    Repeater {                                   // Alt+1..9: the quick inserts of the palette
        model: 9
        Item { required property int index; Shortcut { sequence: "Alt+" + (index + 1); enabled: !cmdForm.opened; onActivated: palette.runPin(index) } }
    }
    CinemaView {
        id: cinemaView
        onGotoLine: (line) => code.gotoLine(line)
        onClosed: code.focusEditor()
    }
    CommandForm {
        id: cmdForm
        onAccepted: (line, replace) => {
            if (replace) code.replaceLine(cmdForm.storyLine, line)
            else ed.insert(line)
        }
        onClosed: code.focusEditor()
    }
    LibraryBrowser {
        id: libraryBrowser
        onInsertLine: (cmd) => ed.insert(cmd)
        onClosed: code.focusEditor()
    }
    MapEditor {
        id: mapEditor
        onAccepted: (line, replace) => {
            if (replace) code.replaceLine(mapEditor.storyLine, line)
            else ed.insert(line)
        }
        onClosed: code.focusEditor()
    }
    ScreenplayDialog {
        id: screenplay
        onInsertBlock: (block) => ed.insert(block)
        onClosed: code.focusEditor()
    }
    DialogueDialog {
        id: dialogue
        onInsertBlock: (block) => ed.insert(block)
        onClosed: code.focusEditor()
    }
    ChoiceDialog {
        id: choiceWizard
        onInsertBlock: (block) => ed.insert(block)
        onClosed: code.focusEditor()
    }
    ModTitleDialog {
        id: titleDialog
        onApplied: (text) => { code.setText(text); ed.save(); ed.issues = Engine.lint(code.text); ed.refreshPreview(); Engine.toast("Название мода обновлено", 0) }
        onClosed: code.focusEditor()
    }
    Connections {
        target: Engine
        function onEngineCheckFinished(clean, lines) {
            checkDialog.clean = clean
            checkDialog.lines = lines
            checkDialog.open()
        }
    }
    Dialog {
        id: checkDialog
        property bool clean: false
        property var lines: []
        anchors.centerIn: parent
        width: Math.min(ed.width - 80, 1100)
        modal: true
        title: clean ? "Движок БЛ: мод чистый" : "Движок БЛ нашёл проблемы"
        standardButtons: Dialog.Ok
        ScrollView {
            implicitHeight: Math.min(480, report.implicitHeight + 10)
            anchors.fill: parent
            TextArea {
                id: report
                readOnly: true
                wrapMode: TextEdit.Wrap
                text: checkDialog.clean ? "Ren'Py 7.4 (lint самой игры) не нашёл ни одной ошибки в моде." : checkDialog.lines.join("\n")
                font.family: Theme.mono
                font.pixelSize: 13
            }
        }
    }

    // ---------------------------------------------------------------- top bar
    Rectangle {
        id: top
        width: parent.width
        height: 62
        color: Theme.bg2
        z: 2
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 14
            spacing: 10
            PillButton { dark: true; text: "← Меню"; onClicked: { ed.save(); ed.back() } }
            InkText {
                text: Engine.currentProjectName; size: 26; color: Theme.gold; Layout.alignment: Qt.AlignVCenter
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: titleDialog.openFor(code.text) }
            }
            PillButton {
                dark: true
                text: "Aa Название"
                onClicked: titleDialog.openFor(code.text)
                ToolTip.visible: hovered
                ToolTip.text: "Как мод выглядит в списке модов БЛ: свой шрифт, цвет, размер"
            }
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                height: 26; radius: 13
                width: chip.implicitWidth + 22
                color: ed.errors ? "#33ff5566" : ed.warnings ? "#33ffc857" : "#3357c785"
                border.color: ed.errors ? Theme.bad : ed.warnings ? Theme.warn : Theme.good
                Text {
                    id: chip
                    anchors.centerIn: parent
                    text: ed.errors ? "✖ ошибок: " + ed.errors + (ed.warnings ? " · ⚠ " + ed.warnings : "")
                                    : ed.warnings ? "⚠ предупреждений: " + ed.warnings : "✓ всё чисто"
                    color: Theme.text; font.family: Theme.ui; font.pixelSize: 13; font.bold: true
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: infoTabs.currentIndex = 0 }
            }
            Item { Layout.fillWidth: true }
            PillButton {
                readonly property bool play: ed.playKind !== "" && ed.playKind !== "prose" && !ed.lineForm.id
                dark: true
                text: play ? "⇲ Развернуть строку" : "✎ Настроить строку"
                enabled: play || !!ed.lineForm.id
                onClicked: play ? ed.expandLine() : ed.editLine()
                ToolTip.visible: hovered
                ToolTip.text: play ? "Сценарная строка → команды, которые она означает: показать …, фон …, реплика (Ctrl+Shift+E)"
                                   : "Открыть строку под курсором в её окне: все параметры, картинки, превью (Ctrl+E)"
            }
            PillButton {
                dark: true
                text: "✍ Сценарий"
                onClicked: screenplay.openFor(code.text, code.currentLine, "")
                ToolTip.visible: hovered
                ToolTip.text: "Пиши как сценарий: вставь сценарий, переписку или главу — станут фонами, спрайтами и репликами (Ctrl+Shift+V — из буфера)"
            }
            PillButton {
                dark: true
                text: "⑂ Выбор"
                onClicked: choiceWizard.openFor(code.text, code.currentLine)
                ToolTip.visible: hovered
                ToolTip.text: "Выбор и последствия: варианты с картинками как в 7ДЛ, очки, сцены веток и общая сцена после"
            }
            PillButton {
                dark: true
                text: "♪ Диалог"
                onClicked: dialogue.openFor(code.text, code.currentLine)
                ToolTip.visible: hovered
                ToolTip.text: "Диалог + озвучка: реплики таблицей, озвучка файлами по порядку, кадр игры сразу (Ctrl+D)"
            }
            PillButton {
                dark: true
                text: "Библиотека"
                onClicked: libraryBrowser.open()
                ToolTip.visible: hovered
                ToolTip.text: "Картинки всех модов из твоей мастерской БЛ: спрайты, фоны, CG — взять в свой мод"
            }
            PillButton { dark: true; text: "Папка мода"; onClicked: Engine.openFolder(Engine.projectDir(ed.projectId)) }
            PillButton {
                dark: true
                text: "Проверить движком"
                enabled: !Engine.busy
                onClicked: Engine.engineCheck(ed.projectId, code.text)
                ToolTip.visible: hovered
                ToolTip.text: "Родной lint Ren'Py самой игры: картинки, метки, синтаксис (1-2 минуты)"
            }
            PillButton { dark: true; text: "Архив мода"; enabled: !Engine.busy; onClicked: Engine.exportZip(ed.projectId, code.text) }
            PillButton { visible: Engine.gameRunning; text: "■ Стоп"; onClicked: Engine.stopGame() }
            PillButton {
                accent: true
                text: "▶ Кино  F6"
                onClicked: ed.cinema()
                ToolTip.visible: hovered
                ToolTip.text: "Кино-режим: мод играет прямо тут, как в игре — печать текста, музыка, выборы по веткам, без загрузки БЛ"
            }
            // the big one
            AbstractButton {
                id: playBtn
                implicitWidth: 168
                implicitHeight: 42
                enabled: !Engine.busy
                hoverEnabled: true
                onClicked: { Sfx.play("gui/sfx/select.ogg"); ed.play() }
                scale: pressed ? 0.96 : hovered ? 1.03 : 1
                Behavior on scale { NumberAnimation { duration: 120 } }
                background: Rectangle {
                    radius: 21
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: playBtn.enabled ? "#b5e27a" : Theme.panel3 }
                        GradientStop { position: 1; color: playBtn.enabled ? "#6fa33a" : Theme.panel3 }
                    }
                    border.color: "#ffdd7d"
                    border.width: playBtn.hovered ? 2 : 0
                }
                contentItem: Row {
                    spacing: 8
                    leftPadding: 18
                    Text { text: Engine.busy ? "…" : "▶"; color: "#16240c"; font.pixelSize: 18; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: Engine.gameRunning ? "Заново" : "Играть"; color: "#16240c"; font.family: Theme.riffic; font.pixelSize: 20; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "F5"; color: "#2e4a16"; font.family: Theme.mono; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                }
                ToolTip.visible: hovered
                ToolTip.text: "Собрать мод и открыть Бесконечное лето прямо в сцене под курсором"
            }
        }
    }

    // ---------------------------------------------------------------- body
    SplitView {
        anchors.top: top.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        orientation: Qt.Horizontal
        handle: Rectangle {
            implicitWidth: 5
            color: SplitHandle.pressed || SplitHandle.hovered ? Theme.accent : Theme.line
            Behavior on color { ColorAnimation { duration: 120 } }
        }

        Rectangle {
            SplitView.preferredWidth: 400
            SplitView.minimumWidth: 320
            color: Theme.panel
            TabBar {
                id: leftTabs
                width: parent.width
                background: Rectangle { color: Theme.bg2 }
                DarkTab { text: "Команды" }
                DarkTab { text: "Персонажи"; accentColor: "#5fb3ff" }
                DarkTab { text: "Фоны"; accentColor: "#8be9fd" }
                DarkTab { text: "Звук"; accentColor: "#50fa7b" }
            }
            StackLayout {
                anchors.top: leftTabs.bottom
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                currentIndex: leftTabs.currentIndex
                Palette {
                    id: palette
                    onInsert: (cmd) => ed.insert(cmd)
                    onHoverCommand: (cmd) => ed.hoverExtra = cmd
                    onOpenForm: (row) => ed.openCommand(row)
                    onOpenTab: (index, query) => leftTabs.currentIndex = index
                    onLeave: code.focusEditor()
                }
                CharacterBrowser { id: charBrowser; onInsert: (cmd) => ed.insert(cmd); onHoverCommand: (cmd) => ed.hoverExtra = cmd }
                BackgroundBrowser { id: bgBrowser; onInsert: (cmd) => ed.insert(cmd); onHoverCommand: (cmd) => ed.hoverExtra = cmd }
                AudioBrowser { onInsert: (cmd) => ed.insert(cmd) }
            }
        }

        CodeEditor {
            id: code
            SplitView.fillWidth: true
            SplitView.minimumWidth: 380
            issues: ed.issues
        }

        Rectangle {
            SplitView.preferredWidth: 620
            SplitView.minimumWidth: 440
            color: Theme.panel
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                PreviewPane {
                    id: preview
                    Layout.fillWidth: true
                    Layout.preferredHeight: (width - 24) * 9 / 16 + 72
                    source: ed.previewSrc
                    extra: ed.hoverExtra
                    line: code.currentLine
                    lines: code.starts.length
                    onSeek: (n) => code.gotoLine(n)
                    onStep: (d) => ed.stepLine(d)
                    onEnlarge: big.open = true
                }
                TabBar {
                    id: infoTabs
                    Layout.fillWidth: true
                    background: Rectangle { color: Theme.bg2 }
                    DarkTab { text: "Проблемы" + (ed.issues.length ? " (" + ed.issues.length + ")" : ""); accentColor: ed.errors ? Theme.bad : ed.warnings ? Theme.warn : Theme.good }
                    DarkTab { text: "Сцена" }
                    DarkTab { text: "Код Ren'Py" }
                }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: infoTabs.currentIndex

                    // ---- problems
                    Item {
                        ListView {
                            id: issueList
                            anchors.fill: parent
                            anchors.margins: 6
                            clip: true
                            spacing: 2
                            model: ed.issues
                            ScrollBar.vertical: ScrollBar {}
                            delegate: Rectangle {
                                width: issueList.width
                                height: Math.max(34, msg.implicitHeight + 14)
                                radius: 6
                                color: ia.containsMouse ? Theme.panel3 : "transparent"
                                Rectangle { x: 10; anchors.verticalCenter: parent.verticalCenter; width: 10; height: 10; radius: 5; color: Theme.levelColor(modelData.level) }
                                Text { x: 30; anchors.verticalCenter: parent.verticalCenter; text: "стр. " + modelData.line; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 13; width: 70 }
                                Text {
                                    id: msg
                                    x: 104; width: parent.width - 114
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.msg; wrapMode: Text.Wrap
                                    color: Theme.text; font.family: Theme.ui; font.pixelSize: 14
                                }
                                MouseArea { id: ia; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: code.gotoLine(modelData.line) }
                            }
                        }
                        Column {
                            anchors.centerIn: parent
                            visible: ed.issues.length === 0
                            spacing: 8
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "✓"; color: Theme.good; font.pixelSize: 42 }
                            Text { text: "Проблем нет — жми «Играть»"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 16 }
                        }
                    }

                    // ---- scene state at the cursor
                    Flickable {
                        clip: true
                        contentHeight: sceneCol.implicitHeight + 20
                        Column {
                            id: sceneCol
                            x: 14; y: 12
                            width: parent.width - 28
                            spacing: 10
                            Row {
                                spacing: 12
                                Rectangle {
                                    width: 128; height: 72; radius: 6; clip: true; color: "#111"
                                    Image { anchors.fill: parent; source: ed.scene.bg && (ed.scene.bg.indexOf("bg ") === 0 || ed.scene.bg.indexOf("cg ") === 0) ? "image://gb/" + ed.scene.bg.substring(0, 2) + "/" + encodeURIComponent(ed.scene.bg.substring(3)) : ""; sourceSize: Qt.size(256, 144); asynchronous: true; fillMode: Image.PreserveAspectCrop }
                                }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    Text { text: "Фон"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                                    Text { text: (ed.scene.bg || "—") + "   ·   " + (ed.scene.time === "day" ? "день" : ed.scene.time === "sunset" ? "вечер" : ed.scene.time === "night" ? "ночь" : "пролог") + (ed.scene.music ? "   ·   ♪ " + ed.scene.music : "") + (ed.scene.weather ? "   ·   " + ed.scene.weather : "") + (ed.scene.filter ? "   ·   фильтр " + ed.scene.filter : ""); color: Theme.text; font.family: Theme.mono; font.pixelSize: 15 }
                                }
                            }
                            Text { text: "На сцене"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                            Flow {
                                width: parent.width
                                spacing: 8
                                Repeater {
                                    model: ed.scene.sprites || []
                                    Rectangle {
                                        width: 110; height: 138; radius: 8
                                        color: Theme.bg; border.color: Theme.charColor(modelData.image.split(" ")[0])
                                        Image {
                                            anchors.fill: parent; anchors.bottomMargin: 34
                                            source: "image://gb/face/" + encodeURIComponent(modelData.image)
                                            sourceSize: Qt.size(110, 110); asynchronous: true; fillMode: Image.PreserveAspectFit
                                            mirror: modelData.mirror
                                        }
                                        Column {
                                            anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            Text { anchors.horizontalCenter: parent.horizontalCenter; width: 104; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: modelData.image; color: Theme.charColor(modelData.image.split(" ")[0]); font.family: Theme.mono; font.pixelSize: 12 }
                                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.mirror ? "⇋ зеркально" : ""; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 11 }
                                        }
                                    }
                                }
                                Text { visible: !(ed.scene.sprites && ed.scene.sprites.length); text: "никого"; color: Theme.faint; font.family: Theme.ui; font.pixelSize: 14 }
                            }
                            Text { visible: !!ed.scene.text; text: "Говорит"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                            Text {
                                visible: !!ed.scene.text
                                width: parent.width; wrapMode: Text.Wrap
                                text: (ed.scene.name ? "<b>" + ed.scene.name + ":</b> " : "") + (ed.scene.text || "")
                                textFormat: Text.StyledText
                                color: Theme.text; font.family: Theme.ui; font.pixelSize: 15
                            }
                            Text {
                                visible: !!(ed.scene.ambience || ed.scene.sound || ed.scene.moment || ed.scene.nvl)
                                width: parent.width; wrapMode: Text.Wrap
                                text: [ed.scene.nvl ? "режим NVL" : "", ed.scene.ambience ? "≈ атмосфера " + ed.scene.ambience : "",
                                       ed.scene.sound ? "♬ звук " + ed.scene.sound : "", ed.scene.moment ? "✦ " + ed.scene.moment : ""].filter(x => x).join("   ·   ")
                                color: Theme.dim; font.family: Theme.mono; font.pixelSize: 14
                            }
                            Text { visible: !!(ed.scene.vars && ed.scene.vars.length); text: "Очки и флаги мода"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                            Repeater {
                                model: ed.scene.vars || []
                                Rectangle {
                                    width: sceneCol.width
                                    height: varCol.implicitHeight + 12
                                    radius: 8
                                    color: modelData.here ? "#2a9bd35a" : Theme.panel2
                                    border.color: modelData.here ? Theme.accent : Theme.line
                                    Column {
                                        id: varCol
                                        x: 10; y: 6
                                        width: parent.width - 20
                                        spacing: 2
                                        Text {
                                            text: "<b>" + modelData.name + "</b> = " + modelData.init + (modelData.persistent ? "   <i>(постоянная — живёт между играми)</i>" : "") + (modelData.here ? "   ← меняется на этой строке" : "")
                                            textFormat: Text.StyledText
                                            color: Theme.text; font.family: Theme.ui; font.pixelSize: 14
                                        }
                                        Text {
                                            width: parent.width; wrapMode: Text.Wrap
                                            visible: !!modelData.changes
                                            text: modelData.changes
                                            color: Theme.dim; font.family: Theme.mono; font.pixelSize: 12
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ---- generated Ren'Py code
                    Item {
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 4
                            TextArea {
                                readOnly: true
                                selectByMouse: true
                                text: infoTabs.currentIndex === 2 ? Engine.compile(code.text) : ""
                                font.family: Theme.mono
                                font.pixelSize: 13
                                color: "#d8d8e4"
                                background: Rectangle { color: Theme.bg }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- big preview (reading mode)
    Rectangle {
        id: big
        property bool open: false
        anchors.fill: parent
        z: 20
        color: "#f00a070d"
        opacity: open ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 220 } }
        focus: open
        Keys.onEscapePressed: open = false
        Keys.onLeftPressed: ed.stepLine(-1)
        Keys.onRightPressed: ed.stepLine(1)
        Keys.onSpacePressed: ed.stepLine(1)
        onOpenChanged: if (open) forceActiveFocus(); else code.focusEditor()
        MouseArea { anchors.fill: parent; onClicked: ed.stepLine(1) }
        Item {
            readonly property real s: Math.min((parent.width - 60) / 1280, (parent.height - 90) / 720)
            width: 1280 * s; height: 720 * s
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -14
            Rectangle { anchors.fill: parent; color: "black" }
            SmoothImage { anchors.fill: parent; source: ed.previewSrc; fade: 160 }
        }
        Text {
            anchors.bottom: parent.bottom; anchors.bottomMargin: 16
            anchors.horizontalCenter: parent.horizontalCenter
            text: "строка " + code.currentLine + " из " + code.starts.length + "   ·   клик / → / пробел — дальше   ·   ← — назад   ·   Esc — выйти"
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15
        }
    }
}
