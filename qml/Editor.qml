import QtQuick
import QtQml.Models
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// The mod editor. One preview for everything: the cursor line, a hovered palette command,
// a hovered emotion or background — all go through Engine.previewUrl and THE renderer.
Item {
    id: ed
    property string projectId
    property int startLine: 0                // opened from «Поиск по всем модам»: that line, the word in the find bar
    property string startFind: ""
    signal back()
    signal openOther(string id, int line, string find)
    property string hoverExtra: ""
    property var issues: []
    property var badges: []                  // the options' marks («🔒», «★», «+1», «↪») after their lines
    property string previewSrc: ""
    property var scene: ({})
    readonly property int errors: issues.filter(i => i.level >= 2).length
    readonly property int warnings: issues.filter(i => i.level === 1).length

    Component.onCompleted: {
        code.setText(Engine.loadStory(projectId))
        issues = Engine.lint(code.text)
        badges = Engine.choiceBadges(code.text)
        code.focusEditor()
        if (shotPage.indexOf("editor") === 0) code.gotoLine(shotPage === "editor-top" || shotPage === "editor-top-hover" ? 1 : 13)
        if (shotPage === "editor-top-hover") hoverExtra = "погода снег"
        if (shotPage === "editor-hover") hoverExtra = qsTr("показать un shy pioneer center dissolve")
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
        // «умный Enter»: «выбор», the options and their answers typed the way a person would
        if (shotPage === "editor-smart-enter") Qt.callLater(() => {
            code.setText(qsTr("@mod_id genry_enter\n\n: start\nфон ext_square_day\nСлавя: Куда пойдём?\n"))
            code.gotoLine(6)
            code.typeLikeAPerson(["выбор", "\n", "На площадь [+1 Славя]", "\n", "Славя: Пошли!", "\n", "\n",
                                  "Остаться здесь", "\n", "Славя: Ну и ладно.", "\n", "\n", "\n", "Славя: Идём дальше."])
        })
        if (shotPage === "editor-line-menu") Qt.callLater(() => { code.gotoLine(13); ed.lineScenes = Engine.sceneNames(code.text); lineMenu.popup(code, code.width - 300, 160) })
        if (shotPage === "editor-choice-sync") Qt.callLater(() => { choiceWizard.openFor(code.text, code.currentLine); choiceWizard.fillDemo(); choiceSyncShot.start() })
        if (shotPage === "editor-choice-timed") Qt.callLater(() => { choiceWizard.openFor(code.text, code.currentLine); choiceWizard.fillDemo(); choiceWizard.setStyle("timed") })
        if (shotPage === "editor-choice-picker") Qt.callLater(() => { choiceWizard.openFor(code.text, code.currentLine); choiceWizard.fillDemo(); choiceWizard.openPickerDemo() })
        if (shotPage === "editor-screenplay") Qt.callLater(() => { screenplay.openFor(code.text, code.currentLine, ""); screenplay.fillDemo(); screenplay.current = Number(shotArg) || 4 })
        if (shotPage === "editor-form") Qt.callLater(() => cmdForm.openNew(shotArg || "show", {}, code.text, code.currentLine))
        if (shotPage === "editor-library") Qt.callLater(() => { libraryBrowser.open(); libraryBrowser.pendingSearch = shotArg })
        if (shotPage === "editor-map") Qt.callLater(() => { mapEditor.openNew(code.text, code.currentLine); mapEditor.toggle("dining_hall"); mapEditor.setField("dining_hall", "chibi", "dv"); mapEditor.hoverId = "beach" })
        if (shotPage === "editor-form-edit") Qt.callLater(() => { code.gotoLine(Number(shotArg) || 14); ed.editLine() })
        if (shotPage === "editor-storymap") Qt.callLater(() => storyMap.openFor(code.text))
        if (shotPage === "editor-lab") Qt.callLater(() => { labDialog.openLab(); labDialog.pick(Number(shotArg) || 0) })
        if (shotPage === "editor-timeline") Qt.callLater(() => { ed.showTimeline = true; code.gotoLine(Number(shotArg) || 14); ed.refreshPreview() })
        if (shotPage === "editor-history") Qt.callLater(() => historyDialog.openFor(projectId, code.text))
        // «живое кино»: shotArg = "line clicks"
        if (shotPage === "editor-live") Qt.callLater(() => {
            const a = (shotArg || "1 2").split(" ").map(Number)
            cinemaView.beginDocked(code.text, a[0] || 1, preview)
            for (let i = 0; i < (a[1] || 0); ++i) { if (cinemaView.stop.kind === "choice") cinemaView.pick(0); else { cinemaView.shown = 999; cinemaView.advance() } }
            cinemaView.shown = 999
        })
        // the doctor: a page of the usual slips
        if (shotPage === "editor-doctor") Qt.callLater(() => {
            code.setText(qsTr("@mod_id genry_doctor\n@mod_name Доктор\n\n: start\nфон ext_square_dai\nпокзать dv smle pioneer center\nСлава: Привет!\nзвук sunny_day\nпереход finsh\n\n: finish\nтекст Конец.\n"))
            ed.issues = Engine.lint(code.text)
            infoTabs.currentIndex = 0
            code.gotoLine(6)
        })
        if (shotPage === "editor-dialogue-import") Qt.callLater(() => {
            dialogue.openFor(code.text, code.currentLine)
            dialogue.importUrls(shotArg.split("|").map(p => "file:///" + p))
        })
        if (shotPage === "editor-find") Qt.callLater(() => { code.gotoLine(1); code.openFindWith(shotArg || "Славя", false); code.replaceOpen = true })
        if (shotPage === "editor-search") Qt.callLater(() => searchAll.openWith(shotArg || "Славя"))
        if (shotPage === "editor-break") Qt.callLater(() => breakDialog.openFor(code.text))
        if (shotPage === "editor-publish") Qt.callLater(() => { exportDialog.openFor(projectId, code.text); exportDialog.startPublish() })
        if (shotPage === "editor-crash") Qt.callLater(() => Engine.shotCrash(projectId, code.text))
        if (startLine > 0) Qt.callLater(() => { code.gotoLine(startLine); if (startFind) code.openFindWith(startFind, false) })
        refreshPreview()
    }

    function refreshPreview() {
        previewSrc = Engine.previewUrl(code.text, code.currentLine, hoverExtra)
        scene = Engine.sceneInfo(code.text, code.currentLine)
        preview.boxes = hoverExtra === "" ? Engine.spriteBoxes(code.text, code.currentLine) : []
        if (timeline.visible) timeline.tdata = Engine.sceneTimeline(code.text, code.currentLine)
    }
    // «Таймлайн» under the editor (remembered between runs)
    property bool showTimeline: Engine.setting("timeline", "false") === "true"
    onShowTimelineChanged: { Engine.setSetting("timeline", showTimeline ? "true" : "false"); if (showTimeline) refreshPreview() }
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
    readonly property bool inChoice: !!Engine.choiceBlockAt(code.text, code.currentLine).from
    // «Алиса (злая, слева): …» under the cursor -> the commands it stands for
    readonly property string playKind: Engine.screenplayKind(code.lineText(code.currentLine))
    function expandLine() {
        const r = Engine.expandScreenplayLine(code.text, code.currentLine)
        if (!r.lines.length) { Engine.toast(qsTr("Эта строка — не сценарная: разворачивать нечего"), 1); return }
        code.replaceLine(code.currentLine, r.lines.join("\n"))
        const warn = r.notes.filter(n => n.warn)
        Engine.toast(warn.length ? "⚠ " + warn[0].text : qsTr("Развернул в ") + r.lines.length + qsTr(" стр."), warn.length ? 1 : 0)
    }
    function editLine() {
        // a line of a «выбор» block: the whole choice opens in its studio
        if (Engine.choiceBlockAt(code.text, code.currentLine).from) { choiceWizard.openFor(code.text, code.currentLine); return }
        if (playKind !== "" && playKind !== "prose" && !lineForm.id) { expandLine(); return }
        const p = Engine.parseCommand(code.lineText(code.currentLine))
        if (!p.id) { Engine.toast(qsTr("Эту строку в форме не настроить — выбери команду в палитре"), 1); return }
        if (p.id === "map") mapEditor.openEdit(p.values, code.text, code.currentLine)
        else cmdForm.openEdit(p.id, p.values, code.text, code.currentLine)
    }
    // «Починить» (the doctor): one fix the way the lint offers it - through the editor, so Ctrl+Z takes it back
    function fixOne(issue) {
        const n = issue.line
        if (issue.fixMode === 1) code.setLineText(n, issue.fix)
        else if (issue.fixMode === 2) code.insertAfterLine(n, issue.fix)
        else if (issue.fixMode === 3) code.removeLine(n)
        else if (issue.fixMode === 4) {
            const last = code.starts.length
            code.insertAfterLine(last, (code.lineText(last).trim() === "" ? "" : "\n") + issue.fix)
        }
        else if (issue.fixMode === 5) {
            if (n > 1) code.insertAfterLine(n - 1, issue.fix)
            else code.insertLine(issue.fix)
        }
        Sfx.click()
        previewTimer.restart(); lintTimer.restart(); saveTimer.restart()
        if (issue.line > 0 && issue.fixMode !== 4) code.gotoLine(Math.max(1, issue.line))
    }
    function fixAll() {
        const line = code.currentLine
        ed.save()
        Engine.keepVersion(projectId, code.text, "doctor")
        const text = Engine.applyFixes(code.text, ed.issues)
        if (text === code.text) return
        const before = ed.issues.length
        code.setText(text)
        code.gotoLine(Math.min(line, code.starts.length))
        ed.issues = Engine.lint(code.text)
        ed.badges = Engine.choiceBadges(code.text)
        ed.save()
        ed.refreshPreview()
        Engine.toast(qsTr("Доктор починил, что мог: было замечаний ") + before + qsTr(", осталось ") + ed.issues.length +
                     qsTr(". Прежний текст — в «Истории»"), 0)
    }
    function save() { Engine.saveStory(projectId, code.text) }
    function gotoStoryLine(n) { code.gotoLine(n) }
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
    Timer { id: choiceSyncShot; interval: 900; onTriggered: choiceWizard.demoSync() }
    Timer { id: facefixShot; property var args: []; interval: 9000; onTriggered: charBrowser.fixFace(args[0], args.slice(1).join(" "), "") }
    Timer { id: lintTimer; interval: 300; onTriggered: { ed.issues = Engine.lint(code.text); ed.badges = Engine.choiceBadges(code.text) } }
    Timer { id: saveTimer; interval: 900; onTriggered: ed.save() }
    Timer { id: liveTimer; interval: 450; onTriggered: cinemaView.reload(code.text) }      // «живое кино»: the edit on screen
    onHoverExtraChanged: previewTimer.restart()
    Connections {
        target: code
        function onEdited() { previewTimer.restart(); lintTimer.restart(); saveTimer.restart(); if (cinemaView.opened && cinemaView.docked) liveTimer.restart() }
        function onCurrentLineChanged() { previewTimer.restart() }
    }
    Connections {
        target: Engine
        function onAssetsChanged() { previewTimer.restart(); lintTimer.restart() }
        // GenryBL gave the mod a free @mod_id: the editor shows the story as it is on disk, the cursor stays
        function onStoryRewritten(id, text) {
            if (id !== ed.projectId || text === code.text) return
            const line = code.currentLine
            code.setText(text)
            code.gotoLine(line)
        }
    }

    Shortcut { sequences: [StandardKey.Save]; onActivated: { ed.save(); Engine.toast(qsTr("Сохранено"), 0) } }
    Shortcut { sequence: "F5"; onActivated: ed.play() }
    Shortcut { sequence: "F6"; enabled: !cinemaView.opened; onActivated: ed.cinema() }
    Shortcut { sequence: "Ctrl+D"; enabled: !dialogue.opened; onActivated: dialogue.openFor(code.text, code.currentLine) }
    Shortcut { sequence: "Ctrl+E"; enabled: !cmdForm.opened; onActivated: ed.editLine() }
    Shortcut { sequence: "Ctrl+Shift+E"; enabled: !cmdForm.opened; onActivated: ed.expandLine() }
    Shortcut { sequence: "Ctrl+Shift+V"; enabled: !screenplay.opened; onActivated: screenplay.openFor(code.text, code.currentLine, Engine.clipboardText()) }
    Shortcut { sequence: "Ctrl+K"; onActivated: { leftTabs.currentIndex = 0; palette.focusSearch() } }
    Shortcut { sequence: "Ctrl+M"; enabled: !storyMap.opened; onActivated: storyMap.openFor(code.text) }
    Shortcut { sequence: "Ctrl+T"; onActivated: ed.showTimeline = !ed.showTimeline }
    Shortcut { sequences: [StandardKey.Find]; onActivated: code.openFind(false) }
    Shortcut { sequence: "Ctrl+H"; onActivated: code.openFind(true) }
    Shortcut { sequence: "F3"; onActivated: code.findNext() }
    Shortcut { sequence: "Shift+F3"; onActivated: code.findPrev() }
    Shortcut { sequence: "Ctrl+Shift+F"; enabled: !searchAll.opened; onActivated: { ed.save(); searchAll.openWith(code.selectedText) } }
    Shortcut { sequence: "F7"; enabled: !breakDialog.opened; onActivated: { ed.save(); breakDialog.openFor(code.text) } }
    BreakDialog {
        id: breakDialog
        onGotoLine: (line) => { breakDialog.close(); code.gotoLine(line) }
        onWatch: (route) => { breakDialog.close(); cinemaView.beginRoute(code.text, route) }
        onClosed: code.focusEditor()
    }
    SearchAllDialog {
        id: searchAll
        projectId: ed.projectId
        storyText: code.text
        onOpenAt: (id, line, query, cs) => {
            searchAll.close()
            if (id === ed.projectId) { code.gotoLine(line); code.openFindWith(query, cs) }
            else { ed.save(); ed.openOther(id, line, query) }
        }
        onClosed: code.focusEditor()
    }
    Repeater {                                   // Alt+1..9: the quick inserts of the palette
        model: 9
        Item { required property int index; Shortcut { sequence: "Alt+" + (index + 1); enabled: !cmdForm.opened; onActivated: palette.runPin(index) } }
    }
    CinemaView {
        id: cinemaView
        onGotoLine: (line) => code.gotoLine(line)
        onClosed: code.focusEditor()
    }
    // the live cinema re-walks the story when a picture / sound comes into the project too
    Connections { target: Engine; function onAssetsChanged() { if (cinemaView.opened && cinemaView.docked) liveTimer.restart() } }
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
    // «+» on the current line: what to put right under it
    property var lineScenes: []
    function lineIndent() {
        const t = code.lineText(code.currentLine)
        const ind = t.match(/^\s*/)[0]
        return /^\s*-(?!>)/.test(t) ? ind + "    " : ind            // under an option: its own lines
    }
    function lastSpeaker() {
        const lines = code.text.split("\n")
        for (let i = Math.min(lines.length, code.currentLine) - 1; i >= 0; --i) {
            const m = lines[i].match(/^\s*([^\s\-\[:#@][^:]{0,30}):\s/)
            if (m && m[1].toLowerCase() !== "выбор") return m[1]
        }
        return "Славя"
    }
    function endChoice() {
        // the options are over: close the choice, or put the cursor right after it to go on with the story
        const b = Engine.choiceBlockAt(code.text, code.currentLine)
        if (!b.from) return
        const head = code.lineText(b.from).match(/^\s*/)[0]
        if (/^\s*конецвыбора/i.test(code.lineText(b.to))) code.insertAfterLine(b.to, head)
        else code.insertAfterLine(b.to, head + "конецвыбора")
    }
    Menu {
        id: lineMenu
        MenuItem { text: qsTr("Реплика героя"); onTriggered: code.insertAfterLine(code.currentLine, ed.lineIndent() + ed.lastSpeaker() + ": ") }
        MenuItem { text: qsTr("Слова рассказчика"); onTriggered: code.insertAfterLine(code.currentLine, ed.lineIndent() + "текст ") }
        MenuItem { text: qsTr("Сменить локацию (фон)…"); onTriggered: cmdForm.openNew("bg", {}, code.text, code.currentLine) }
        MenuItem { text: qsTr("Выбор…"); onTriggered: choiceWizard.openFor(code.text, code.currentLine) }
        MenuItem { text: qsTr("Конец выбора — дальше история"); visible: ed.inChoice; height: visible ? implicitHeight : 0; onTriggered: ed.endChoice() }
        Menu {
            id: lineJumpMenu
            title: qsTr("Перейти в сцену")
            Instantiator {
                model: ed.lineScenes
                delegate: MenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: code.insertAfterLine(code.currentLine, ed.lineIndent() + "переход " + modelData)
                }
                onObjectAdded: (index, object) => lineJumpMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => lineJumpMenu.removeItem(object)
            }
            MenuSeparator {}
            MenuItem { text: qsTr("Другая…"); onTriggered: cmdForm.openNew("jump", {}, code.text, code.currentLine) }
        }
        MenuItem { text: qsTr("Новая сцена…"); onTriggered: cmdForm.openNew("label", {}, code.text, code.currentLine) }
        MenuItem { text: qsTr("Конец игры"); onTriggered: code.insertAfterLine(code.currentLine, ed.lineIndent() + "конецигры") }
    }
    ChoiceDialog {
        id: choiceWizard
        onInsertBlock: (block) => ed.insert(block)
        onReplaceBlock: (from, to, block) => code.replaceLines(from, to, block)
        onClosed: code.focusEditor()
    }
    ExportDialog { id: exportDialog; onClosed: code.focusEditor() }
    StoryMap {
        id: storyMap
        onGotoLine: (line) => code.gotoLine(line)
        onCreateScene: (name) => ed.createScene(name)
        onClosed: code.focusEditor()
    }
    // a way leads to a scene that is not there («Создать» on the story map): the scene at the end of the story
    function createScene(name) {
        const n = code.starts.length
        const last = code.lineText(n)
        code.insertAfterLine(n, (last.trim() === "" ? "" : "\n") + ": " + name + "\nтекст ")
        Engine.toast(qsTr("Сцена «") + name + qsTr("» добавлена в конец — пиши"), 0)
    }
    ModFilesDialog { id: modFilesDialog; onClosed: code.focusEditor() }
    LabDialog {
        id: labDialog
        // a piece with scenes of its own goes to the end of the story; a plain one under the cursor
        onInsertPiece: (body, atEnd) => {
            if (!atEnd) { ed.insert(body); return }
            const last = code.starts.length
            code.insertAfterLine(last, (code.lineText(last).trim() === "" ? "" : "\n") + body)
        }
        onWatch: (story) => cinemaView.begin(story, 1)
        onClosed: code.focusEditor()
    }
    HistoryDialog {
        id: historyDialog
        onRestored: (text) => { code.setText(text); ed.issues = Engine.lint(code.text); ed.badges = Engine.choiceBadges(code.text); ed.refreshPreview() }
        onClosed: code.focusEditor()
    }
    ModTitleDialog {
        id: titleDialog
        onApplied: (text) => { code.setText(text); ed.save(); ed.issues = Engine.lint(code.text); ed.refreshPreview(); Engine.toast(qsTr("Название мода обновлено"), 0) }
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
        title: clean ? qsTr("Движок БЛ: мод чистый") : qsTr("Движок БЛ нашёл проблемы")
        standardButtons: Dialog.Ok
        ScrollView {
            implicitHeight: Math.min(480, report.implicitHeight + 10)
            anchors.fill: parent
            TextArea {
                id: report
                readOnly: true
                wrapMode: TextEdit.Wrap
                text: checkDialog.clean ? qsTr("Ren'Py 7.4 (lint самой игры) не нашёл ни одной ошибки в моде.") : checkDialog.lines.join("\n")
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
        // A laptop at 125-150 % Windows scale is ~1280 px wide: the old one-line bar pushed «Играть» off the edge.
        // Now «← Меню», the name, the problems chip, «Экспорт», «Кино» and «Играть» always stay; the tools fold
        // into «☰ Ещё» when they do not fit. The tools keep their width inside a clipping box (never hidden), so
        // the bar always knows how much room they would need.
        readonly property real toolsNeed: tools.implicitWidth + 10
        readonly property real fixedNeed: menuBtn.implicitWidth + Math.min(titleInk.implicitWidth, 420) + nameBtn.implicitWidth + chipBox.width +
                                          exportBtn.implicitWidth + (stopBtn.visible ? stopBtn.implicitWidth + 10 : 0) + cinemaBtn.implicitWidth +
                                          playBtn.implicitWidth + 10 * 8 + 26 + 40
        readonly property bool compact: fixedNeed + toolsNeed > width
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 14
            spacing: 10
            PillButton { id: menuBtn; dark: true; text: qsTr("← Меню"); onClicked: { ed.save(); ed.back() } }
            InkText {
                id: titleInk
                text: Engine.currentProjectName; size: 26; color: Theme.gold; Layout.alignment: Qt.AlignVCenter
                Layout.maximumWidth: top.compact ? 300 : 520
                clip: true
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: titleDialog.openFor(code.text) }
            }
            PillButton {
                id: nameBtn
                dark: true
                text: qsTr("Aa Название")
                onClicked: titleDialog.openFor(code.text)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Как мод выглядит в списке модов БЛ: свой шрифт, цвет, размер")
            }
            Rectangle {
                id: chipBox
                Layout.alignment: Qt.AlignVCenter
                height: 26; radius: 13
                width: chip.implicitWidth + 22
                color: ed.errors ? "#33ff5566" : ed.warnings ? "#33ffc857" : "#3357c785"
                border.color: ed.errors ? Theme.bad : ed.warnings ? Theme.warn : Theme.good
                Text {
                    id: chip
                    anchors.centerIn: parent
                    text: ed.errors ? qsTr("✖ ошибок: ") + ed.errors + (ed.warnings ? " · ⚠ " + ed.warnings : "")
                                    : ed.warnings ? qsTr("⚠ предупреждений: ") + ed.warnings : qsTr("✓ всё чисто")
                    color: Theme.text; font.family: Theme.ui; font.pixelSize: 13; font.bold: true
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: infoTabs.currentIndex = 0 }
            }
            Item { Layout.fillWidth: true }
            // the tools: in a line when they fit, folded into «☰ Ещё» when they do not
            Item {
                Layout.preferredWidth: top.compact ? 0 : tools.implicitWidth
                Layout.preferredHeight: tools.implicitHeight
                clip: true
                Row {
                    id: tools
                    spacing: 10
                    readonly property bool linePlay: ed.playKind !== "" && ed.playKind !== "prose" && !ed.lineForm.id
                    PillButton {
                        dark: true
                        text: tools.linePlay ? qsTr("⇲ Развернуть строку") : qsTr("✎ Настроить строку")
                        enabled: tools.linePlay || !!ed.lineForm.id || ed.inChoice
                        onClicked: tools.linePlay ? ed.expandLine() : ed.editLine()
                        ToolTip.visible: hovered
                        ToolTip.text: tools.linePlay ? qsTr("Сценарная строка → команды, которые она означает: показать …, фон …, реплика (Ctrl+Shift+E)")
                                                     : qsTr("Открыть строку под курсором в её окне: все параметры, картинки, превью (Ctrl+E)")
                    }
                    PillButton {
                        dark: true
                        text: qsTr("✍ Сценарий")
                        onClicked: screenplay.openFor(code.text, code.currentLine, "")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Пиши как сценарий: вставь сценарий, переписку или главу — станут фонами, спрайтами и репликами (Ctrl+Shift+V — из буфера)")
                    }
                    PillButton {
                        dark: true
                        accent: ed.inChoice
                        text: ed.inChoice ? qsTr("⑂ Этот выбор") : qsTr("⑂ Выбор")
                        onClicked: choiceWizard.openFor(code.text, code.currentLine)
                        ToolTip.visible: hovered
                        ToolTip.text: ed.inChoice ? qsTr("Открыть выбор под курсором в студии: текст, превью, карточка варианта (Ctrl+E)")
                                                  : qsTr("Студия выбора: пишешь варианты текстом, окно помогает — очки, замки, «запомнит», превью меню БЛ")
                    }
                    PillButton {
                        dark: true
                        text: qsTr("♪ Диалог")
                        onClicked: dialogue.openFor(code.text, code.currentLine)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Диалог + озвучка: реплики таблицей, озвучка файлами по порядку, кадр игры сразу (Ctrl+D)")
                    }
                    PillButton {
                        dark: true
                        text: qsTr("Библиотека")
                        onClicked: libraryBrowser.open()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Картинки всех модов из твоей мастерской БЛ: спрайты, фоны, CG — взять в свой мод")
                    }
                    PillButton { dark: true; text: qsTr("Папка мода"); onClicked: Engine.openFolder(Engine.projectDir(ed.projectId)) }
                    PillButton {
                        dark: true
                        text: qsTr("📦 Файлы мода")
                        enabled: !Engine.busy
                        onClicked: modFilesDialog.openFor(ed.projectId, code.text)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Что реально уедет к игрокам: размер по файлам, чего не хватает, что лежит зря")
                    }
                    PillButton {
                        dark: true
                        text: qsTr("🧪 Лаборатория")
                        onClicked: labDialog.openLab()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Готовые куски сцен: скример, гроза, выбор на время, кодовый замок, звонок… — каждый проверен самой игрой")
                    }
                    PillButton {
                        dark: true
                        accent: ed.showTimeline
                        text: qsTr("🎞 Таймлайн")
                        onClicked: ed.showTimeline = !ed.showTimeline
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Сцена дорожками, как в видеоредакторе: фон, герои, реплики, музыка, эффекты — тащи клипы, чтобы переставить строки (Ctrl+T)")
                    }
                    PillButton {
                        dark: true
                        text: qsTr("💥 Сломай мой мод")
                        onClicked: { ed.save(); breakDialog.openFor(code.text) }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("GenryBL пройдёт мод сотни раз, как разные игроки, и покажет, где он ломается: недостижимые концовки, вечные замки, тупики, петли (F7)")
                    }
                    PillButton {
                        dark: true
                        text: qsTr("🗺 Карта сюжета")
                        onClicked: storyMap.openFor(code.text)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Все сцены и пути между ними: что недостижимо, что ведёт в никуда, где мод обрывается (Ctrl+M)")
                    }
                    PillButton {
                        dark: true
                        text: qsTr("🕘 История")
                        onClicked: { ed.save(); historyDialog.openFor(ed.projectId, code.text) }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("История версий: GenryBL сам хранит копии сценария — вернуть любую можно одним кликом")
                    }
                    PillButton {
                        dark: true
                        text: qsTr("Проверить движком")
                        enabled: !Engine.busy
                        onClicked: Engine.engineCheck(ed.projectId, code.text)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Родной lint Ren'Py самой игры: картинки, метки, синтаксис (1-2 минуты)")
                    }
                }
            }
            PillButton {
                visible: top.compact
                dark: true
                text: qsTr("☰ Ещё")
                onClicked: moreMenu.popup()
                Menu {
                    id: moreMenu
                    MenuItem {
                        text: tools.linePlay ? qsTr("Развернуть строку") : qsTr("Настроить строку")
                        enabled: tools.linePlay || !!ed.lineForm.id || ed.inChoice
                        onTriggered: tools.linePlay ? ed.expandLine() : ed.editLine()
                    }
                    MenuItem { text: qsTr("Сценарий — пиши как сценарий"); onTriggered: screenplay.openFor(code.text, code.currentLine, "") }
                    MenuItem { text: qsTr("Выбор и последствия"); onTriggered: choiceWizard.openFor(code.text, code.currentLine) }
                    MenuItem { text: qsTr("Диалог + озвучка"); onTriggered: dialogue.openFor(code.text, code.currentLine) }
                    MenuItem { text: qsTr("Библиотека картинок"); onTriggered: libraryBrowser.open() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Найти и заменить (Ctrl+F, Ctrl+H)"); onTriggered: code.openFind(true) }
                    MenuItem { text: qsTr("Поиск по всем модам (Ctrl+Shift+F)"); onTriggered: { ed.save(); searchAll.openWith(code.selectedText) } }
                    MenuItem { text: qsTr("Папка мода"); onTriggered: Engine.openFolder(Engine.projectDir(ed.projectId)) }
                    MenuItem { text: qsTr("Лаборатория механик"); onTriggered: labDialog.openLab() }
                    MenuItem { text: qsTr("Таймлайн сцены"); onTriggered: ed.showTimeline = !ed.showTimeline }
                    MenuItem { text: qsTr("Карта сюжета"); onTriggered: storyMap.openFor(code.text) }
                    MenuItem { text: qsTr("Сломай мой мод (F7)"); onTriggered: { ed.save(); breakDialog.openFor(code.text) } }
                    MenuItem { text: qsTr("Файлы мода — что уедет к игрокам"); enabled: !Engine.busy; onTriggered: modFilesDialog.openFor(ed.projectId, code.text) }
                    MenuItem { text: qsTr("История версий"); onTriggered: { ed.save(); historyDialog.openFor(ed.projectId, code.text) } }
                    MenuItem { text: qsTr("Проверить движком игры"); enabled: !Engine.busy; onTriggered: Engine.engineCheck(ed.projectId, code.text) }
                }
            }
            PillButton {
                id: exportBtn
                dark: true
                text: qsTr("⇪ Экспорт")
                enabled: !Engine.busy
                onClicked: exportDialog.openFor(ed.projectId, code.text)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Отдать мод людям: архив для игроков или папка для Мастерской Steam — после проверки самой игрой")
            }
            PillButton { id: stopBtn; visible: Engine.gameRunning; text: qsTr("■ Стоп"); onClicked: Engine.stopGame() }
            PillButton {
                id: cinemaBtn
                accent: true
                text: qsTr("▶ Кино  F6")
                onClicked: ed.cinema()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Кино-режим: мод играет прямо тут, как в игре — печать текста, музыка, выборы по веткам, без загрузки БЛ (F6)")
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
                    Text { text: Engine.gameRunning ? qsTr("Заново") : qsTr("Играть"); color: "#16240c"; font.family: Theme.riffic; font.pixelSize: 20; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "F5"; color: "#2e4a16"; font.family: Theme.mono; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Собрать мод и открыть Бесконечное лето прямо в сцене под курсором")
            }
        }
    }

    // ---------------------------------------------------------------- body
    SplitView {
        anchors.top: top.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: timeline.visible ? timeline.top : parent.bottom
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
                DarkTab { text: qsTr("Команды") }
                DarkTab { text: qsTr("Персонажи"); accentColor: "#5fb3ff" }
                DarkTab { text: qsTr("Фоны"); accentColor: "#8be9fd" }
                DarkTab { text: qsTr("Звук"); accentColor: "#50fa7b" }
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
            badges: ed.badges
            lineTool: true
            onLineToolClicked: (line, anchor) => { ed.lineScenes = Engine.sceneNames(code.text); lineMenu.popup(anchor, anchor.width + 4, 0) }
            onReplaced: (n) => {
                previewTimer.restart(); lintTimer.restart(); saveTimer.restart()
                Engine.toast(n ? qsTr("Заменено: ") + n + qsTr(" (Ctrl+Z вернёт всё сразу)") : qsTr("Нечего заменять"), n ? 0 : 1)
            }
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
                    onLiveCinema: { ed.save(); cinemaView.beginDocked(code.text, code.currentLine, preview) }
                    // a character dragged on the frame: its «показать» line takes the new place / distance
                    onMoveSprite: (n, pos, dist) => {
                        code.setLineText(n, Engine.placeSprite(code.lineText(n), pos, dist))
                        previewTimer.restart(); lintTimer.restart(); saveTimer.restart()
                    }
                }
                TabBar {
                    id: infoTabs
                    Layout.fillWidth: true
                    background: Rectangle { color: Theme.bg2 }
                    DarkTab { text: qsTr("Проблемы") + (ed.issues.length ? " (" + ed.issues.length + ")" : ""); accentColor: ed.errors ? Theme.bad : ed.warnings ? Theme.warn : Theme.good }
                    DarkTab { text: qsTr("Сцена") }
                    DarkTab { text: qsTr("Код Ren'Py") }
                }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: infoTabs.currentIndex

                    // ---- problems
                    Item {
                        readonly property int fixable: ed.issues.filter(i => i.fixMode > 0).length
                        // «Починить всё»: every fix the doctor offers, in one go (the old text stays in the history)
                        Rectangle {
                            id: fixAllBar
                            visible: parent.fixable > 0
                            width: parent.width
                            height: visible ? 42 : 0
                            color: Theme.bg2
                            Text {
                                x: 12; anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - fixAllBtn.width - 36
                                elide: Text.ElideRight
                                text: qsTr("Доктор может сам починить: ") + parent.parent.fixable
                                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14
                            }
                            PillButton {
                                id: fixAllBtn
                                anchors.right: parent.right; anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                accent: true
                                text: qsTr("🩺 Починить всё")
                                onClicked: ed.fixAll()
                            }
                        }
                        ListView {
                            id: issueList
                            anchors.fill: parent
                            anchors.topMargin: fixAllBar.height + 6
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
                                Text { x: 30; anchors.verticalCenter: parent.verticalCenter; text: modelData.line > 0 ? qsTr("стр. ") + modelData.line : qsTr("файл"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 13; width: 70 }
                                Text {
                                    id: msg
                                    x: 104; width: parent.width - 114 - (fixBtn.visible ? fixBtn.width + 10 : 0)
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.msg; wrapMode: Text.Wrap
                                    color: Theme.text; font.family: Theme.ui; font.pixelSize: 14
                                }
                                MouseArea { id: ia; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: modelData.line > 0 ? code.gotoLine(modelData.line) : Engine.revealFile(modelData.file) }
                                PillButton {
                                    id: fixBtn
                                    visible: modelData.fixMode > 0
                                    anchors.right: parent.right; anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(implicitWidth, 190)
                                    dark: true
                                    text: qsTr("Починить")
                                    onClicked: ed.fixOne(modelData)
                                    ToolTip.visible: hovered
                                    ToolTip.text: modelData.fixMode === 3 ? qsTr("Убрать эту строку")
                                                : modelData.fixMode === 1 ? qsTr("Строка станет: ") + modelData.fix.trim()
                                                : qsTr("Допишу: ") + modelData.fix.trim()
                                }
                            }
                        }
                        Column {
                            anchors.centerIn: parent
                            visible: ed.issues.length === 0
                            spacing: 8
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "✓"; color: Theme.good; font.pixelSize: 42 }
                            Text { text: qsTr("Проблем нет — жми «Играть»"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 16 }
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
                                    Text { text: qsTr("Фон"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                                    Text { text: (ed.scene.bg || "—") + "   ·   " + (ed.scene.time === "day" ? qsTr("день") : ed.scene.time === "sunset" ? qsTr("вечер") : ed.scene.time === "night" ? qsTr("ночь") : qsTr("пролог")) + (ed.scene.music ? "   ·   ♪ " + ed.scene.music : "") + (ed.scene.weather ? "   ·   " + ed.scene.weather : "") + (ed.scene.filter ? qsTr("   ·   фильтр ") + ed.scene.filter : ""); color: Theme.text; font.family: Theme.mono; font.pixelSize: 15 }
                                }
                            }
                            Text { text: qsTr("На сцене"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
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
                                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.mirror ? qsTr("⇋ зеркально") : ""; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 11 }
                                        }
                                    }
                                }
                                Text { visible: !(ed.scene.sprites && ed.scene.sprites.length); text: qsTr("никого"); color: Theme.faint; font.family: Theme.ui; font.pixelSize: 14 }
                            }
                            Text { visible: !!ed.scene.text; text: qsTr("Говорит"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
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
                                text: [ed.scene.nvl ? qsTr("режим NVL") : "", ed.scene.ambience ? qsTr("≈ атмосфера ") + ed.scene.ambience : "",
                                       ed.scene.sound ? qsTr("♬ звук ") + ed.scene.sound : "", ed.scene.moment ? "✦ " + ed.scene.moment : ""].filter(x => x).join("   ·   ")
                                color: Theme.dim; font.family: Theme.mono; font.pixelSize: 14
                            }
                            Text { visible: !!(ed.scene.vars && ed.scene.vars.length); text: qsTr("Очки и флаги мода"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
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
                                            text: "<b>" + modelData.name + "</b> = " + modelData.init + (modelData.persistent ? qsTr("   <i>(постоянная — живёт между играми)</i>") : "") + (modelData.here ? qsTr("   ← меняется на этой строке") : "")
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

    // ---------------------------------------------------------------- the timeline (Ctrl+T)
    Timeline {
        id: timeline
        visible: ed.showTimeline
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.min(300, Math.max(170, ed.height * 0.28))
        line: code.currentLine
        onGotoLine: (n) => code.gotoLine(n)
        onEditLine: (n) => { code.gotoLine(n); ed.editLine() }
        onMoveLine: (from, to) => {
            code.moveLine(from, to)
            previewTimer.restart(); lintTimer.restart(); saveTimer.restart()
        }
        onCloseMe: ed.showTimeline = false
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
            text: qsTr("строка ") + code.currentLine + qsTr(" из ") + code.starts.length + qsTr("   ·   клик / → / пробел — дальше   ·   ← — назад   ·   Esc — выйти")
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15
        }
    }
}
