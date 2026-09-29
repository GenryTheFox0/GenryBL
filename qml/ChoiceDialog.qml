import QtQuick
import QtQml.Models
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Студия выбора» (Выбор 2.0). The choice IS text - the big editor on the left holds exactly the lines that go into the
// story, typed by hand. The rest only helps with that text: the real ES menu on the right (the option under the cursor
// lit; a click on an option there puts the cursor on its line), the options as thin rows under it, the one under the
// cursor opened as a card - points, lock, condition, «запомнит», scene, picture: a change there rewrites its line, a line
// typed by hand fills the card. «+» on the current line, completion on «[», mistakes underlined under their own words.
Popup {
    id: cs
    property string storyText: ""
    property int storyLine: 1          // a new choice goes under this story line
    property int editFrom: 0           // a choice of the story opened here: its lines (0 = a new one)
    property int editTo: 0
    property string previewSrc: ""
    property var outline: ({ head: {}, options: [], timeout: {}, end: 0 })
    property var issues: []
    property var rects: []
    property int previewHover: -1
    property int pickFor: -1           // the option waiting for a picture (-2: the background before the choice)
    signal insertBlock(string block)
    signal replaceBlock(int from, int to, string block)

    readonly property var head: outline.head || ({})
    readonly property var options: outline.options || []
    readonly property string style: head.style || "es"
    readonly property int cur: {
        const l = ed.currentLine
        for (let i = 0; i < options.length; ++i) if (l >= options[i].line && l <= options[i].bodyTo) return i
        return -1
    }
    readonly property var curOpt: cur >= 0 && cur < options.length ? options[cur] : null
    // one colour per option: its band in the text, its row and card, its frame in the game's menu
    readonly property var optColors: ["#ff79c6", "#8be9fd", "#50fa7b", "#ffb86c", "#bd93f9", "#f1fa8c", "#ff6e6e", "#6be5c5", "#ffa0d8"]
    function colorOf(i) { return i >= 0 ? optColors[i % optColors.length] : Theme.accent }
    property bool moreOpen: false        // the card's «Условия и последствия»
    function hasMore(o) {
        return !!o && (!!o.need || !!o.cond || (o.remember || []).length > 0 || (o.flags || []).length > 0 || !!o.target || !!o.image || !!o.exit || !!o.always)
    }
    readonly property string template: "выбор\nСлавя: Куда пойдём?\n- На площадь\n    Славя: Пошли!\n- Остаться здесь\nконецвыбора"

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 30, 1800)
    height: Math.min(parent.height - 30, 1010)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    // ------------------------------------------------------------------ open / write back
    function openFor(text, line) {
        storyText = text
        sceneList = Engine.sceneNames(text)
        const b = Engine.choiceBlockAt(text, line)
        if (b.from) { editFrom = b.from; editTo = b.to; storyLine = b.from - 1; ed.setText(b.text) }
        else { editFrom = 0; editTo = 0; storyLine = line; ed.setText(template) }
        previewHover = -1
        open()
        reparse()
        Qt.callLater(() => { const o = cs.options; ed.gotoLine(o.length ? o[0].line : 1) })
    }
    function fillDemo() {                // --shot editor-choice
        ed.setText("выбор\nСлавя: Куда пойдём?\n- Пойти со Славей [+1 Славя] [запомнит Славя]\n    Славя: Отлично, пойдём!\n"
                   + "- Позвать её на танцы [нужно Славя 3]\n    Славя: Ой… давай.\n- Остаться с Алисой [-1 Славя]\n    Алиса: Ну и правильно.\nконецвыбора")
        reparse()
        Qt.callLater(() => ed.gotoLine(5))
    }
    // --shot editor-choice-sync: the card writes the text (the lock 3 -> 5), the text fills the card (a hand-typed
    // «[+2 Алиса]»), a mistake is underlined under its own word
    function demoSync() {
        ed.gotoLine(5)
        setOptMany({ needVar: "Славя", needN: 5 })
        ed.setLineText(7, "- Остаться с Алисой [-1 Славя] [+2 Алиса]")
        ed.setLineText(3, "- Пойти со Славей [+1 Славя] [запомнит Славя] [нужно Слава много]")
        ed.gotoLine(7)
    }
    function openPickerDemo() { pickFor = Math.max(0, cur); picker.tab = 0; picker.tag = "sl"; picker.tagName = "Славя"; picker.open() }
    function setStyle(s) { setHead("style", s) }
    function commit() {
        if (!options.length) { Engine.toast("В выборе нет ни одного варианта «- Текст»", 2); return }
        const text = ed.text.replace(/\s+$/, "")
        if (editFrom) replaceBlock(editFrom, editTo, text)
        else insertBlock(text)
        close()
    }
    function reparse() { outline = Engine.choiceOutline(ed.text); refresh() }
    function refresh() { previewTimer.restart(); issueTimer.restart() }

    Timer {
        id: previewTimer
        interval: 60
        onTriggered: {
            cs.previewSrc = Engine.previewUrl(cs.storyText, cs.storyLine, ed.text, cs.previewHover >= 0 ? cs.previewHover : Math.max(0, cs.cur))
            cs.rects = Engine.choiceRects(cs.storyText, cs.storyLine, ed.text)
        }
    }
    Timer {
        id: issueTimer
        interval: 350
        onTriggered: cs.issues = Engine.choiceIssues(cs.storyText, cs.editFrom ? cs.editFrom : cs.storyLine + 1, cs.editFrom ? cs.editTo : cs.storyLine, ed.text)
    }

    // ------------------------------------------------------------------ the card writes the text
    function writeOpt(i, o) {
        const ln = options[i].line
        const indent = ed.lineText(ln).match(/^\s*/)[0]
        ed.setLineText(ln, indent + Engine.choiceItemLine(o))
    }
    function setOpt(k, v) {
        if (!curOpt) return
        const o = Object.assign({}, curOpt)
        o[k] = v
        writeOpt(cur, o)
    }
    function setOptMany(kv) {
        if (!curOpt) return
        writeOpt(cur, Object.assign({}, curOpt, kv))
    }
    // «Что ответят»: the first line under the option (a new one when there is none yet)
    function setReply(t) {
        if (!curOpt) return
        const o = curOpt
        if (o.firstBody) {
            const bi = ed.lineText(o.firstBody).match(/^\s*/)[0]
            ed.setLineText(o.firstBody, bi + t)
        } else if (t.trim() !== "") {
            const ind = ed.lineText(o.line).match(/^\s*/)[0] + "    "
            ed.setLineText(o.line, ed.lineText(o.line) + "\n" + ind + t)
        }
    }
    function setPoint(k, field, v) {
        const p = (curOpt.points || []).map(x => Object.assign({}, x))
        if (!p[k]) return
        p[k][field] = v
        setOpt("points", p)
    }
    function addPoint() {
        const p = (curOpt.points || []).slice()
        p.push({ v: defaultVar(), n: 1 })
        setOpt("points", p)
    }
    function removePoint(k) { setOpt("points", (curOpt.points || []).filter((x, j) => j !== k)) }
    function setHead(k, v) {
        const h = Object.assign({ style: "es", secs: 8, loop: false, random: false }, head)
        h[k] = v
        if (k === "loop" && v) h.random = false
        if (k === "random" && v) h.loop = false
        if (!head.line) { ed.setText(Engine.choiceHeadLine(h) + "\n" + ed.text); return }
        const indent = ed.lineText(head.line).match(/^\s*/)[0]
        ed.setLineText(head.line, indent + Engine.choiceHeadLine(h))
    }
    // the names the story counts with, for a new «+1» / lock
    function defaultVar() {
        const used = []
        for (const o of options) for (const p of (o.points || [])) used.push(p.v)
        if (used.length) return used[0]
        const who = lastSpeaker()
        return who && who !== "Я" ? who : "Славя"
    }
    function lastSpeaker() {
        const lines = ed.text.split("\n")
        const at = Math.min(lines.length, ed.currentLine)
        for (let i = at - 1; i >= 0; --i) {
            const m = lines[i].match(/^\s*([^\s\-\[:#][^:]{0,30}):\s/)
            if (m && ["выбор", "время вышло"].indexOf(m[1].toLowerCase()) < 0) return m[1]
        }
        return "Славя"
    }
    // before the choice: «фон …» / «музыка …» lines above «выбор» (replaced when there already is one)
    function setBefore(word, line) {
        const lines = ed.text.split("\n")
        const h = head.line ? head.line - 1 : 0
        for (let i = 0; i < h; ++i) if (lines[i].trim().split(/\s+/)[0] === word) { ed.setLineText(i + 1, line); return }
        ed.setLineText(h + 1, line + "\n" + lines[h])
    }

    // ------------------------------------------------------------------ «+»: what goes right under a line
    function addUnder(what) {
        const ln = ed.currentLine
        const t = ed.lineText(ln)
        const indent = t.match(/^\s*/)[0]
        const inOption = cur >= 0
        const opt = curOpt
        const optIndent = inOption ? ed.lineText(opt.line).match(/^\s*/)[0] : indent
        const body = inOption ? (ln === opt.line ? indent + "    " : indent) : indent
        switch (what) {
        case "say": ed.insertAfterLine(ln, body + lastSpeaker() + ": "); break
        case "narr": ed.insertAfterLine(ln, body + "текст "); break
        case "item": ed.insertAfterLine(ln, body + "предмет ключ | Ключ"); break
        case "jump": ed.insertAfterLine(ln, body + "переход "); break
        case "end": ed.insertAfterLine(ln, body + "конецигры"); break
        case "bg":                                     // another place: the background picker, the line comes after the pick
            pickLine = ln
            pickIndent = body
            pickFor = -3
            picker.tab = 1
            picker.tag = ""
            picker.open()
            break
        case "endchoice": {                            // the options are over: close the choice, or go on after it
            const lines = ed.text.split("
")
            const headIndent = head.line ? ed.lineText(head.line).match(/^\s*/)[0] : ""
            if ((outline.end || 0) > lines.length) ed.insertAfterLine(options.length ? options[options.length - 1].bodyTo : lines.length, headIndent + "конецвыбора")
            else ed.insertAfterLine(outline.end, headIndent)
            break
        }
        case "choice": ed.insertAfterLine(ln, [body + "выбор", body + "- Да", body + "- Нет", body + "конецвыбора"].join("\n")); break
        case "points": if (inOption) addPoint(); else ed.insertAfterLine(ln, body + "прибавить " + defaultVar() + " 1"); break
        case "remember":
            if (inOption) setOpt("remember", (opt.remember || []).concat([lastSpeaker()]))
            else ed.insertAfterLine(ln, body + "запомнит " + lastSpeaker())
            break
        case "option": {
            const after = inOption ? opt.bodyTo : Math.max(ln, (outline.end || 2) - 1)
            ed.insertAfterLine(after, optIndent + "- ")
            break
        }
        }
    }
    Menu {
        id: addMenu
        MenuItem { text: "Реплика героя"; onTriggered: cs.addUnder("say") }
        MenuItem { text: "Слова рассказчика"; onTriggered: cs.addUnder("narr") }
        MenuItem { text: cs.cur >= 0 ? "Очки этому варианту" : "Изменить очки"; onTriggered: cs.addUnder("points") }
        MenuItem { text: "«…это запомнит»"; onTriggered: cs.addUnder("remember") }
        MenuItem { text: "Выдать предмет"; onTriggered: cs.addUnder("item") }
        MenuItem { text: "Сменить локацию (фон)…"; onTriggered: cs.addUnder("bg") }
        MenuItem { text: "Новый выбор внутри"; onTriggered: cs.addUnder("choice") }
        Menu {
            id: jumpMenu
            title: "Перейти в сцену"
            Instantiator {
                model: cs.sceneList
                delegate: MenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: cs.addJump(modelData)
                }
                onObjectAdded: (index, object) => jumpMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => jumpMenu.removeItem(object)
            }
            MenuSeparator {}
            MenuItem { text: "Новая сцена…"; onTriggered: cs.addUnder("jump") }
        }
        MenuItem { text: "Конец игры"; onTriggered: cs.addUnder("end") }
        MenuSeparator {}
        MenuItem { text: "＋ Новый вариант"; onTriggered: cs.addUnder("option") }
        MenuItem { text: "Конец выбора — дальше история"; onTriggered: cs.addUnder("endchoice") }
    }
    // the story's scenes for «Перейти в сцену», read when the studio opens
    property var sceneList: []
    property int pickLine: 0
    property string pickIndent: ""
    function addJump(scene) {
        const ln = ed.currentLine
        const indent = ed.lineText(ln).match(/^\s*/)[0]
        const body = cur >= 0 && ln === curOpt.line ? indent + "    " : indent
        ed.insertAfterLine(ln, body + "переход " + scene)
    }

    ImagePicker {
        id: picker
        onPicked: (img) => {
            if (cs.pickFor === -2) {
                if (!/^(bg|cg) /.test(img)) { Engine.toast("Для фона выбери фон или CG", 1); return }
                cs.setBefore(img.indexOf("cg ") === 0 ? "цг" : "фон", (img.indexOf("cg ") === 0 ? "цг " + img.substring(3) : "фон " + img.substring(3)) + " fade")
            } else if (cs.pickFor === -3) {
                if (!/^(bg|cg) /.test(img)) { Engine.toast("Для локации выбери фон или CG", 1); return }
                ed.insertAfterLine(cs.pickLine, cs.pickIndent + (img.indexOf("cg ") === 0 ? "цг " : "фон ") + img.substring(3) + " fade")
            } else if (cs.pickFor >= 0 && cs.pickFor === cs.cur) {
                cs.setOpt("image", img)
            }
        }
    }

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }

    component Label2: Text { color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
    // a card field: the line is the truth - the field shows it, and while the writer types in it the line follows
    component CardField: TextField {
        id: cf
        property string value: ""
        signal commit(string text)
        implicitHeight: 32
        placeholderTextColor: Theme.faint
        color: Theme.text
        font.family: Theme.ui; font.pixelSize: 14
        selectByMouse: true
        background: Rectangle { radius: 7; color: Theme.bg; border.color: cf.activeFocus ? Theme.accent : Theme.line }
        onValueChanged: if (!activeFocus && text !== value) text = value
        Component.onCompleted: text = value
        onTextEdited: commit(text)
        onActiveFocusChanged: if (!activeFocus && text !== value) text = value
    }
    component Stepper: Row {
        id: st
        property real value: 0
        property real min: -99
        property real max: 99
        property string prefix: ""
        signal changed(real v)
        spacing: 2
        PillButton { dark: true; text: "−"; implicitWidth: 28; onClicked: st.changed(Math.max(st.min, st.value - 1)) }
        Text {
            width: 44; height: 30
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            text: st.prefix + (st.value > 0 && st.min < 0 ? "+" : "") + st.value
            color: st.min < 0 ? (st.value > 0 ? Theme.good : st.value < 0 ? Theme.bad : Theme.dim) : Theme.gold
            font.family: Theme.mono; font.pixelSize: 16; font.bold: true
        }
        PillButton { dark: true; text: "+"; implicitWidth: 28; onClicked: st.changed(Math.min(st.max, st.value + 1)) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        // ---------------------------------------------------------------- top: how the menu looks, how it behaves
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            InkText { text: "Выбор"; size: 30; color: Theme.gold }
            Text {
                Layout.fillWidth: true
                text: "пиши текстом — окно подсказывает. Курсор на варианте — справа его карточка"
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14
                elide: Text.ElideRight
            }
            Repeater {
                model: [["es", "Как в БЛ"], ["images", "Картинки 7ДЛ"], ["timed", "На время"], ["buttons", "Кнопки"]]
                PillButton {
                    required property var modelData
                    text: modelData[1]
                    accent: cs.style === modelData[0]
                    dark: cs.style !== modelData[0]
                    onClicked: cs.setHead("style", modelData[0])
                }
            }
            Stepper {
                visible: cs.style === "timed"
                value: Math.round(cs.head.secs || 8)
                min: 2; max: 60
                prefix: ""
                onChanged: (v) => cs.setHead("secs", v)
                ToolTip.visible: false
            }
            Rectangle { width: 1; height: 26; color: Theme.line }
            PillButton {
                text: "↻ По кругу"
                accent: !!cs.head.loop; dark: !cs.head.loop
                onClicked: cs.setHead("loop", !cs.head.loop)
                ToolTip.visible: hovered
                ToolTip.text: "Расспросы: выбранный вариант пропадает, меню возвращается, пока не выберут [выход] или не кончатся вопросы"
            }
            PillButton {
                text: "🎲 Наугад"
                accent: !!cs.head.random; dark: !cs.head.random
                onClicked: cs.setHead("random", !cs.head.random)
                ToolTip.visible: hovered
                ToolTip.text: "Игра сама берёт один из вариантов — случайные события, каждый раз по-разному"
            }
            PillButton { dark: true; text: "✕"; onClicked: cs.close() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            // ------------------------------------------------------------ left 60%: THE TEXT
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 6
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label2 { text: "Перед выбором:" }
                    PillButton { dark: true; text: "🖼 фон"; onClicked: { cs.pickFor = -2; picker.tab = 1; picker.tag = ""; picker.open() } }
                    DarkCombo {
                        Layout.preferredWidth: 230
                        model: ["♪ музыка как есть"].concat(Engine.music.filter(m => !m.custom).map(m => m.word))
                        onActivated: if (currentIndex > 0) cs.setBefore("музыка", "музыка " + currentText)
                    }
                    Item { Layout.fillWidth: true }
                    Label2 { text: "[ — подсказки   ·   Ctrl+Пробел — ещё"; color: Theme.faint }
                }
                CodeEditor {
                    id: ed
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    lineTool: true
                    issues: cs.issues
                    bands: cs.options.map((o, i) => ({ from: o.line, to: o.bodyTo, color: cs.colorOf(i), strong: i === cs.cur }))
                    badges: cs.options.filter(o => (o.badges || []).length).map(o => ({ line: o.line, parts: o.badges }))
                    radius: 10
                    border.color: Theme.line
                    onEdited: reparseTimer.restart()
                    onCurrentLineChanged: previewTimer.restart()
                    onLineToolClicked: (line, anchor) => addMenu.popup(anchor, anchor.width + 4, 0)
                    Timer { id: reparseTimer; interval: 30; onTriggered: cs.reparse() }
                }
                // the mistakes: the one on the cursor's line first, click = go there
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 32
                    radius: 8
                    readonly property var shown: {
                        const bad = cs.issues.filter(i => i.level >= 1)
                        const here = bad.filter(i => i.line === ed.currentLine)
                        return here.length ? here[0] : (bad.length ? bad[0] : null)
                    }
                    readonly property int count: cs.issues.filter(i => i.level >= 1).length
                    color: shown ? (shown.level >= 2 ? "#33ff5566" : "#33ffc857") : "#1450fa7b"
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        text: parent.shown ? (parent.shown.level >= 2 ? "⛔ " : "⚠ ") + "строка " + parent.shown.line + ": " + parent.shown.msg
                                             + (parent.count > 1 ? "   (ещё " + (parent.count - 1) + ")" : "")
                                           : "✓ Всё понятно: " + cs.options.length + " вар."
                        color: parent.shown ? Theme.text : Theme.good
                        font.family: Theme.ui; font.pixelSize: 14
                    }
                    MouseArea { anchors.fill: parent; enabled: !!parent.shown; cursorShape: Qt.PointingHandCursor; onClicked: ed.gotoLine(parent.shown.line) }
                }
            }

            // ------------------------------------------------------------ right 40%: the game's menu + the options
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 4
                spacing: 8
                Rectangle {
                    id: pv
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 16
                    color: "black"
                    radius: 6
                    clip: true
                    SmoothImage { anchors.fill: parent; source: cs.previewSrc; fade: 90 }
                    // the option under the cursor (and under the mouse) framed in its own colour, as in the text
                    Repeater {
                        model: cs.rects.length
                        Rectangle {
                            required property int index
                            readonly property var r: cs.rects[index] || ({ x: 0, y: 0, w: 0, h: 0 })
                            visible: index === cs.cur || index === cs.previewHover
                            x: r.x * pv.width / 1920 - 3
                            y: r.y * pv.height / 1080 - 2
                            width: r.w * pv.width / 1920 + 6
                            height: r.h * pv.height / 1080 + 4
                            radius: 6
                            color: "transparent"
                            border.width: index === cs.cur ? 3 : 1.5
                            border.color: cs.colorOf(index)
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: cs.previewHover >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                        function hit(mx, my) {
                            const sx = mx * 1920 / width, sy = my * 1080 / height
                            for (let i = 0; i < cs.rects.length; ++i) {
                                const r = cs.rects[i]
                                if (sx >= r.x && sx < r.x + r.w && sy >= r.y && sy < r.y + r.h) return i
                            }
                            return -1
                        }
                        onPositionChanged: (m) => { const h = hit(m.x, m.y); if (h !== cs.previewHover) { cs.previewHover = h; previewTimer.restart() } }
                        onExited: { cs.previewHover = -1; previewTimer.restart() }
                        onClicked: (m) => { const h = hit(m.x, m.y); if (h >= 0 && h < cs.options.length) ed.gotoLine(cs.options[h].line) }
                    }
                }
                Label2 {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: (cs.style === "images" ? "Картинки как в 7ДЛ: у каждого варианта своя полоса" : cs.style === "timed" ? "Как в Telltale: полоса тает, не успел — «время вышло -> сцена»"
                          : cs.style === "buttons" ? "Тёмные кнопки" : "Родное меню выбора БЛ, цвета по времени суток")
                          + " · клик по варианту — к его строке · в игре жмутся и цифрами 1–9"
                }

                // the options: one thin row each, the one under the cursor opened as its card
                ListView {
                    id: optList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: cs.options.length
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    currentIndex: cs.cur
                    onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                    delegate: Rectangle {
                        id: row
                        required property int index
                        readonly property var o: cs.options[index] || ({})
                        readonly property bool open: index === cs.cur
                        width: optList.width - 10
                        height: col.implicitHeight + 12
                        radius: 9
                        color: open ? Theme.panel3 : Theme.panel2
                        border.color: open ? cs.colorOf(index) : Theme.line
                        border.width: open ? 2 : 1
                        Rectangle { x: 0; y: 6; width: 4; height: parent.height - 12; radius: 2; color: cs.colorOf(row.index) }
                        ColumnLayout {
                            id: col
                            x: 10; y: 6
                            width: parent.width - 20
                            spacing: 6
                            // the thin row
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 26
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 10
                                    width: parent.width
                                    Text { text: (row.index + 1) + "."; color: cs.colorOf(row.index); font.family: Theme.mono; font.pixelSize: 14; font.bold: true }
                                    Text {
                                        width: Math.min(implicitWidth, parent.width * 0.55)
                                        elide: Text.ElideRight
                                        text: row.o.caption || "…"
                                        color: Theme.text; font.family: Theme.ui; font.pixelSize: 15; font.bold: true
                                    }
                                    Repeater {
                                        model: row.o.points || []
                                        Text {
                                            required property var modelData
                                            text: (modelData.n > 0 ? "+" : "") + modelData.n + " " + modelData.v
                                            color: modelData.n > 0 ? Theme.good : Theme.bad
                                            font.family: Theme.mono; font.pixelSize: 13
                                        }
                                    }
                                    Text { visible: !!row.o.need; text: "🔒 " + (row.o.needVar ? row.o.needVar + " " + row.o.needN : row.o.need); color: Theme.gold; font.family: Theme.mono; font.pixelSize: 13 }
                                    Text { visible: !!row.o.cond; text: "если " + row.o.cond; color: "#bd93f9"; font.family: Theme.mono; font.pixelSize: 13 }
                                    Text { visible: (row.o.remember || []).length > 0; text: "★ " + (row.o.remember || []).join(", "); color: "#8be9fd"; font.family: Theme.ui; font.pixelSize: 13 }
                                    Text { visible: !!row.o.target; text: "→ " + row.o.target; color: "#ff8a80"; font.family: Theme.mono; font.pixelSize: 13 }
                                    Text { visible: !!row.o.exit; text: "выход"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                                    Text { visible: (row.o.body || 0) > 0; text: "· " + row.o.body + " стр."; color: Theme.faint; font.family: Theme.ui; font.pixelSize: 13 }
                                }
                                MouseArea { anchors.fill: parent; enabled: !row.open; cursorShape: Qt.PointingHandCursor; onClicked: ed.gotoLine(row.o.line) }
                            }
                            // the card
                            Loader {
                                Layout.fillWidth: true
                                active: row.open
                                visible: active
                                sourceComponent: card
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    PillButton { dark: true; text: "＋ Вариант"; onClicked: { if (cs.cur < 0 && cs.options.length) ed.gotoLine(cs.options[cs.options.length - 1].line); cs.addUnder("option") } }
                    Item { Layout.fillWidth: true }
                    PillButton { dark: true; text: "Отмена"; onClicked: cs.close() }
                    PillButton { accent: true; text: cs.editFrom ? "Сохранить выбор" : "Вставить в сцену"; onClicked: cs.commit() }
                }
            }
        }
    }

    // ---------------------------------------------------------------- the card of the option under the cursor
    Component {
        id: card
        ColumnLayout {
            spacing: 7
            readonly property var o: cs.curOpt || ({})
            // a newcomer needs the text, the points and the answer; the rest opens when wanted (or is set already)
            readonly property bool more: cs.moreOpen || cs.hasMore(o)
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10
                rowSpacing: 7

                Label2 { text: "Текст" }
                CardField { Layout.fillWidth: true; value: o.caption || ""; placeholderText: "что видит игрок"; onCommit: (t) => cs.setOpt("caption", t) }

                Label2 { text: "Очки"; Layout.alignment: Qt.AlignTop; topPadding: 7 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: (o.points || []).length
                        RowLayout {
                            required property int index
                            readonly property var p: (o.points || [])[index] || ({})
                            spacing: 6
                            CardField { Layout.preferredWidth: 150; value: p.v || ""; placeholderText: "кому"; onCommit: (t) => { if (t.trim()) cs.setPoint(index, "v", t.trim().replace(/\s+/g, "_")) } }
                            Stepper { value: p.n || 0; onChanged: (v) => cs.setPoint(index, "n", v) }
                            PillButton { dark: true; text: "✕"; implicitWidth: 30; onClicked: cs.removePoint(index) }
                        }
                    }
                    PillButton { dark: true; text: "＋ очки"; onClicked: cs.addPoint() }
                }

                Label2 { text: "Что ответят" }
                CardField {
                    Layout.fillWidth: true
                    value: o.firstBodyText || ""
                    placeholderText: "Славя: Отлично, пойдём! — первая строка под вариантом"
                    onCommit: (t) => cs.setReply(t)
                }

                Item { Layout.columnSpan: 2; Layout.fillWidth: true; implicitHeight: 26
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Text {
                            text: (more ? "▾ " : "▸ ") + "Условия и последствия"
                            color: Theme.gold; font.family: Theme.ui; font.pixelSize: 14; font.bold: true
                        }
                        Text {
                            text: cs.hasMore(o) ? "есть настройки" : "замок, условие, «запомнит», сцена" + (cs.style === "images" ? ", картинка" : "")
                            color: Theme.faint; font.family: Theme.ui; font.pixelSize: 13
                        }
                    }
                    MouseArea { anchors.fill: parent; enabled: !cs.hasMore(o); cursorShape: Qt.PointingHandCursor; onClicked: cs.moreOpen = !cs.moreOpen }
                }

                Label2 { visible: more; text: "Замок"; Layout.alignment: Qt.AlignTop; topPadding: 7 }
                ColumnLayout {
                    visible: more
                    Layout.fillWidth: true
                    spacing: 4
                    PillButton {
                        visible: !o.need
                        dark: true
                        text: "🔒 Закрыть, пока не хватает очков"
                        onClicked: cs.setOptMany({ needVar: cs.defaultVar(), needN: 3, need: "" })
                    }
                    RowLayout {
                        visible: !!o.needVar
                        spacing: 6
                        Label2 { text: "нужно" }
                        CardField { Layout.preferredWidth: 150; value: o.needVar || ""; onCommit: (t) => { if (t.trim()) cs.setOptMany({ needVar: t.trim().replace(/\s+/g, "_"), needN: o.needN }) } }
                        Stepper { value: o.needN || 0; min: 0; max: 999; onChanged: (v) => cs.setOptMany({ needVar: o.needVar, needN: v }) }
                        PillButton { dark: true; text: "✕"; implicitWidth: 30; onClicked: cs.setOptMany({ needVar: "", need: "", hint: "" }) }
                    }
                    CardField {                       // a lock in words («славя 3 и алиса 2», «предмет ключ»)
                        visible: !!o.need && !o.needVar
                        Layout.fillWidth: true
                        value: o.need || ""
                        onCommit: (t) => cs.setOptMany({ need: t, needVar: "" })
                    }
                    CardField {
                        visible: !!o.need
                        Layout.fillWidth: true
                        value: o.hint || ""
                        placeholderText: "подсказка игроку под замком (пусто — «нужно: Славя 3»)"
                        onCommit: (t) => cs.setOpt("hint", t)
                    }
                }

                Label2 { visible: more; text: "Появится, если" }
                CardField { visible: more; Layout.fillWidth: true; value: o.cond || ""; placeholderText: "пусто — всегда. ключ / славя 3 / предмет фонарик / не ссора"; onCommit: (t) => cs.setOpt("cond", t) }

                Label2 { visible: more; text: "Запомнит" }
                CardField {
                    visible: more
                    Layout.fillWidth: true
                    value: (o.remember || []).join(", ")
                    placeholderText: "кто: «Славя это запомнит» (можно через запятую)"
                    onCommit: (t) => cs.setOpt("remember", t.split(",").map(x => x.trim()).filter(x => x))
                }

                Label2 { visible: more; text: "Потом сцена" }
                RowLayout {
                    visible: more
                    Layout.fillWidth: true
                    spacing: 8
                    CardField { Layout.preferredWidth: 200; value: o.target || ""; placeholderText: "пусто — история дальше"; onCommit: (t) => cs.setOpt("target", t.trim()) }
                    Label2 {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: !o.target ? "после строк варианта история идёт дальше сама"
                              : Engine.sceneNames(cs.storyText).indexOf(o.target) >= 0 ? "сцена есть — вариант поведёт в неё" : "такой сцены пока нет — допиши её в истории"
                    }
                }

                Label2 { text: "Картинка"; visible: more && cs.style === "images" }
                RowLayout {
                    visible: more && cs.style === "images"
                    spacing: 8
                    AbstractButton {
                        implicitWidth: 72; implicitHeight: 72
                        onClicked: { cs.pickFor = cs.cur; picker.tab = (o.image || "").indexOf("bg ") === 0 ? 1 : (o.image || "").indexOf("cg ") === 0 ? 2 : 0; picker.tag = ""; picker.open() }
                        background: Rectangle {
                            radius: 8; color: Theme.bg; clip: true
                            border.color: parent.hovered ? Theme.accent : Theme.line
                            Image {
                                anchors.fill: parent; anchors.margins: 1
                                visible: !!o.image
                                source: !o.image ? "" : (o.image.indexOf("bg ") === 0 || o.image.indexOf("cg ") === 0)
                                        ? "image://gb/" + o.image.substring(0, 2) + "/" + encodeURIComponent(o.image.substring(3))
                                        : "image://gb/face/" + encodeURIComponent(o.image)
                                sourceSize: Qt.size(144, 144); asynchronous: true
                                fillMode: Image.PreserveAspectCrop
                            }
                            Text { anchors.centerIn: parent; visible: !o.image; text: "＋"; color: Theme.faint; font.pixelSize: 22 }
                        }
                    }
                    Label2 { text: o.image || "герой, фон или CG для полосы этого варианта" }
                    PillButton { visible: !!o.image; dark: true; text: "✕"; onClicked: cs.setOpt("image", "") }
                }

                Label2 { text: "По кругу"; visible: more && !!cs.head.loop }
                RowLayout {
                    visible: more && !!cs.head.loop
                    spacing: 14
                    CheckBox { text: "Этим расспросы заканчиваются"; checked: !!o.exit; onToggled: cs.setOpt("exit", checked) }
                    CheckBox { text: "Не пропадает после выбора"; checked: !!o.always; onToggled: cs.setOpt("always", checked) }
                }

                Label2 { text: "Под вариантом" }
                RowLayout {
                    spacing: 8
                    Label2 { text: (o.body || 0) ? o.body + " стр. — видно в тексте слева" : "пока ничего: сразу дальше" }
                    PillButton { dark: true; text: "＋ добавить…"; onClicked: { ed.gotoLine(o.bodyTo || o.line); addMenu.popup() } }
                }
            }
        }
    }
}
