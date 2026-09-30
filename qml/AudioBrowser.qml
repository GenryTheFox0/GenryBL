import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import GenryBL

// The game's music, sound effects and ambiences: listen here, insert with one click.
Item {
    id: ab
    signal insert(string cmd)
    property int tab: 0
    // a community file goes into the mod first (with its author in CREDITS.txt), then its command
    function use(m) {
        if (!m.community) { insert(m.cmd); return }
        const p = Engine.importCommunity(m.community)
        if (!p) return
        insert((m.kind === "music" ? "музыкафайл " : m.kind === "ambience" ? "атмосфера " : "звукфайл ") + p)
    }

    Row {
        id: head
        x: 10; y: 10
        spacing: 6
        Repeater {
            model: [qsTr("Музыка"), qsTr("Звуки"), qsTr("Атмосфера"), qsTr("Сообщество")]
            PillButton { text: modelData; accent: ab.tab === index; dark: ab.tab !== index; onClicked: ab.tab = index }
        }
        PillButton { dark: true; text: qsTr("＋ Свой"); visible: ab.tab < 3; onClicked: audioDialog.open() }
        PillButton { dark: true; text: "■"; visible: Music.previewing !== ""; onClicked: Music.stopPreview() }
    }
    TextField {
        id: search
        anchors.top: head.bottom; anchors.topMargin: 8
        x: 10; width: parent.width - 20
        placeholderText: qsTr("Поиск…")
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
        // «пляж» finds ambience_beach_* by its Russian name (es-doc), «beach» by the word
        readonly property var source: ab.tab === 3 ? Engine.communitySounds().map(c => ({ word: c.file.split("/").pop(), title: c.title, sub: c.captions,
                                                                                            path: c.url, community: c.file, kind: c.kind }))
                                                   : [Engine.music, Engine.sounds, Engine.ambience][ab.tab]
        model: source.filter(m => !search.text || (m.word + " " + (m.title || "")).toLowerCase().indexOf(search.text.toLowerCase()) >= 0)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            width: list.width - 12
            x: 6
            height: 44
            radius: 8
            readonly property bool playing: Music.previewing === modelData.path
            color: playing ? "#339bd35a" : area.containsMouse ? Theme.panel3 : "transparent"
            MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onDoubleClicked: ab.use(modelData) }
            Rectangle {
                x: 8; anchors.verticalCenter: parent.verticalCenter
                width: 30; height: 30; radius: 15
                color: parent.playing ? Theme.accent : Theme.panel2
                border.color: Theme.accent
                Text { anchors.centerIn: parent; text: parent.parent.playing ? "■" : "▶"; color: parent.parent.playing ? "#16240c" : Theme.text; font.pixelSize: 13 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Music.preview(modelData.path) }
            }
            Column {
                x: 48; anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 48 - ins.width - 16
                Text {
                    width: parent.width
                    text: (modelData.title || modelData.word) + (modelData.custom ? "  ★" : "")
                    color: Theme.text; font.family: modelData.title ? Theme.ui : Theme.mono; font.pixelSize: 14
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: !!modelData.title || !!modelData.sub
                    text: modelData.sub || modelData.word
                    color: Theme.faint; font.family: Theme.mono; font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            PillButton {
                id: ins
                anchors.right: parent.right; anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                dark: true
                text: modelData.community ? qsTr("В мой мод") : qsTr("Вставить")
                onClicked: ab.use(modelData)
            }
        }
    }

    FileDialog {
        id: audioDialog
        title: ab.tab === 2 ? qsTr("Своя атмосфера (ogg/mp3/wav/flac… — переделаю в ogg)") : ab.tab === 1 ? qsTr("Свой звук (ogg/mp3/wav/flac… — переделаю в ogg)")
                                                                                                   : qsTr("Своя музыка (ogg/mp3/wav/flac… — переделаю в ogg)")
        nameFilters: [qsTr("Аудио (*.ogg *.mp3 *.wav *.flac *.m4a *.opus)")]
        onAccepted: {
            const p = Engine.importAudio(selectedFile, ["music", "sfx", "ambience"][ab.tab])
            if (p) ab.insert((ab.tab === 2 ? "атмосфера " : ab.tab === 1 ? "звукфайл " : "музыкафайл ") + p)
        }
    }
}
