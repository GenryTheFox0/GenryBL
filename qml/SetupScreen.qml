import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import GenryBL

// The first start when Steam did not tell where the game is: «where is Everlasting Summer?»
Item {
    id: setup
    property string found: Engine.detectEs()
    property string error: ""

    function use(folder) {
        const e = Engine.useEsRoot(folder)
        error = e
        if (e) shake.restart()
    }

    SummerBackdrop { anchors.fill: parent }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 900)
        height: col.implicitHeight + 64
        radius: 20
        color: "#e0111a15"
        border.color: "#66ffdd7d"
        opacity: 0
        Component.onCompleted: appear.start()
        ParallelAnimation {
            id: appear
            NumberAnimation { target: card; property: "opacity"; from: 0; to: 1; duration: 700; easing.type: Easing.OutCubic }
            NumberAnimation { target: card; property: "anchors.verticalCenterOffset"; from: 40; to: 0; duration: 900; easing.type: Easing.OutBack }
        }
        SequentialAnimation {
            id: shake
            loops: 2
            NumberAnimation { target: card; property: "anchors.horizontalCenterOffset"; to: 14; duration: 50 }
            NumberAnimation { target: card; property: "anchors.horizontalCenterOffset"; to: -14; duration: 80 }
            NumberAnimation { target: card; property: "anchors.horizontalCenterOffset"; to: 0; duration: 50 }
        }
        ColumnLayout {
            id: col
            x: 40; y: 32
            width: parent.width - 80
            spacing: 16
            Text { text: "GenryBL"; color: Theme.gold; font.family: Theme.riffic; font.pixelSize: 64; font.bold: true }
            Text { text: qsTr("конструктор модов «Бесконечного лета»"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 20; Layout.topMargin: -12 }
            Text {
                Layout.topMargin: 10
                text: qsTr("Где у тебя стоит «Бесконечное лето»?")
                color: Theme.text; font.family: Theme.ui; font.pixelSize: 28; font.bold: true
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 17
                text: setup.found
                      ? qsTr("Нашёл игру через Steam. Если это она — жми «Да, это она».")
                      : qsTr("Через Steam не нашёл. Покажи папку игры: Steam → Библиотека → Бесконечное лето → ⚙ → Управление → ") +
                        qsTr("Просмотреть локальные файлы — вот эту папку (там лежит Everlasting Summer.exe).")
            }
            Rectangle {
                visible: setup.found !== ""
                Layout.fillWidth: true
                height: 48; radius: 10
                color: "#33000000"; border.color: Theme.line
                Text { anchors.fill: parent; anchors.margins: 14; verticalAlignment: Text.AlignVCenter; elide: Text.ElideMiddle; text: setup.found; color: Theme.text; font.family: Theme.mono; font.pixelSize: 16 }
            }
            Text {
                visible: setup.error !== ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "⚠ " + setup.error
                color: Theme.bad; font.family: Theme.ui; font.pixelSize: 17
            }
            RowLayout {
                spacing: 12
                PillButton { visible: setup.found !== ""; accent: true; text: qsTr("Да, это она"); implicitHeight: 44; onClicked: setup.use(setup.found) }
                PillButton { accent: setup.found === ""; dark: setup.found !== ""; text: qsTr("Выбрать папку…"); implicitHeight: 44; onClicked: folderDialog.open() }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("<a href='steam://install/331470'>Нет игры? Она бесплатная в Steam</a>")
                    textFormat: Text.RichText
                    color: Theme.dim; linkColor: Theme.gold
                    font.family: Theme.ui; font.pixelSize: 16
                    onLinkActivated: (l) => Qt.openUrlExternally(l)
                    MouseArea { anchors.fill: parent; acceptedButtons: Qt.NoButton; cursorShape: Qt.PointingHandCursor }
                }
            }
        }
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Папка «Бесконечного лета» (там Everlasting Summer.exe)")
        onAccepted: setup.use(String(selectedFolder))
    }
}
