import QtQuick
import QtQuick.Controls
import GenryBL

// The story editor: line numbers that follow word wrap, syntax colours from the command
// table, lint markers, and context completion (показать → персонаж → эмоция → позиция).
Rectangle {
    id: ce
    color: Theme.bg
    property alias text: area.text
    property var issues: []
    readonly property int currentLine: Engine.lineAt(area.text, area.cursorPosition)
    readonly property var starts: Engine.lineStarts(area.text)
    property bool programmatic: false
    property bool lineTool: false          // «+» on the current line (the choice studio): what to add right under it
    // a choice's options: a band of the option's colour from its line to the end of its lines ({from, to, color, strong})
    property var bands: []
    // little marks after an option's line, the mechanics at a glance: {line, parts: [{t, c}]} («🔒», «★», «+1», «↪»)
    property var badges: []
    signal edited()
    signal lineToolClicked(int line, Item anchor)

    function setText(t) { programmatic = true; area.text = t; area.cursorPosition = 0; programmatic = false }
    // --shot: type like a person - text at the cursor, "\n" = the Enter key (smart Enter first)
    function typeLikeAPerson(parts) {
        area.forceActiveFocus()
        for (const p of parts) {
            if (p === "\n") { if (!smartEnter()) area.insert(area.cursorPosition, "\n") }
            else area.insert(area.cursorPosition, p)
        }
    }
    function focusEditor() { area.forceActiveFocus() }
    function lineY(n) {                    // the top of line n on the page (after the last line: the bottom)
        if (n - 1 >= starts.length) return area.contentHeight - area.bottomPadding
        return area.positionToRectangle(Math.min(area.length, starts[Math.max(0, n - 1)])).y
    }
    function lineEndRect(n) {
        const end = n < starts.length ? starts[n] - 1 : area.text.length
        return area.positionToRectangle(Math.max(0, Math.min(area.length, end)))
    }

    // Enter the way a list is written: «выбор» opens the block with its first option, the text of an option goes on
    // under it, an empty line under an option is the next option, an empty «- » leaves the choice
    function smartEnter() {
        const pos = area.cursorPosition
        const b = lineBounds(pos)
        const line = area.text.substring(b.start, b.end)
        if (area.selectedText !== "" || area.text.substring(pos, b.end).trim() !== "") return false
        const indent = line.match(/^[ \t]*/)[0]
        const s = line.trim()
        const next = nextRealLine(b.end)
        if (/^выбор(\s.*)?$/i.test(s)) {
            if (/^-(?!>)/.test(next) || /^конецвыбора/i.test(next)) { area.insert(pos, "\n" + indent + "- "); return true }
            area.insert(b.end, "\n" + indent + "- \n" + indent + "конецвыбора")
            area.cursorPosition = b.end + 1 + indent.length + 2
            return true
        }
        if (/^-(?!>)\s*\S/.test(s)) { area.insert(pos, "\n" + indent + "    "); return true }
        if (s === "-") {                                   // an empty option: the options are over, on after «конецвыбора»
            const t = area.text
            let k = b.end, endOfEnd = -1
            while (k < t.length) {
                const e0 = t.indexOf("\n", k + 1)
                const e = e0 < 0 ? t.length : e0
                const l = t.substring(k + 1, e).trim()
                if (/^конецвыбора/i.test(l)) { endOfEnd = e; break }
                if (/^:/.test(l) || /^выбор/i.test(l)) break
                k = e
            }
            const from = b.start, to = b.end < t.length ? b.end + 1 : b.end
            area.remove(from, to)                          // the empty option's line goes away
            if (endOfEnd >= 0) {
                const at = endOfEnd - (to - from)
                area.insert(at, "\n")
                area.cursorPosition = at + 1
            } else {
                area.cursorPosition = from
            }
            return true
        }
        const oi = optionIndentAbove(b.start, indent)
        if (s === "" && indent.length > 0 && oi !== null) {
            area.remove(b.start, b.end)
            area.insert(b.start, oi + "- ")
            area.cursorPosition = b.start + oi.length + 2
            return true
        }
        if (indent.length > 0 && s !== "") { area.insert(pos, "\n" + indent); return true }
        return false
    }
    function nextRealLine(from) {
        const t = area.text
        let k = from
        while (k < t.length) {
            const e = t.indexOf("\n", k + 1) < 0 ? t.length : t.indexOf("\n", k + 1)
            const l = t.substring(k + 1, e).trim()
            if (l !== "") return l
            k = e
        }
        return ""
    }
    // the indentation of the option this indented line belongs to, or null
    function optionIndentAbove(start, indent) {
        const t = area.text
        let e = start - 1
        while (e > 0) {
            const s = t.lastIndexOf("\n", e - 1) + 1
            const l = t.substring(s, e)
            const li = l.match(/^[ \t]*/)[0]
            if (/^[ \t]*-(?!>)/.test(l) && li.length < indent.length) return li
            if (l.trim() !== "" && li.length === 0) return null
            e = s - 1
        }
        return null
    }

    // where line n starts, from the text as it is right now (`starts` may lag one edit behind inside a script)
    function lineStartPos(n) {
        const t = area.text
        let pos = 0
        for (let i = 1; i < n; ++i) {
            const nl = t.indexOf("\n", pos)
            if (nl < 0) break
            pos = nl + 1
        }
        return pos
    }
    function lineBounds(pos) {
        const t = area.text
        const s = t.lastIndexOf("\n", pos - 1) + 1
        let e = t.indexOf("\n", pos)
        if (e < 0) e = t.length
        return { start: s, end: e }
    }
    // Put a command on its own line under the cursor (or into the current empty line).
    function insertLine(cmd) {
        const b = lineBounds(area.cursorPosition)
        const cur = area.text.substring(b.start, b.end)
        let at
        if (cur.trim() === "") {
            area.remove(b.start, b.end)
            area.insert(b.start, cmd)
            at = b.start
        } else {
            area.insert(b.end, "\n" + cmd)
            at = b.end + 1
        }
        area.cursorPosition = at + cmd.length
        flash.flashAt(at)
        area.forceActiveFocus()
    }
    function gotoLine(n) {
        const st = lineStartPos(n)
        area.cursorPosition = st
        area.forceActiveFocus()
    }
    function lineText(n) {
        const b = lineBounds(lineStartPos(n))
        return area.text.substring(b.start, b.end)
    }
    // Line n becomes `t` as it is - the cursor stays where the writer is (a helper window rewrote the line)
    function setLineText(n, t) {
        const b = lineBounds(lineStartPos(n))
        if (area.text.substring(b.start, b.end) === t) return
        const cur = area.cursorPosition
        const delta = t.length - (b.end - b.start)
        programmatic = true
        area.remove(b.start, b.end)
        area.insert(b.start, t)
        area.cursorPosition = cur <= b.start ? cur : (cur >= b.end ? cur + delta : Math.min(cur, b.start + t.length))
        programmatic = false
    }
    // Lines from..to (1-based, inclusive) become `t` (a whole «выбор» block edited in its studio)
    function replaceLines(from, to, t) {
        const a = lineStartPos(from)
        const b = lineBounds(lineStartPos(to)).end
        area.remove(a, b)
        area.insert(a, t)
        area.cursorPosition = a
        flash.flashAt(a)
        area.forceActiveFocus()
    }
    // New line(s) right under line n; the cursor at the end of the first one, ready to type
    function insertAfterLine(n, t) {
        const b = lineBounds(lineStartPos(n))
        area.insert(b.end, "\n" + t)
        area.cursorPosition = b.end + 1 + t.split("\n")[0].length
        flash.flashAt(b.end + 1)
        area.forceActiveFocus()
    }
    // Swap line n for `cmd` (one line or a block), keeping the line's indentation.
    function replaceLine(n, cmd) {
        const b = lineBounds(lineStartPos(n))
        const indent = area.text.substring(b.start, b.end).match(/^\s*/)[0]
        const body = cmd.split("\n").map(l => l ? indent + l : l).join("\n")
        area.remove(b.start, b.end)
        area.insert(b.start, body)
        area.cursorPosition = b.start + body.length
        flash.flashAt(b.start)
        area.forceActiveFocus()
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.leftMargin: gutter.width
        contentWidth: width
        contentHeight: area.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        // the options' bands: each option in its own colour from its line to the end of its lines
        Repeater {
            model: ce.bands
            Item {
                required property var modelData
                readonly property real bandTop: (area.width, area.contentHeight, ce.starts, ce.lineY(modelData.from))
                x: 0; width: flick.width
                y: bandTop
                height: Math.max(4, (area.width, area.contentHeight, ce.starts, ce.lineY(modelData.to + 1)) - bandTop)
                Rectangle { anchors.fill: parent; visible: modelData.strong; color: modelData.color; opacity: 0.10 }
                Rectangle { x: 0; width: modelData.strong ? 4 : 2; height: parent.height; color: modelData.color; opacity: modelData.strong ? 1 : 0.55 }
            }
        }
        // current line
        Rectangle {
            x: 0; width: flick.width
            y: area.cursorRectangle.y
            height: area.cursorRectangle.height
            color: "#14ffffff"
            visible: area.activeFocus
        }
        // flash where a command was inserted
        Rectangle {
            id: flash
            x: 0; width: flick.width; height: 22
            color: Theme.accent
            opacity: 0
            function flashAt(pos) {
                y = area.positionToRectangle(pos).y
                height = area.positionToRectangle(pos).height
                anim.restart()
            }
            SequentialAnimation on opacity {
                id: anim
                running: false
                NumberAnimation { to: 0.35; duration: 90 }
                NumberAnimation { to: 0; duration: 700; easing.type: Easing.OutCubic }
            }
        }

        // the marks after an option's line
        Repeater {
            model: ce.badges
            Row {
                required property var modelData
                readonly property rect lineEnd: (area.width, area.contentHeight, ce.starts, ce.lineEndRect(modelData.line))
                z: 2
                spacing: 4
                x: Math.min(lineEnd.x + 16, flick.width - (ce.lineTool ? 44 : 8) - width)
                y: lineEnd.y + (lineEnd.height - height) / 2
                Repeater {
                    model: modelData.parts
                    Item {
                        required property var modelData
                        width: bt.implicitWidth + 10; height: 18
                        Rectangle { anchors.fill: parent; radius: 9; color: modelData.c; opacity: 0.18 }
                        Rectangle { anchors.fill: parent; radius: 9; color: "transparent"; border.color: modelData.c }
                        Text { id: bt; anchors.centerIn: parent; text: modelData.t; color: modelData.c; font.family: Theme.ui; font.pixelSize: 11; font.bold: true }
                    }
                }
            }
        }
        // «+» on the current line
        Rectangle {
            id: lineToolBtn
            z: 3
            visible: ce.lineTool
            x: flick.width - 36
            y: area.cursorRectangle.y + (area.cursorRectangle.height - height) / 2
            width: 28; height: 28; radius: 14
            color: toolArea.containsMouse ? Theme.accent : Theme.panel2
            border.color: Theme.accent
            Text { anchors.centerIn: parent; text: "+"; color: toolArea.containsMouse ? "#16240c" : Theme.accent; font.pixelSize: 18; font.bold: true }
            MouseArea { id: toolArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ce.lineToolClicked(ce.currentLine, lineToolBtn) }
            ToolTip.visible: toolArea.containsMouse
            ToolTip.text: qsTr("Добавить сюда: реплику, очки, предмет, новый выбор…")
        }

        TextArea.flickable: TextArea {
            id: area
            wrapMode: TextEdit.Wrap
            font.family: Theme.mono
            font.pixelSize: 16
            color: Theme.text
            selectionColor: "#77ff5fae"
            selectedTextColor: "white"
            selectByMouse: true
            persistentSelection: true
            rightPadding: ce.lineTool ? 44 : 6
            leftPadding: 10
            topPadding: 10
            bottomPadding: 200
            tabStopDistance: 32
            background: null
            placeholderText: qsTr("Пиши историю…")
            placeholderTextColor: Theme.faint
            onTextChanged: {
                ce.edited()
                if (!ce.programmatic && activeFocus) completeTimer.restart()
            }
            onCursorPositionChanged: if (!completeTimer.running && completion.visible && !completion.accepting) completion.close()

            Keys.onPressed: (e) => {
                if (completion.visible) {
                    if (e.key === Qt.Key_Down) { list.incrementCurrentIndex(); e.accepted = true; return }
                    if (e.key === Qt.Key_Up) { list.decrementCurrentIndex(); e.accepted = true; return }
                    if (e.key === Qt.Key_Tab || e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { completion.accept(list.currentIndex); e.accepted = true; return }
                    if (e.key === Qt.Key_Escape) { completion.close(); e.accepted = true; return }
                }
                if (e.key === Qt.Key_Space && (e.modifiers & Qt.ControlModifier)) { ce.suggestNow(); e.accepted = true; return }
                if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && !(e.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier))) {
                    if (ce.smartEnter()) e.accepted = true
                }
            }
        }
    }

    // ---- gutter: numbers + lint markers (follow wrapped lines)
    Rectangle {
        id: gutter
        width: 58
        height: parent.height
        color: Theme.bg2
        clip: true
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }
        Item {
            y: -flick.contentY
            Repeater {
                model: ce.starts.length
                delegate: Item {
                    readonly property int ln: index + 1
                    readonly property var issue: {
                        let worst = null
                        for (const i of ce.issues) if (i.line === ln && (!worst || i.level > worst.level)) worst = i
                        return worst
                    }
                    y: (area.width, area.contentHeight, area.positionToRectangle(Math.min(area.length, ce.starts[index])).y)
                    width: gutter.width
                    height: 20
                    Text {
                        anchors.right: parent.right; anchors.rightMargin: 12
                        y: 1
                        text: ln
                        color: ln === ce.currentLine ? Theme.gold : Theme.faint
                        font.family: Theme.mono
                        font.pixelSize: 13
                        font.bold: ln === ce.currentLine
                    }
                    Rectangle {
                        visible: parent.issue !== null && parent.issue.level >= 1
                        x: 6; y: 5; width: 9; height: 9; radius: 5
                        color: parent.issue ? Theme.levelColor(parent.issue.level) : "transparent"
                        ToolTip.visible: dotArea.containsMouse
                        ToolTip.text: parent.issue ? parent.issue.msg : ""
                        MouseArea { id: dotArea; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true }
                    }
                }
            }
        }
    }

    StoryHighlighter { document: area.textDocument; issues: ce.issues }

    // ---- completion
    Timer { id: completeTimer; interval: 140; onTriggered: ce.suggestNow() }
    function suggestNow() {
        const b = lineBounds(area.cursorPosition)
        const lineText = area.text.substring(b.start, b.end)
        const col = area.cursorPosition - b.start
        if (lineText.substring(0, col).trim() === "" ) { completion.close(); return }
        const res = Engine.suggest(lineText, col, area.text)
        if (!res.items.length) { completion.close(); return }
        completion.replaceFrom = b.start + res.start
        list.model = res.items
        list.currentIndex = 0
        const r = area.cursorRectangle
        completion.x = Math.min(gutter.width + r.x, ce.width - completion.width - 8)
        completion.y = r.y + r.height - flick.contentY + 4
        if (completion.y + completion.height > ce.height) completion.y = r.y - flick.contentY - completion.height - 4
        completion.open()
    }

    Popup {
        id: completion
        property int replaceFrom: 0
        property bool accepting: false
        width: 320
        height: Math.min(list.contentHeight + 12, 280)
        padding: 6
        focus: false
        closePolicy: Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.panel2; radius: 10; border.color: Theme.accent; border.width: 1 }
        function accept(i) {
            const item = list.model[i]
            if (!item) return
            accepting = true
            let t = item.text
            if (!t.endsWith(":") || item.kind !== "say") t += " "
            else t += " "
            area.remove(replaceFrom, area.cursorPosition)
            area.insert(replaceFrom, t)
            area.cursorPosition = replaceFrom + t.length
            accepting = false
            close()
            completeTimer.restart()      // chain: after "показать " come the characters
        }
        ListView {
            id: list
            anchors.fill: parent
            clip: true
            highlightMoveDuration: 80
            highlight: Rectangle { color: "#33ff5fae"; radius: 6 }
            delegate: Item {
                width: list.width
                height: 28
                Row {
                    x: 8; spacing: 10
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: modelData.kind === "cmd" ? Theme.accent : modelData.kind === "char" || modelData.kind === "say" ? Theme.charColor(modelData.hint)
                             : modelData.kind === "scene" ? "#ff79c6" : modelData.kind === "bg" ? "#8be9fd" : "#50fa7b"
                    }
                    Text { text: modelData.text; color: Theme.text; font.family: Theme.mono; font.pixelSize: 15 }
                    Text { text: modelData.hint; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                }
                MouseArea { anchors.fill: parent; onClicked: completion.accept(index) }
            }
        }
    }
}
