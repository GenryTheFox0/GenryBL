import QtQuick
import QtQuick.Controls
import GenryBL

ComboBox {
    id: c
    implicitHeight: 28
    hoverEnabled: true
    font.family: Theme.ui
    font.pixelSize: 14
    background: Rectangle {
        radius: height / 2
        color: c.hovered ? Theme.panel3 : Theme.panel2
        border.color: c.popup.visible ? Theme.accent : Theme.line
    }
    contentItem: Text {
        leftPadding: 14
        rightPadding: 24
        text: c.displayText
        color: Theme.text
        font: c.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: c.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: Theme.dim
        font.pixelSize: 12
    }
    delegate: ItemDelegate {
        width: c.width
        height: 30
        highlighted: c.highlightedIndex === index
        contentItem: Text {
            text: modelData
            color: highlighted ? "white" : Theme.text
            font: c.font
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: highlighted ? Theme.accent : "transparent"; radius: 6 }
    }
    popup: Popup {
        y: c.height + 4
        width: c.width
        implicitHeight: contentItem.implicitHeight + 8
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: c.popup.visible ? c.delegateModel : null
            currentIndex: c.highlightedIndex
        }
        background: Rectangle { color: Theme.panel2; radius: 10; border.color: Theme.accent }
    }
}
