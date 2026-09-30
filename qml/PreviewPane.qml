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
    signal seek(int line)
    signal step(int dir)
    signal enlarge()

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
                onDoubleClicked: pp.enlarge()
                ToolTip.visible: containsMouse && pp.extra === ""
                ToolTip.delay: 900
                ToolTip.text: qsTr("Двойной клик — на весь экран")
                hoverEnabled: true
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
