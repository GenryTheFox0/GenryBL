import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// Pick one value from a list: scenes, tracks (with ▶ to listen), videos, project files,
// speakers, variables. Items: {value, label, sub, color, audio, thumb}.
Popup {
    id: pk
    property string title: ""
    property var items: []
    property var onPick: null
    signal picked(string value)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 160, 720)
    height: Math.min(parent.height - 120, 720)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: { search.text = ""; search.forceActiveFocus() }
    onClosed: Music.stopPreview()

    function show(title, items, cb) {
        pk.title = title
        pk.items = items
        pk.onPick = cb
        open()
    }
    function choose(v) {
        if (onPick) onPick(v)
        picked(v)
        close()
    }
    readonly property var shown: items.filter(it => !search.text || (String(it.label) + " " + String(it.value) + " " + (it.sub || "")).toLowerCase().indexOf(search.text.toLowerCase()) >= 0)

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.accent }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            InkText { text: pk.title; size: 24; color: Theme.gold }
            TextField {
                id: search
                Layout.fillWidth: true
                implicitHeight: 32
                placeholderText: "Поиск…"
                placeholderTextColor: Theme.faint
                color: Theme.text
                font.family: Theme.ui; font.pixelSize: 14
                selectByMouse: true
                background: Rectangle { radius: 16; color: Theme.bg; border.color: search.activeFocus ? Theme.accent : Theme.line }
                Keys.onReturnPressed: if (pk.shown.length) pk.choose(pk.shown[0].value)
            }
            PillButton { dark: true; text: "✕"; onClicked: pk.close() }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 3
            model: pk.shown
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: list.width - 12
                x: 6
                height: modelData.thumb ? 64 : 42
                radius: 8
                readonly property bool playing: !!modelData.audio && Music.previewing === modelData.audio
                color: playing ? "#339bd35a" : area.containsMouse ? Theme.panel3 : "transparent"
                border.color: area.containsMouse ? Theme.accent : "transparent"
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { Sfx.click(); pk.choose(modelData.value) }
                }
                Row {
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Rectangle {
                        visible: !!modelData.audio
                        width: 30; height: 30; radius: 15
                        anchors.verticalCenter: parent.verticalCenter
                        color: parent.parent.playing ? Theme.accent : Theme.panel2
                        border.color: Theme.accent
                        Text { anchors.centerIn: parent; text: parent.parent.parent.playing ? "■" : "▶"; color: parent.parent.parent.playing ? "#16240c" : Theme.text; font.pixelSize: 13 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Music.preview(modelData.audio) }
                    }
                    Image {
                        visible: !!modelData.thumb
                        width: 96; height: 54
                        anchors.verticalCenter: parent.verticalCenter
                        source: modelData.thumb || ""
                        sourceSize: Qt.size(192, 108)
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            text: modelData.label
                            color: modelData.color || Theme.text
                            font.family: Theme.ui; font.pixelSize: 15; font.bold: true
                        }
                        Text {
                            visible: !!modelData.sub
                            text: modelData.sub || ""
                            color: Theme.dim
                            font.family: Theme.mono; font.pixelSize: 12
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: pk.shown.length === 0
                text: pk.items.length ? "Ничего не нашлось" : "Пока пусто"
                color: Theme.faint; font.family: Theme.ui; font.pixelSize: 16
            }
        }
    }
}
