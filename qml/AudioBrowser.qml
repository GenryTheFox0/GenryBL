import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import GenryBL

// The game's music, sound effects and ambiences: listen here, insert with one click.
Item {
    id: ab
    signal insert(string cmd)
    property int tab: 0

    Row {
        id: head
        x: 10; y: 10
        spacing: 6
        Repeater {
            model: ["Музыка", "Звуки", "Атмосфера"]
            PillButton { text: modelData; accent: ab.tab === index; dark: ab.tab !== index; onClicked: ab.tab = index }
        }
        PillButton { dark: true; text: "＋"; visible: ab.tab === 0; onClicked: audioDialog.open() }
        PillButton { dark: true; text: "■"; visible: Music.previewing !== ""; onClicked: Music.stopPreview() }
    }
    TextField {
        id: search
        anchors.top: head.bottom; anchors.topMargin: 8
        x: 10; width: parent.width - 20
        placeholderText: "Поиск…"
        placeholderTextColor: Theme.faint
        color: Theme.text
        font.family: Theme.ui; font.pixelSize: 14
        selectByMouse: true
        background: Rectangle { radius: 8; color: Theme.bg; border.color: search.activeFocus ? Theme.accent : Theme.line }
    }

    ListView {
        id: list
        anchors.top: search.bottom; anchors.topMargin: 8
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        clip: true
        spacing: 3
        model: [Engine.music, Engine.sounds, Engine.ambience][ab.tab].filter(m => !search.text || m.word.toLowerCase().indexOf(search.text.toLowerCase()) >= 0)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            width: list.width - 12
            x: 6
            height: 44
            radius: 8
            readonly property bool playing: Music.previewing === modelData.path
            color: playing ? "#339bd35a" : area.containsMouse ? Theme.panel3 : "transparent"
            MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onDoubleClicked: ab.insert(modelData.cmd) }
            Rectangle {
                x: 8; anchors.verticalCenter: parent.verticalCenter
                width: 30; height: 30; radius: 15
                color: parent.playing ? Theme.accent : Theme.panel2
                border.color: Theme.accent
                Text { anchors.centerIn: parent; text: parent.parent.playing ? "■" : "▶"; color: parent.parent.playing ? "#16240c" : Theme.text; font.pixelSize: 13 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Music.preview(modelData.path) }
            }
            Text {
                x: 48; anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 48 - ins.width - 16
                text: modelData.word + (modelData.custom ? "  ★" : "")
                color: Theme.text; font.family: Theme.mono; font.pixelSize: 14
                elide: Text.ElideRight
            }
            PillButton {
                id: ins
                anchors.right: parent.right; anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                dark: true
                text: "Вставить"
                onClicked: ab.insert(modelData.cmd)
            }
        }
    }

    FileDialog {
        id: audioDialog
        title: "Своя музыка (ogg/mp3/wav/flac…)"
        nameFilters: ["Аудио (*.ogg *.mp3 *.wav *.flac *.m4a *.opus)"]
        onAccepted: {
            const p = Engine.importAudio(selectedFile)
            if (p) ab.insert("музыкафайл " + p)
        }
    }
}
