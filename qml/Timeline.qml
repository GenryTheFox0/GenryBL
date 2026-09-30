import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Таймлайн» (src/core/Timeline): the scene under the cursor like a clip in a video editor. Every line is a beat;
// the background, each character (from «показать» to «убрать», cut where the emotion changes), the words, music,
// ambience, sounds, effects and the ways out lie on their tracks. A click on a clip = its line in the editor, a
// double click = its form, a drag along the beats = the line moves there. ▶ walks the scene beat by beat.
Rectangle {
    id: tl
    property var tdata: ({ scene: "", beats: [], tracks: [] })
    property int line: 1                        // the editor's cursor line
    property real beatW: 86
    property bool playing: false
    signal gotoLine(int line)
    signal editLine(int line)
    signal moveLine(int from, int to)
    signal addKeyframe(string tag)
    signal closeMe()

    readonly property var beats: tdata.beats || []
    readonly property int current: {
        let b = -1
        for (let i = 0; i < beats.length; ++i) if (beats[i] <= line) b = i
        return b
    }
    readonly property int headW: 150
    readonly property int rowH: 30
    color: Theme.bg2
    Rectangle { width: parent.width; height: 1; color: Theme.line }

    function kindColor(k) {
        return k === "bg" ? "#3b6ea5" : k === "char" ? "#9b59b6" : k === "say" ? "#4f7d52" : k === "music" ? "#c58b2b"
             : k === "amb" ? "#2f8f8f" : k === "sfx" ? "#5c8fc7" : k === "fx" ? "#c0508a" : k === "flow" ? "#b8573a" : "#5a6470"
    }

    // ---- the bar
    RowLayout {
        id: bar
        x: 10; width: parent.width - 20; height: 36
        spacing: 10
        Text { text: "🎞"; font.pixelSize: 18 }
        Text {
            text: qsTr("Таймлайн") + (tl.tdata.scene ? qsTr(" · сцена «") + tl.tdata.scene + "»" : "") + "   ·   " + tl.beats.length + qsTr(" шагов")
            color: Theme.text; font.family: Theme.ui; font.pixelSize: 14; font.bold: true
        }
        Item { Layout.fillWidth: true }
        PillButton { dark: true; text: tl.playing ? qsTr("■ Стоп") : qsTr("▶ По шагам"); onClicked: tl.playing = !tl.playing }
        PillButton { dark: true; text: "−"; onClicked: tl.beatW = Math.max(40, tl.beatW / 1.25) }
        PillButton { dark: true; text: "+"; onClicked: tl.beatW = Math.min(240, tl.beatW * 1.25) }
        PillButton { dark: true; text: "✕"; onClicked: tl.closeMe() }
    }
    Timer {
        running: tl.playing
        interval: 1300
        repeat: true
        onTriggered: {
            const next = tl.current + 1
            if (next >= tl.beats.length) { tl.playing = false; return }
            tl.gotoLine(tl.beats[next])
        }
    }

    // ---- the track heads (they scroll up and down with the lanes)
    Item {
        x: 0; y: bar.height + 22
        width: tl.headW
        height: tl.height - y
        clip: true
    Column {
        id: heads
        y: -lanes.contentY
        width: tl.headW
        Repeater {
            model: tl.tdata.tracks || []
            Rectangle {
                required property var modelData
                width: tl.headW; height: tl.rowH
                color: Theme.panel
                border.color: Theme.line
                Text {
                    x: 10; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 16; elide: Text.ElideRight
                    text: modelData.title
                    color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13
                }
            }
        }
    }
    }

    // ---- the lanes
    Flickable {
        id: lanes
        x: tl.headW; y: bar.height
        width: tl.width - tl.headW
        height: tl.height - bar.height
        clip: true
        contentWidth: Math.max(width, tl.beats.length * tl.beatW + 40)
        contentHeight: 22 + (tl.tdata.tracks || []).length * tl.rowH
        flickableDirection: Flickable.HorizontalAndVerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {}
        // the playhead in sight
        onContentWidthChanged: follow()
        Connections { target: tl; function onCurrentChanged() { lanes.follow() } }
        function follow() {
            const x = tl.current * tl.beatW
            if (x < contentX || x > contentX + width - tl.beatW) contentX = Math.max(0, Math.min(contentWidth - width, x - width / 3))
        }
        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: (e) => tl.beatW = Math.max(40, Math.min(240, tl.beatW * (e.angleDelta.y > 0 ? 1.12 : 1 / 1.12)))
        }

        // the ruler
        Repeater {
            model: tl.beats.length
            Rectangle {
                required property int index
                x: index * tl.beatW; y: 0
                width: tl.beatW; height: 22
                color: index === tl.current ? "#2a9bd35a" : "transparent"
                Rectangle { width: 1; height: parent.height; color: Theme.line }
                Text {
                    x: 4; anchors.verticalCenter: parent.verticalCenter
                    text: tl.beats[index]
                    color: index === tl.current ? Theme.accent : Theme.faint; font.family: Theme.mono; font.pixelSize: 11
                }
                MouseArea { anchors.fill: parent; onClicked: tl.gotoLine(tl.beats[index]) }
            }
        }
        // the tracks and their clips
        Repeater {
            model: tl.tdata.tracks || []
            Item {
                id: lane
                required property var modelData
                required property int index
                y: 22 + index * tl.rowH
                width: lanes.contentWidth; height: tl.rowH
                Rectangle { anchors.fill: parent; color: lane.index % 2 ? "#0cffffff" : "transparent" }
                Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.line; opacity: 0.5 }
                Repeater {
                    model: lane.modelData.clips
                    Rectangle {
                        id: clipBox
                        required property var modelData
                        property real dragX: 0
                        x: modelData.from * tl.beatW + 2 + dragX
                        y: 3
                        width: Math.max(10, (modelData.to - modelData.from) * tl.beatW - 4)
                        height: tl.rowH - 6
                        radius: 6
                        z: ca.drag.active ? 5 : 1
                        color: tl.kindColor(modelData.kind)
                        opacity: ca.drag.active ? 0.8 : 1
                        border.width: modelData.line === tl.line ? 2 : 0
                        border.color: Theme.gold
                        Text {
                            x: 6; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 10; elide: Text.ElideRight
                            text: clipBox.modelData.label
                            color: "white"; font.family: Theme.ui; font.pixelSize: 12
                        }
                        MouseArea {
                            id: ca
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: clipBox.modelData.movable ? (drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.PointingHandCursor
                            drag.target: clipBox.modelData.movable ? dragProxy : null
                            drag.axis: Drag.XAxis
                            drag.threshold: 8
                            property real startX: 0
                            onPressed: startX = dragProxy.x = 0
                            onPositionChanged: if (drag.active) clipBox.dragX = dragProxy.x
                            onReleased: {
                                if (clipBox.dragX !== 0) {
                                    const beat = Math.round((clipBox.modelData.from * tl.beatW + clipBox.dragX) / tl.beatW)
                                    const target = Math.max(0, Math.min(tl.beats.length, beat > clipBox.modelData.from ? beat + 1 : beat))
                                    clipBox.dragX = 0
                                    if (beat !== clipBox.modelData.from) {
                                        const to = target >= tl.beats.length ? tl.beats[tl.beats.length - 1] + 1 : tl.beats[target]
                                        tl.moveLine(clipBox.modelData.line, to)
                                    }
                                }
                            }
                            onClicked: tl.gotoLine(clipBox.modelData.line)
                            onDoubleClicked: tl.editLine(clipBox.modelData.line)
                            ToolTip.visible: containsMouse && !drag.active
                            ToolTip.delay: 500
                            ToolTip.text: qsTr("стр. ") + clipBox.modelData.line + ": " + clipBox.modelData.label +
                                          (clipBox.modelData.movable ? qsTr("   ·   тащи — переставить строку") : "")
                        }
                        Item { id: dragProxy }
                        // the right edge: stretch / shorten the clip - the line that ends it moves
                        readonly property bool stretchy: clipBox.modelData.to < tl.beats.length &&
                                                         ["say", "flow", "moment"].indexOf(clipBox.modelData.kind) < 0
                        property real stretchX: 0
                        Rectangle {
                            visible: clipBox.stretchy
                            anchors.right: parent.right; anchors.rightMargin: -2 - clipBox.stretchX
                            width: 8; height: parent.height; radius: 3
                            color: edge.containsMouse || edge.drag.active ? Theme.gold : "#40ffffff"
                            MouseArea {
                                id: edge
                                anchors.fill: parent; anchors.margins: -3
                                hoverEnabled: true
                                cursorShape: Qt.SizeHorCursor
                                drag.target: edgeProxy
                                drag.axis: Drag.XAxis
                                drag.threshold: 4
                                onPressed: edgeProxy.x = 0
                                onPositionChanged: if (drag.active) clipBox.stretchX = edgeProxy.x
                                onReleased: {
                                    const to = clipBox.modelData.to
                                    const end = Math.max(clipBox.modelData.from + 1, Math.round((to * tl.beatW + clipBox.stretchX) / tl.beatW))
                                    clipBox.stretchX = 0
                                    if (end === to) return
                                    const ender = tl.beats[to]
                                    const at = end > to ? end + 1 : end
                                    tl.moveLine(ender, at >= tl.beats.length ? tl.beats[tl.beats.length - 1] + 1 : tl.beats[at])
                                }
                                ToolTip.visible: containsMouse && !drag.active
                                ToolTip.delay: 400
                                ToolTip.text: qsTr("Тащи край — растянуть или укоротить: сдвигается строка, которая это заканчивает")
                            }
                            Item { id: edgeProxy }
                        }
                    }
                }
            }
        }
        // ◆ a keyframe on a character's track, at the playhead: it glides to a new place from here
        Repeater {
            model: tl.current >= 0 ? (tl.tdata.tracks || []) : []
            Rectangle {
                required property var modelData
                required property int index
                visible: modelData.id.indexOf("char:") === 0
                x: tl.current * tl.beatW + tl.beatW / 2 + 6
                y: 22 + index * tl.rowH + (tl.rowH - height) / 2
                width: 22; height: 22; radius: 11
                z: 11
                color: kf.containsMouse ? Theme.gold : "#cc1e2b24"
                border.color: Theme.gold
                Text { anchors.centerIn: parent; text: "◆"; color: kf.containsMouse ? "#16240c" : Theme.gold; font.pixelSize: 12 }
                MouseArea {
                    id: kf
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tl.addKeyframe(parent.modelData.id.substring(5))
                }
                ToolTip.visible: kf.containsMouse
                ToolTip.text: qsTr("Ключевой кадр: отсюда персонаж плавно переедет на новое место — потом тащи его в превью")
            }
        }
        // the playhead
        Rectangle {
            visible: tl.current >= 0
            x: tl.current * tl.beatW + tl.beatW / 2
            y: 0; width: 2; height: lanes.contentHeight
            color: Theme.accent
            z: 10
        }
    }
    Text {
        anchors.centerIn: parent
        visible: tl.beats.length === 0
        text: qsTr("Поставь курсор в сцену — здесь появятся её дорожки")
        color: Theme.faint; font.family: Theme.ui; font.pixelSize: 14
    }
}
