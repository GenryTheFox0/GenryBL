import QtQuick
import QtQuick.Controls
import GenryBL

TabButton {
    id: t
    property color accentColor: Theme.accent
    hoverEnabled: true
    implicitHeight: 40
    contentItem: Text {
        text: t.text
        color: t.checked ? Theme.text : t.hovered ? Theme.gold : Theme.dim
        font.family: Theme.riffic
        font.pixelSize: 14
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    background: Rectangle {
        color: t.checked ? Theme.panel2 : "transparent"
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            height: 3
            radius: 2
            width: t.checked ? parent.width - 16 : 0
            color: t.accentColor
            Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        }
    }
}
