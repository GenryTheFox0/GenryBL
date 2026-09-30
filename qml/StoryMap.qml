import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import GenryBL

// «Карта сюжета» (src/core/Graph): every scene of the story as a card and every way between them as an arrow -
// «переход», options, buttons of the mod's menu, calls, places on the camp map, code locks. Columns are steps
// from the start; the scenes nobody can reach stand apart, a way into a scene that is not there ends in a red
// card with «Создать». A click on a card = that scene in the editor. Wheel = zoom, drag = move around.
Popup {
    id: sm
    parent: Overlay.overlay
    x: 0; y: 0
    width: parent ? parent.width : 1600
    height: parent ? parent.height : 900
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    signal gotoLine(int line)
    signal createScene(string name)

    property var graph: ({ nodes: [], edges: [], cols: 0, rows: 0 })
    property int hover: -1
    property real zoom: 1
    readonly property int cardW: 230
    readonly property int cardH: 126
    readonly property int colStep: 330
    readonly property int rowStep: 158
    readonly property int margin: 60
    readonly property var nodes: graph.nodes || []
    readonly property var edges: graph.edges || []
    readonly property int lost: nodes.filter(n => n.kind === "scene" && !n.reachable).length
    readonly property int missing: nodes.filter(n => n.kind === "missing").length
    readonly property int stops: nodes.filter(n => n.kind === "scene" && n.ending === "stops" && n.reachable).length
    readonly property real canvasW: margin * 2 + Math.max(1, graph.cols) * colStep
    readonly property real canvasH: margin * 2 + Math.max(1, graph.rows) * rowStep

    function openFor(text) {
        graph = Engine.storyGraph(text)
        hover = -1
        open()
        Qt.callLater(fit)
    }
    function fit() {
        zoom = Math.max(0.25, Math.min(1.2, Math.min(flick.width / canvasW, flick.height / canvasH)))
        flick.contentX = 0
        flick.contentY = 0
    }
    function nodeX(n) { return margin + n.col * colStep }
    function nodeY(n) { return margin + n.row * rowStep }
    function edgeColor(kind) {
        return kind === "choice" ? "#9bd35a" : kind === "call" ? "#c792ea" : kind === "if" || kind === "best" ? "#ffb86c"
             : kind === "map" ? "#8be9fd" : kind === "button" || kind === "chapter" ? "#ffdd7d" : kind === "timeout" ? "#ff6b6b"
             : kind === "code" || kind === "phone" ? "#f78fb3" : kind === "next" ? "#a9b9a4" : "#5fb3ff"
    }
    function endingText(n) {
        return n.kind === "menu" ? qsTr("меню мода") : n.kind === "missing" ? qsTr("такой сцены нет")
             : n.ending === "end" ? qsTr("■ конец игры") : n.ending === "jump" ? qsTr("→ переход") : n.ending === "choice" ? qsTr("⑂ выбор")
             : n.ending === "return" ? qsTr("↩ возврат") : n.ending === "next" ? qsTr("↓ дальше") : qsTr("⚠ мод тут закончится")
    }

    background: Rectangle { color: Theme.bg }

    // ---------------------------------------------------------------- top
    Rectangle {
        id: bar
        width: parent.width
        height: 64
        color: Theme.bg2
        z: 2
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 14
            InkText { text: qsTr("Карта сюжета"); size: 28; color: Theme.gold }
            Text {
                text: qsTr("сцен: ") + sm.nodes.filter(n => n.kind === "scene").length + qsTr("  ·  путей: ") + sm.edges.length
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15
            }
            Rectangle {
                visible: sm.lost > 0
                height: 26; radius: 13; width: lostT.implicitWidth + 20
                color: "#33ffc857"; border.color: Theme.warn
                Text { id: lostT; anchors.centerIn: parent; text: qsTr("⚠ недостижимых: ") + sm.lost; color: Theme.text; font.family: Theme.ui; font.pixelSize: 13; font.bold: true }
            }
            Rectangle {
                visible: sm.missing > 0
                height: 26; radius: 13; width: missT.implicitWidth + 20
                color: "#33ff6b6b"; border.color: Theme.bad
                Text { id: missT; anchors.centerIn: parent; text: qsTr("✖ ведут в никуда: ") + sm.missing; color: Theme.text; font.family: Theme.ui; font.pixelSize: 13; font.bold: true }
            }
            Rectangle {
                visible: sm.stops > 0
                height: 26; radius: 13; width: stopT.implicitWidth + 20
                color: "#33ffc857"; border.color: Theme.warn
                Text { id: stopT; anchors.centerIn: parent; text: qsTr("⚠ обрываются: ") + sm.stops; color: Theme.text; font.family: Theme.ui; font.pixelSize: 13; font.bold: true }
            }
            Item { Layout.fillWidth: true }
            Row {
                spacing: 12
                Repeater {
                    model: [["jump", qsTr("переход")], ["choice", qsTr("выбор")], ["button", qsTr("кнопка")], ["call", qsTr("вызов")],
                            ["if", qsTr("если")], ["map", qsTr("карта")], ["code", qsTr("замок, звонок")], ["timeout", qsTr("время вышло")]]
                    Row {
                        required property var modelData
                        spacing: 5
                        Rectangle { width: 16; height: 3; radius: 1; color: sm.edgeColor(modelData[0]); anchors.verticalCenter: parent.verticalCenter }
                        Text { text: modelData[1]; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 12 }
                    }
                }
            }
            PillButton { dark: true; text: qsTr("Вписать"); onClicked: sm.fit() }
            PillButton { dark: true; text: "✕"; onClicked: sm.close() }
        }
    }

    // ---------------------------------------------------------------- the map
    Flickable {
        id: flick
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: hint.top
        clip: true
        contentWidth: sm.canvasW * sm.zoom
        contentHeight: sm.canvasH * sm.zoom
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

        WheelHandler {
            acceptedModifiers: Qt.NoModifier
            onWheel: (e) => {
                const old = sm.zoom
                const z = Math.max(0.2, Math.min(2.0, old * (e.angleDelta.y > 0 ? 1.12 : 1 / 1.12)))
                // zoom around the mouse
                const px = (flick.contentX + e.x) / old, py = (flick.contentY + e.y) / old
                sm.zoom = z
                flick.contentX = Math.max(0, px * z - e.x)
                flick.contentY = Math.max(0, py * z - e.y)
            }
        }

        Item {
            id: board
            width: sm.canvasW
            height: sm.canvasH
            transformOrigin: Item.TopLeft
            scale: sm.zoom

            // the lost ones: a column of their own, marked
            Rectangle {
                readonly property var firstLost: sm.nodes.find(n => n.kind === "scene" && !n.reachable)
                visible: !!firstLost
                x: firstLost ? sm.nodeX(firstLost) - 18 : 0
                y: 14
                width: sm.cardW + 36
                height: sm.canvasH - 28
                radius: 14
                color: "#10ffc857"
                border.color: "#55ffc857"
                Text { x: 14; y: 10; text: qsTr("Сюда никто не ведёт"); color: Theme.warn; font.family: Theme.ui; font.pixelSize: 14; font.bold: true }
            }

            // the ways: vector curves (a canvas as big as a long story would eat hundreds of MB)
            Repeater {
                model: sm.edges
                Item {
                    id: way
                    required property var modelData
                    readonly property var a: sm.nodes[modelData.from]
                    readonly property var b: sm.nodes[modelData.to]
                    readonly property bool lit: sm.hover < 0 || modelData.from === sm.hover || modelData.to === sm.hover
                    readonly property bool forward: !!a && !!b && b.col > a.col
                    readonly property real x1: a ? sm.nodeX(a) + sm.cardW : 0
                    readonly property real y1: a ? sm.nodeY(a) + sm.cardH / 2 : 0
                    readonly property real x2: b ? sm.nodeX(b) : 0
                    readonly property real y2: b ? sm.nodeY(b) + sm.cardH / 2 : 0
                    readonly property real low: a && b ? Math.max(sm.nodeY(a), sm.nodeY(b)) + sm.cardH + 26 : 0
                    readonly property real dx: Math.max(60, (x2 - x1) / 2)
                    readonly property color tint: sm.edgeColor(modelData.kind)
                    z: lit && sm.hover >= 0 ? 1 : 0
                    opacity: lit ? 0.95 : 0.16
                    Shape {
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: way.tint
                            strokeWidth: way.lit && sm.hover >= 0 ? 3 : 2
                            fillColor: "transparent"
                            startX: way.x1; startY: way.y1
                            PathCubic {
                                x: way.forward ? way.x2 : (way.x1 + way.x2) / 2
                                y: way.forward ? way.y2 : way.low
                                control1X: way.forward ? way.x1 + way.dx : way.x1 + 90
                                control1Y: way.y1
                                control2X: way.forward ? way.x2 - way.dx : way.x1 + 90
                                control2Y: way.forward ? way.y2 : way.low
                            }
                            PathCubic {
                                x: way.x2; y: way.y2
                                control1X: way.forward ? way.x2 : way.x2 - 90
                                control1Y: way.forward ? way.y2 : way.low
                                control2X: way.forward ? way.x2 : way.x2 - 90
                                control2Y: way.y2
                            }
                        }
                        ShapePath {
                            strokeColor: "transparent"
                            fillColor: way.tint
                            startX: way.x2; startY: way.y2
                            PathLine { x: way.x2 - 11; y: way.y2 - 6 }
                            PathLine { x: way.x2 - 11; y: way.y2 + 6 }
                            PathLine { x: way.x2; y: way.y2 }
                        }
                    }
                    Rectangle {
                        visible: !!way.modelData.text && way.lit
                        x: (way.x1 + way.x2) / 2 - width / 2
                        y: (way.forward ? (way.y1 + way.y2) / 2 : way.low) - height / 2
                        width: Math.min(170, wayT.implicitWidth + 12)
                        height: 20
                        radius: 6
                        color: "#e6121a16"
                        Text {
                            id: wayT
                            anchors.centerIn: parent
                            width: Math.min(158, implicitWidth)
                            elide: Text.ElideRight
                            text: way.modelData.text
                            color: way.tint
                            font.family: Theme.ui; font.pixelSize: 12
                        }
                    }
                }
            }

            Repeater {
                model: sm.nodes
                Rectangle {
                    id: card
                    required property var modelData
                    required property int index
                    readonly property bool bad: modelData.kind === "missing"
                    readonly property bool lostOne: modelData.kind === "scene" && !modelData.reachable
                    x: sm.nodeX(modelData)
                    y: sm.nodeY(modelData)
                    width: sm.cardW
                    height: sm.cardH
                    radius: 12
                    color: bad ? "#2aff6b6b" : modelData.kind === "menu" ? "#2affdd7d" : Theme.panel
                    opacity: lostOne ? 0.72 : 1
                    border.width: sm.hover === index ? 3 : 2
                    border.color: bad ? Theme.bad : modelData.start ? Theme.accent : lostOne ? Theme.warn
                                : modelData.ending === "stops" ? Theme.warn : modelData.ending === "end" ? Theme.gold : Theme.line
                    clip: true
                    Image {
                        visible: !!card.modelData.bg
                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                        anchors.margins: 2
                        width: 92
                        source: card.modelData.bg ? "image://gb/" + card.modelData.bg.substring(0, 2) + "/" + encodeURIComponent(card.modelData.bg.substring(3)) : ""
                        sourceSize: Qt.size(184, 244)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        opacity: 0.85
                    }
                    Column {
                        x: card.modelData.bg ? 102 : 12
                        y: 10
                        width: parent.width - x - 10
                        spacing: 4
                        Text {
                            width: parent.width
                            text: card.modelData.kind === "menu" ? qsTr("Меню мода") : card.modelData.name
                            elide: Text.ElideRight
                            color: card.bad ? Theme.bad : Theme.text
                            font.family: Theme.ui; font.pixelSize: 17; font.bold: true
                        }
                        Text {
                            visible: card.modelData.kind === "scene"
                            text: qsTr("стр. ") + card.modelData.line + "  ·  " + card.modelData.words + qsTr(" сл.")
                            color: Theme.dim; font.family: Theme.mono; font.pixelSize: 12
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: sm.endingText(card.modelData)
                            color: card.modelData.ending === "stops" || card.bad ? Theme.warn : card.modelData.ending === "end" ? Theme.gold : Theme.dim
                            font.family: Theme.ui; font.pixelSize: 13
                        }
                        Text {
                            visible: card.lostOne
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: qsTr("⚠ сюда никто не ведёт")
                            color: Theme.warn; font.family: Theme.ui; font.pixelSize: 12
                        }
                    }
                    Rectangle {
                        visible: card.modelData.start
                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 6
                        width: startT.implicitWidth + 12; height: 20; radius: 10
                        color: Theme.accent
                        Text { id: startT; anchors.centerIn: parent; text: qsTr("▶ старт"); color: "#16240c"; font.family: Theme.ui; font.pixelSize: 11; font.bold: true }
                    }
                    Rectangle {
                        visible: card.modelData.chapter
                        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 6
                        width: chT.implicitWidth + 12; height: 20; radius: 10
                        color: "#44ffdd7d"
                        Text { id: chT; anchors.centerIn: parent; text: qsTr("глава"); color: Theme.gold; font.family: Theme.ui; font.pixelSize: 11; font.bold: true }
                    }
                    PillButton {
                        visible: card.bad
                        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 8
                        accent: true
                        text: qsTr("Создать")
                        onClicked: { sm.createScene(card.modelData.name); sm.close() }
                    }
                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        hoverEnabled: true
                        cursorShape: card.modelData.line > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onEntered: sm.hover = card.index
                        onExited: if (sm.hover === card.index) sm.hover = -1
                        onClicked: if (card.modelData.line > 0) { sm.gotoLine(card.modelData.line); sm.close() }
                    }
                }
            }
        }
    }

    Text {
        id: hint
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        height: 34
        verticalAlignment: Text.AlignVCenter
        text: qsTr("клик по сцене — к её строке   ·   колёсико — ближе/дальше   ·   тащи — двигать   ·   Esc — назад")
        color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14
    }
}
