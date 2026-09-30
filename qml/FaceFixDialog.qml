import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Поправить лицо»: a wardrobe sprite whose face does not sit on the head (a layer of a mod drawn
// for another pose) - move the face by hand: arrows (Shift = 5 px), drag on the big head, or type.
// Saved for this sprite, for every emotion of the outfit, or for this emotion in every outfit;
// the preview, «Кино» and the mod itself get it. Or delete the sprite right here.
Popup {
    id: ff
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 80 : 1100, 1180)
    height: Math.min(parent ? parent.height - 60 : 800, 820)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    property string tag: ""
    property string name: ""            // "smile casual"
    property string dist: ""
    property int dx: 0
    property int dy: 0
    property string scope: "outfit"
    readonly property string image: tag + " " + name + (dist ? " " + dist : "")
    readonly property string emotion: name.split(" ")[0]
    readonly property string outfit: name.split(" ").slice(1).join(" ")
    property string fullUrl: ""
    property string headUrl: ""

    function openFor(t, n, d) {
        tag = t; name = n; dist = d || ""
        const s = Engine.faceShiftOf(t, n, dist)
        dx = s.dx; dy = s.dy
        scope = s.scope || (outfit ? "outfit" : "look")
        refresh()
        open()
        keys.forceActiveFocus()
    }
    function refresh() { refreshTimer.restart() }
    function nudge(x, y) { dx += x; dy += y; refresh() }
    onDxChanged: refresh()
    onDyChanged: refresh()
    Timer {
        id: refreshTimer
        interval: 30
        onTriggered: {
            ff.fullUrl = Engine.faceFixUrl(ff.image, ff.dx, ff.dy, false)
            ff.headUrl = Engine.faceFixUrl(ff.image, ff.dx, ff.dy, true)
        }
    }

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.accent }

    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
            const step = (e.modifiers & Qt.ShiftModifier) ? 5 : 1
            if (e.key === Qt.Key_Left) { ff.nudge(-step, 0); e.accepted = true }
            else if (e.key === Qt.Key_Right) { ff.nudge(step, 0); e.accepted = true }
            else if (e.key === Qt.Key_Up) { ff.nudge(0, -step); e.accepted = true }
            else if (e.key === Qt.Key_Down) { ff.nudge(0, step); e.accepted = true }
            else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) { save.clicked(); e.accepted = true }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                InkText { text: qsTr("Поправить лицо"); size: 26; color: Theme.gold }
                Text { text: "  " + ff.image; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 14 }
                Item { Layout.fillWidth: true }
                PillButton { dark: true; text: "✕"; onClicked: ff.close() }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 14
                // the whole sprite
                Rectangle {
                    Layout.preferredWidth: parent.height * 900 / 1080
                    Layout.fillHeight: true
                    radius: 10
                    color: "#1b2330"
                    Image {
                        anchors.fill: parent
                        anchors.margins: 6
                        source: ff.fullUrl
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        asynchronous: false
                    }
                }
                // the head, big: drag the face
                Rectangle {
                    id: headBox
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 10
                    color: "#1b2330"
                    border.color: drag.pressed ? Theme.accent : Theme.line
                    Image {
                        id: head
                        anchors.fill: parent
                        anchors.margins: 6
                        source: ff.headUrl
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        asynchronous: false
                    }
                    Text {
                        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 10
                        text: qsTr("тяни мышью · стрелки 1 px · Shift+стрелки 5 px")
                        color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13
                    }
                    MouseArea {
                        id: drag
                        anchors.fill: parent
                        cursorShape: Qt.SizeAllCursor
                        property point from
                        property int sx: 0
                        property int sy: 0
                        onPressed: (m) => { from = Qt.point(m.x, m.y); sx = ff.dx; sy = ff.dy; keys.forceActiveFocus() }
                        onPositionChanged: (m) => {
                            if (!pressed || head.paintedWidth <= 0) return
                            // screen pixels -> sprite pixels (the head picture is shown scaled)
                            const k = head.sourceSize.width > 0 ? head.sourceSize.width / head.paintedWidth : 1
                            ff.dx = sx + Math.round((m.x - from.x) * k)
                            ff.dy = sy + Math.round((m.y - from.y) * k)
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Grid {
                    columns: 3
                    spacing: 4
                    Item { width: 34; height: 30 }
                    PillButton { dark: true; text: "↑"; implicitWidth: 34; onClicked: ff.nudge(0, -1) }
                    Item { width: 34; height: 30 }
                    PillButton { dark: true; text: "←"; implicitWidth: 34; onClicked: ff.nudge(-1, 0) }
                    PillButton { dark: true; text: "↓"; implicitWidth: 34; onClicked: ff.nudge(0, 1) }
                    PillButton { dark: true; text: "→"; implicitWidth: 34; onClicked: ff.nudge(1, 0) }
                }
                Text { text: qsTr("сдвиг  ") + ff.dx + ", " + ff.dy; color: Theme.text; font.family: Theme.mono; font.pixelSize: 16 }
                PillButton { dark: true; text: "0, 0"; onClicked: { ff.dx = 0; ff.dy = 0 } }
                Item { width: 18 }
                ColumnLayout {
                    spacing: 2
                    Repeater {
                        model: [["look", qsTr("только этот спрайт")], ["outfit", qsTr("все эмоции наряда «") + ff.outfit + "»"], ["face", qsTr("эмоция «") + ff.emotion + qsTr("» во всех нарядах")]]
                        RadioButton {
                            required property var modelData
                            visible: modelData[0] !== "outfit" || ff.outfit !== ""
                            checked: ff.scope === modelData[0]
                            text: modelData[1]
                            onClicked: { ff.scope = modelData[0]; keys.forceActiveFocus() }
                            contentItem: Text { leftPadding: 36; text: parent.text; color: Theme.text; font.family: Theme.ui; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                PillButton {
                    dark: true
                    text: qsTr("Удалить спрайт")
                    onClicked: { Engine.hideSprite(ff.tag, ff.name); ff.close() }
                }
                PillButton {
                    id: save
                    accent: true
                    text: qsTr("Сохранить  ⏎")
                    onClicked: { Engine.setFaceShift(ff.tag, ff.name, ff.dist, ff.scope, ff.dx, ff.dy); ff.close() }
                }
            }
        }
    }
}
