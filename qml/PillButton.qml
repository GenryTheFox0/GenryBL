import QtQuick
import QtQuick.Controls
import GenryBL

// Small rounded button. light = paper & ink (album screens), accent = leaf green, dark = editor.
AbstractButton {
    id: b
    property bool accent: false
    property bool dark: false
    implicitWidth: label.implicitWidth + 26
    implicitHeight: 30
    hoverEnabled: true
    onClicked: Sfx.click()
    scale: pressed ? 0.95 : 1
    Behavior on scale { NumberAnimation { duration: 90 } }

    background: Rectangle {
        radius: height / 2
        color: b.accent ? (b.hovered ? "#aee06e" : "#9bd35a") : b.dark ? (b.hovered ? Theme.panel3 : Theme.panel2) : (b.hovered ? "#fffaf0" : "#f4ecd2")
        border.color: b.accent ? "#5f8f2e" : b.dark ? Theme.line : "#b9a57a"
        border.width: 1
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    contentItem: Text {
        id: label
        text: b.text
        color: b.accent ? "#16240c" : b.dark ? Theme.text : Theme.ink
        font.family: Theme.ui
        font.pixelSize: 15
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        opacity: b.enabled ? 1 : 0.5
    }
}
