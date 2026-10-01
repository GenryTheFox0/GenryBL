import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// What the player sees at the cursor, rendered by THE renderer. Scrub the story with the
// timeline or press ▶ to watch it advance line by line like the game.
Rectangle {
    id: pp
    color: Theme.panel
    property url source
    property string extra
    property int line: 1
    property int lines: 1
    property bool playing: false
    property var boxes: []                 // Engine.spriteBoxes: the characters on the frame
    signal seek(int line)
    signal step(int dir)
    signal enlarge()
    signal liveCinema()                    // «живое кино» right here, over this pane
    // a character dragged to a place (fleft…fright) or its distance wheeled (-1 far, 0, 1 close; -2 = keep)
    signal moveSprite(int line, string pos, int dist)
    // V2.1.2 Filmora's way: with ◆ («автоключ») a drag anywhere / the wheel is a keyframe («ключ»)
    property bool autoKey: false
    property bool hud: true                // the music / sound plates over the frame
    signal spriteClicked(string tag)
    signal keySprite(string tag, real kx, real ky)
    signal zoomSprite(string tag, real factor)
    signal autoKeyToggled(bool on)
    signal hudToggled(bool on)

    // ES's own places (media.rpy xalign): where a dropped character snaps to
    readonly property var places: [["fleft", 0.16], ["left", 0.28], ["cleft", 0.355], ["center", 0.5], ["cright", 0.645], ["right", 0.72], ["fright", 0.84]]
    function nearestPlace(fx) {
        let best = places[0]
        for (const p of places) if (Math.abs(p[1] - fx) < Math.abs(best[1] - fx)) best = p
        return best
    }
    function placeName(p) {
        return { fleft: qsTr("край слева"), left: qsTr("слева"), cleft: qsTr("левее центра"), center: qsTr("по центру"),
                 cright: qsTr("правее центра"), right: qsTr("справа"), fright: qsTr("край справа") }[p] || p
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Item {
            id: frameBox
            Layout.fillWidth: true
            Layout.preferredHeight: width * 9 / 16
            Rectangle { anchors.fill: parent; color: "black"; radius: 8 }
            SmoothImage {
                anchors.fill: parent
                anchors.margins: 1
                source: pp.source
                fade: 110
            }
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                radius: 8
                border.color: pp.extra ? Theme.accent : Theme.line
                border.width: pp.extra ? 2 : 1
                Behavior on border.color { ColorAnimation { duration: 120 } }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onDoubleClicked: (m) => { if (m.button === Qt.LeftButton) pp.enlarge() }
                onClicked: (m) => { if (m.button === Qt.RightButton) frameMenu.popup() }
                ToolTip.visible: containsMouse && pp.extra === ""
                ToolTip.delay: 900
                ToolTip.text: qsTr("Двойной клик — на весь экран · правая кнопка — меню превью")
                hoverEnabled: true
            }
            Menu {
                id: frameMenu
                MenuItem { text: qsTr("Плашки музыки и звука"); checkable: true; checked: pp.hud; onTriggered: pp.hudToggled(checked) }
                MenuItem { text: qsTr("◆ Автоключ: мышь ставит ключевые кадры"); checkable: true; checked: pp.autoKey; onTriggered: pp.autoKeyToggled(checked) }
                MenuSeparator {}
                MenuItem { text: qsTr("На весь экран"); onTriggered: pp.enlarge() }
            }
            // ---- drag a character along the frame: it snaps to ES's places, the wheel brings it closer or farther;
            // the «показать» line that placed it is rewritten
            Item {
                id: dragLayer
                anchors.fill: parent
                anchors.margins: 1
                visible: pp.extra === "" && !pp.playing
                readonly property real k: width / 1920
                property int dragging: -1
                property real dragFx: 0.5
                // the snap marks, while a character is carried (with ◆ it goes anywhere)
                Repeater {
                    model: dragLayer.dragging >= 0 && !pp.autoKey ? pp.places : []
                    Rectangle {
                        required property var modelData
                        readonly property bool hit: pp.nearestPlace(dragLayer.dragFx)[0] === modelData[0]
                        x: modelData[1] * dragLayer.width - width / 2
                        y: dragLayer.height - 22
                        width: hit ? 4 : 2; height: 18; radius: 1
                        color: hit ? Theme.accent : "#ccffffff"
                    }
                }
                Repeater {
                    model: dragLayer.visible ? pp.boxes : []
                    Item {
                        id: sb
                        required property var modelData
                        required property int index
                        readonly property bool canDrag: modelData.line > 0 || pp.autoKey
                        x: modelData.x * dragLayer.k
                        y: Math.max(0, modelData.y * dragLayer.k)
                        width: modelData.w * dragLayer.k
                        height: Math.min(dragLayer.height, (modelData.y + modelData.h) * dragLayer.k) - y
                        Rectangle {
                            id: ghost
                            width: sb.width; height: sb.height
                            color: "transparent"
                            radius: 6
                            border.width: 2
                            border.color: Theme.accent
                            opacity: area.drag.active ? 0.95 : area.containsMouse && sb.canDrag ? 0.55 : 0
                            Behavior on opacity { NumberAnimation { duration: 120 } }
                            Rectangle {
                                visible: area.drag.active || area.containsMouse
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 8
                                height: 24; radius: 12
                                width: tip.implicitWidth + 18
                                color: Theme.accent
                                Text {
                                    id: tip
                                    anchors.centerIn: parent
                                    text: area.drag.active ? (pp.autoKey ? qsTr("◆ отпусти — ключевой кадр") : "→ " + pp.placeName(pp.nearestPlace(dragLayer.dragFx)[0]))
                                                           : pp.autoKey ? qsTr("◆ тащи куда угодно · колёсико — масштаб")
                                                                        : qsTr("тащи · колёсико — ближе/дальше")
                                    color: "white"; font.family: Theme.ui; font.pixelSize: 12; font.bold: true
                                }
                            }
                        }
                        MouseArea {
                            id: area
                            anchors.fill: parent
                            enabled: sb.canDrag
                            hoverEnabled: true
                            cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            drag.target: ghost
                            drag.axis: pp.autoKey ? Drag.XAndYAxis : Drag.XAxis
                            drag.threshold: 6
                            onPressed: dragLayer.dragFx = sb.modelData.cx
                            onPositionChanged: if (drag.active) {
                                dragLayer.dragging = sb.index
                                dragLayer.dragFx = (sb.x + ghost.x + ghost.width / 2) / dragLayer.width
                            }
                            onReleased: {
                                if (dragLayer.dragging === sb.index) {
                                    const dx = ghost.x / dragLayer.width, dy = ghost.y / dragLayer.height
                                    const p = pp.nearestPlace(dragLayer.dragFx)[0]
                                    ghost.x = 0; ghost.y = 0
                                    dragLayer.dragging = -1
                                    if (pp.autoKey) pp.keySprite(sb.modelData.tag, sb.modelData.kx + dx, sb.modelData.ky + dy)
                                    else if (p !== sb.modelData.pos) pp.moveSprite(sb.modelData.line, p, -2)
                                } else {
                                    ghost.x = 0; ghost.y = 0
                                    pp.spriteClicked(sb.modelData.tag)
                                }
                            }
                            onDoubleClicked: pp.enlarge()
                            onWheel: (wheel) => {
                                if (pp.autoKey) { pp.zoomSprite(sb.modelData.tag, wheel.angleDelta.y > 0 ? 1.1 : 1 / 1.1); return }
                                if (!(sb.modelData.line > 0)) return
                                const d = Math.max(-1, Math.min(1, sb.modelData.dist + (wheel.angleDelta.y > 0 ? 1 : -1)))
                                if (d !== sb.modelData.dist) pp.moveSprite(sb.modelData.line, "", d)
                            }
                        }
                    }
                }
            }
            // the two switches at hand: the plates (♪) and ◆ «автоключ»
            Row {
                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 8
                spacing: 6
                visible: pp.extra === ""
                Repeater {
                    model: [["♪", qsTr("Плашки музыки и звука"), "hud"], ["◆", qsTr("Автоключ: мышь ставит ключевые кадры, как в Филморе"), "key"]]
                    AbstractButton {
                        id: sw
                        required property var modelData
                        readonly property bool lit: modelData[2] === "hud" ? pp.hud : pp.autoKey
                        implicitWidth: 30; implicitHeight: 30
                        hoverEnabled: true
                        opacity: lit || hovered ? 1 : 0.6
                        onClicked: modelData[2] === "hud" ? pp.hudToggled(!pp.hud) : pp.autoKeyToggled(!pp.autoKey)
                        ToolTip.visible: hovered
                        ToolTip.text: modelData[1]
                        background: Rectangle { radius: 15; color: sw.lit ? Theme.accent : "#b3000000"; border.color: sw.hovered ? Theme.accent : "#66ffffff" }
                        contentItem: Text { text: sw.modelData[0]; color: sw.lit ? "#16240c" : Theme.gold; font.pixelSize: 14; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }
            Rectangle {
                visible: pp.extra !== ""
                x: 10; y: 10
                height: 26
                width: tagText.implicitWidth + 20
                radius: 13
                color: Theme.accent
                Text {
                    id: tagText
                    anchors.centerIn: parent
                    text: qsTr("примерка: ") + pp.extra.split("\n")[0]
                    color: "white"; font.family: Theme.ui; font.pixelSize: 13; font.bold: true
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            PillButton { dark: true; text: "«"; onClicked: pp.seek(1) }
            PillButton { dark: true; text: "‹"; onClicked: pp.step(-1) }
            PillButton {
                accent: true
                text: pp.playing ? qsTr("Пауза") : qsTr("Смотреть")
                implicitWidth: 92
                onClicked: pp.playing = !pp.playing
            }
            PillButton { dark: true; text: "›"; onClicked: pp.step(1) }
            PillButton {
                dark: true
                text: qsTr("🎬 Живое кино")
                onClicked: pp.liveCinema()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Мод играет прямо тут, а ты пишешь дальше: каждая правка сразу видна в кино — без запуска БЛ")
            }
            Slider {
                id: scrub
                Layout.fillWidth: true
                from: 1; to: Math.max(1, pp.lines); stepSize: 1
                value: pp.line
                onMoved: pp.seek(Math.round(value))
            }
            Text {
                text: pp.line + " / " + pp.lines
                color: Theme.dim; font.family: Theme.mono; font.pixelSize: 13
            }
        }
    }

    Timer {
        running: pp.playing
        interval: 1400
        repeat: true
        onTriggered: {
            if (pp.line >= pp.lines) { pp.playing = false; return }
            pp.step(1)
        }
    }
}
