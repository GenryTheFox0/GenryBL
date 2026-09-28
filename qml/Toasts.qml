import QtQuick
import GenryBL

// Stacked notifications, bottom-right, slide in and fade away.
Item {
    id: root
    function show(text, level) { toastModel.append({ msg: text, level: level || 0 }) }

    ListModel { id: toastModel }

    ListView {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 18
        width: 420
        height: parent.height - 40
        spacing: 10
        verticalLayoutDirection: ListView.BottomToTop
        interactive: false
        model: toastModel
        add: Transition {
            ParallelAnimation {
                NumberAnimation { property: "x"; from: 440; to: 0; duration: 320; easing.type: Easing.OutCubic }
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 260 }
            }
        }
        remove: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 300 } }
        displaced: Transition { NumberAnimation { properties: "y"; duration: 240; easing.type: Easing.OutCubic } }
        delegate: Rectangle {
            width: 420
            height: label.implicitHeight + 28
            radius: 12
            color: Theme.panel2
            border.color: Theme.levelColor(level === 0 ? -1 : level)
            border.width: 1
            Rectangle { width: 5; height: parent.height - 16; x: 8; y: 8; radius: 2; color: level === 0 ? Theme.accent : Theme.levelColor(level) }
            Text {
                id: label
                x: 24; width: parent.width - 36
                anchors.verticalCenter: parent.verticalCenter
                text: msg
                wrapMode: Text.Wrap
                color: Theme.text
                font.family: Theme.ui
                font.pixelSize: 15
            }
            Timer { running: true; interval: level >= 2 ? 7000 : 3800; onTriggered: toastModel.remove(index) }
            MouseArea { anchors.fill: parent; onClicked: toastModel.remove(index) }
        }
    }
}
