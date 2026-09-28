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
    signal edited()

    function setText(t) { programmatic = true; area.text = t; area.cursorPosition = 0; programmatic = false }
    function focusEditor() { area.forceActiveFocus() }

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
        const st = starts[Math.max(0, Math.min(starts.length - 1, n - 1))]
        area.cursorPosition = st
        area.forceActiveFocus()
    }
    function lineText(n) {
        const b = lineBounds(starts[Math.max(0, Math.min(starts.length - 1, n - 1))])
        return area.text.substring(b.start, b.end)
    }
    // Swap line n for `cmd` (one line or a block), keeping the line's indentation.
    function replaceLine(n, cmd) {
        const b = lineBounds(starts[Math.max(0, Math.min(starts.length - 1, n - 1))])
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
            leftPadding: 10
            topPadding: 10
            bottomPadding: 200
            tabStopDistance: 32
            background: null
            placeholderText: "Пиши историю…"
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
                if (e.key === Qt.Key_Space && (e.modifiers & Qt.ControlModifier)) { ce.suggestNow(); e.accepted = true }
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
                    y: (area.width, area.contentHeight, area.positionToRectangle(ce.starts[index]).y)
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
