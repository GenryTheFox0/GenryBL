import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import GenryBL

// "Инструменты" on ES's preferences paper (images/gui/settings/preferences_bg.jpg).
Item {
    id: set
    width: 1920
    height: 1080
    signal back()
    property string menuTime: Engine.setting("menuTime", "auto")

    Image { anchors.fill: parent; source: "image://gb/file/images/gui/settings/preferences_bg.jpg"; asynchronous: true }

    component Option: Item {
        property string text
        property bool checked
        signal toggled(bool on)
        width: 760; height: 56
        Rectangle {
            id: box
            y: 12; width: 32; height: 32; radius: 6
            color: parent.checked ? "#7fb845" : "#fffaf0"
            border.color: "#8a7a55"; border.width: 2
            Text { anchors.centerIn: parent; text: "✔"; color: "white"; font.pixelSize: 20; visible: parent.parent.checked }
        }
        Text { x: 50; y: 10; text: parent.text; color: Theme.ink; font.family: Theme.ui; font.pixelSize: 28 }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); parent.toggled(!parent.checked) } }
    }

    Column {
        x: 590; y: 290
        spacing: 16
        Text { text: "Инструменты"; color: "#5a3e1e"; font.family: Theme.riffic; font.pixelSize: 56; font.bold: true }
        Option { text: "Музыка в меню"; checked: Music.enabled; onToggled: (on) => Music.setEnabled(on) }
        Option { text: "Звуки интерфейса"; checked: Sfx.enabled; onToggled: (on) => Sfx.setEnabled(on) }
        Row {
            spacing: 18
            Text { text: "Громкость"; color: Theme.ink; font.family: Theme.ui; font.pixelSize: 28; anchors.verticalCenter: parent.verticalCenter }
            Slider { width: 420; from: 0; to: 1; value: Music.volume; onMoved: Music.setVolume(value) }
        }
        Row {
            spacing: 10
            Text { text: "Меню:"; color: Theme.ink; font.family: Theme.ui; font.pixelSize: 28; anchors.verticalCenter: parent.verticalCenter }
            Repeater {
                model: [["auto", "по часам"], ["day", "день"], ["sunset", "вечер"], ["night", "ночь"]]
                PillButton {
                    text: modelData[1]
                    accent: set.menuTime === modelData[0]
                    onClicked: { set.menuTime = modelData[0]; Engine.setSetting("menuTime", modelData[0]) }
                }
            }
        }
        Text {
            width: 760
            wrapMode: Text.Wrap
            text: "Бесконечное лето: " + Engine.esRoot
            color: Theme.ink; font.family: Theme.ui; font.pixelSize: 22
        }
        Row {
            spacing: 10
            PillButton { text: "Другая папка БЛ…"; onClicked: esDialog.open() }
            PillButton { text: "Моды в игре"; onClicked: Engine.openFolder(Engine.esRoot + "/game/mods") }
            PillButton { text: "Проекты"; onClicked: Engine.openFolder(Engine.appRoot + "/projects") }
        }
        Text {
            width: 760
            wrapMode: Text.Wrap
            text: "Ассеты игры читаются прямо из archive.rpa — ничего не копируется на диск. «Играть» запускает БЛ окном сразу в нужной сцене, со своими сохранениями: твои обычные сейвы не трогаются."
            color: "#6b5838"; font.family: Theme.ui; font.pixelSize: 21
        }
    }

    FolderDialog {
        id: esDialog
        title: "Папка «Everlasting Summer» (где лежит Everlasting Summer.exe)"
        onAccepted: {
            Engine.setSetting("esRoot", decodeURIComponent(String(selectedFolder)).replace("file:///", ""))
            Engine.toast("Папка БЛ сохранится после перезапуска программы", 0)
        }
    }

    Text {
        x: 590; y: 930
        text: "‹ Назад"
        color: bArea.containsMouse ? "#2f6b1c" : "#5a3e1e"
        font.family: Theme.riffic; font.pixelSize: 38; font.bold: true
        MouseArea { id: bArea; anchors.fill: parent; anchors.margins: -10; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); set.back() } }
    }
}
