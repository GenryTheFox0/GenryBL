import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Лаборатория механик» (data/lab): ready pieces of scenes - a scream, a storm, a timed choice, a code lock, a phone
// call… Every one is installed into the game and checked by it before a GenryBL release (gb_cli gate), so a piece
// that goes into the story works. Pick one, scrub its frame, watch it in the film, put it into the story.
Popup {
    id: lab
    signal insertPiece(string body, bool atEnd)
    signal watch(string story)
    property var pieces: []
    property int current: 0
    property int line: 1                        // the piece's line on the frame (1-based inside the piece)
    readonly property var piece: pieces.length ? pieces[Math.min(current, pieces.length - 1)] : ({})
    readonly property var bodyLines: (piece.body || "").split("\n")
    readonly property string story: piece.body ? Engine.labStory(piece.body) : ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 50, 1320)
    height: Math.min(parent.height - 40, 840)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openLab() {
        pieces = Engine.labPieces()
        pick(0)
        open()
    }
    function pick(i) {
        current = i
        line = Math.max(1, Math.min(Number(pieces[i] ? pieces[i].preview : 1) || 1, (pieces[i] ? pieces[i].body : "").split("\n").length))
    }

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            InkText { text: qsTr("Лаборатория механик"); size: 32; color: Theme.gold }
            Item { Layout.fillWidth: true }
            PillButton { dark: true; text: "✕"; onClicked: lab.close() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Готовые куски сцен — каждый перед выпуском GenryBL проверяет сама игра. Выбери, прокрути кадр, посмотри в кино и вставь в историю.")
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // ---- the pieces
            ListView {
                id: list
                Layout.preferredWidth: 380
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: lab.pieces
                ScrollBar.vertical: ScrollBar {}
                delegate: Column {
                    id: pieceRow
                    required property var modelData
                    required property int index
                    width: list.width - 10
                    spacing: 4
                    // the group's name above its first piece
                    Text {
                        visible: pieceRow.index === 0 || lab.pieces[pieceRow.index - 1].group !== pieceRow.modelData.group
                        topPadding: pieceRow.index === 0 ? 0 : 10
                        text: pieceRow.modelData.group
                        color: Theme.gold; font.family: Theme.ui; font.pixelSize: 15; font.bold: true
                    }
                    Rectangle {
                        width: parent.width
                        height: 64
                        radius: 10
                        color: lab.current === pieceRow.index ? Theme.panel3 : pa.containsMouse ? Theme.panel2 : Theme.bg2
                        border.color: lab.current === pieceRow.index ? Theme.gold : Theme.line
                        Text { x: 12; anchors.verticalCenter: parent.verticalCenter; text: pieceRow.modelData.icon || "•"; font.pixelSize: 26 }
                        Column {
                            x: 52; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 62
                            Text { width: parent.width; elide: Text.ElideRight; text: pieceRow.modelData.title; color: Theme.text; font.family: Theme.ui; font.pixelSize: 16; font.bold: true }
                            Text { width: parent.width; elide: Text.ElideRight; text: pieceRow.modelData.about; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                        }
                        MouseArea { id: pa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: lab.pick(pieceRow.index); onDoubleClicked: lab.watch(lab.story) }
                    }
                }
            }

            // ---- the chosen one
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: lab.piece.icon || ""; font.pixelSize: 30 }
                    Column {
                        Layout.fillWidth: true
                        Text { text: lab.piece.title || ""; color: Theme.text; font.family: Theme.ui; font.pixelSize: 22; font.bold: true }
                        Text { width: parent.width; wrapMode: Text.Wrap; text: lab.piece.about || ""; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15 }
                    }
                }
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 16
                    Layout.maximumHeight: 430
                    Rectangle { anchors.fill: parent; color: "black"; radius: 8 }
                    SmoothImage {
                        anchors.fill: parent
                        anchors.margins: 1
                        fillMode: Image.PreserveAspectFit
                        // the piece plays as a little mod: three lines of its head come first
                        source: lab.story ? Engine.previewUrl(lab.story, lab.line + 3) : ""
                        fade: 90
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    PillButton { dark: true; text: "‹"; onClicked: lab.line = Math.max(1, lab.line - 1) }
                    Slider {
                        Layout.fillWidth: true
                        from: 1; to: Math.max(1, lab.bodyLines.length); stepSize: 1
                        value: lab.line
                        onMoved: lab.line = Math.round(value)
                    }
                    PillButton { dark: true; text: "›"; onClicked: lab.line = Math.min(lab.bodyLines.length, lab.line + 1) }
                }
                // the piece's lines; the one on the frame lit
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: Theme.bg
                    border.color: Theme.line
                    ListView {
                        id: lines
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        model: lab.bodyLines
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: lines.width
                            height: lt.implicitHeight + 4
                            color: lab.line === index + 1 ? "#2a9bd35a" : "transparent"
                            Text {
                                id: lt
                                x: 6; width: parent.width - 12
                                wrapMode: Text.WrapAnywhere
                                text: modelData
                                textFormat: Text.PlainText
                                color: lab.line === index + 1 ? Theme.text : Theme.dim
                                font.family: Theme.mono; font.pixelSize: 13
                            }
                            MouseArea { anchors.fill: parent; onClicked: lab.line = index + 1 }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: !!lab.piece.atEnd
                        text: qsTr("У этого куска свои сцены — он встанет в конец истории")
                        color: Theme.faint; font.family: Theme.ui; font.pixelSize: 13
                    }
                    Item { Layout.fillWidth: !lab.piece.atEnd }
                    PillButton { dark: true; text: qsTr("▶ Смотреть в кино"); onClicked: lab.watch(lab.story) }
                    PillButton { accent: true; text: qsTr("Вставить в историю"); onClicked: { lab.insertPiece(lab.piece.body, !!lab.piece.atEnd); lab.close() } }
                }
            }
        }
    }
}
