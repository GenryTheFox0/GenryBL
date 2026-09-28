import QtQuick
import GenryBL

// A check box of the constructor's look: a rounded green square, the label next to it.
Item {
    id: c
    property bool checked: false
    property string text
    property color textColor: Theme.text
    signal toggled()
    implicitWidth: row.implicitWidth
    implicitHeight: 32
    Row {
        id: row
        spacing: 10
        anchors.verticalCenter: parent.verticalCenter
        Rectangle {
            width: 24; height: 24; radius: 6
            color: c.checked ? Theme.accent : "#33000000"
            border.color: c.checked ? "#5f8f2e" : (area.containsMouse ? Theme.accent : Theme.line)
            border.width: 2
            Behavior on color { ColorAnimation { duration: 120 } }
            Text { anchors.centerIn: parent; text: "✓"; visible: c.checked; color: "#16240c"; font.pixelSize: 16; font.bold: true }
        }
        Text { text: c.text; color: c.textColor; font.family: Theme.ui; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { c.checked = !c.checked; c.toggled() }
    }
}
