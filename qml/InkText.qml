import QtQuick
import GenryBL

// Heading text with a soft dark rim, readable over any part of the ES menu art.
Item {
    id: root
    property string text
    property color color: "white"
    property color rim: "#b0101a0d"
    property int size: 30
    property bool bold: true
    property string family: Theme.riffic
    implicitWidth: t.implicitWidth + 4
    implicitHeight: t.implicitHeight + 4
    Repeater {
        model: 8
        Text {
            readonly property real a: index * Math.PI / 4
            x: 2 + Math.round(Math.cos(a) * 2)
            y: 2 + Math.round(Math.sin(a) * 2)
            text: root.text
            font: t.font
            color: root.rim
        }
    }
    Text {
        id: t
        x: 2; y: 2
        text: root.text
        color: root.color
        font.family: root.family
        font.pixelSize: root.size
        font.bold: root.bold
    }
}
