import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Dialogs
import GenryBL

// "Инструменты" on ES's preferences paper (images/gui/settings/preferences_bg.jpg).
Item {
    id: set
    width: 1920
    height: 1080
    signal back()
    property string menuTime: Engine.setting("menuTime", "auto")
    readonly property string scaleAtStart: Engine.setting("uiScale", "")
    property string uiScale: scaleAtStart
    property string windowSize: Engine.setting("windowSize", "")
    // the sizes this screen can hold (the rest would stick out of it)
    readonly property var sizes: [["", qsTr("авто")], ["1280x720", "1280×720"], ["1366x768", "1366×768"], ["1600x900", "1600×900"],
                                  ["1920x1080", "1920×1080"], ["full", qsTr("весь экран")]]
        .filter(s => { if (!s[0] || s[0] === "full") return true; const wh = s[0].split("x"); return +wh[0] <= Screen.desktopAvailableWidth && +wh[1] <= Screen.desktopAvailableHeight - 32 })

    Image { anchors.fill: parent; source: "image://gb/file/images/gui/settings/preferences_bg.jpg"; asynchronous: true }

    component Option: Item {
        property string text
        property bool checked
        signal toggled(bool on)
        width: 760; height: 46
        Rectangle {
            id: box
            y: 7; width: 32; height: 32; radius: 6
            color: parent.checked ? "#7fb845" : "#fffaf0"
            border.color: "#8a7a55"; border.width: 2
            Text { anchors.centerIn: parent; text: "✔"; color: "white"; font.pixelSize: 20; visible: parent.parent.checked }
        }
        Text { x: 50; y: 5; text: parent.text; color: Theme.ink; font.family: Theme.ui; font.pixelSize: 28 }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); parent.toggled(!parent.checked) } }
    }

    Column {
        x: 590; y: 262
        spacing: 9
        Text { text: qsTr("Инструменты"); color: "#5a3e1e"; font.family: Theme.riffic; font.pixelSize: 56; font.bold: true }
        Option { text: qsTr("Музыка в меню"); checked: Music.enabled; onToggled: (on) => Music.setEnabled(on) }
        Option { text: qsTr("Звуки интерфейса"); checked: Sfx.enabled; onToggled: (on) => Sfx.setEnabled(on) }
        Option { text: qsTr("Звуки природы в меню"); checked: Ambience.enabled; onToggled: (on) => Ambience.setEnabled(on) }
        Row {
            spacing: 18
            Text { text: qsTr("Громкость"); color: Theme.ink; font.family: Theme.ui; font.pixelSize: 28; anchors.verticalCenter: parent.verticalCenter }
            Slider { width: 420; from: 0; to: 1; value: Music.volume; onMoved: Music.setVolume(value) }
        }
        // the language: a flag + its name, a click opens all 20
        Row {
            spacing: 14
            Text { text: qsTr("Язык:"); color: Theme.ink; font.family: Theme.ui; font.pixelSize: 28; anchors.verticalCenter: parent.verticalCenter }
            Rectangle {
                width: langRow.implicitWidth + 28; height: 50; radius: 25
                color: langArea2.containsMouse ? "#f6ecd2" : "#fffaf0"; border.color: "#c9b58a"; border.width: 2
                Row {
                    id: langRow
                    x: 14; anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Image { source: "file:///" + Engine.appRoot + "/data/flags/" + Engine.language + ".png"; width: 42; height: 28; smooth: true; mipmap: true; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: langPicker2.nameOf(Engine.language) + (Engine.languageSetting === "auto" || !Engine.languageSetting ? "  · " + qsTr("как в Windows") : "")
                           color: Theme.ink; font.family: Theme.ui; font.pixelSize: 22; anchors.verticalCenter: parent.verticalCenter }
                }
                MouseArea { id: langArea2; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); langPicker2.open() } }
            }
        }
        Row {
            spacing: 10
            Text { text: qsTr("Меню:"); color: Theme.ink; font.family: Theme.ui; font.pixelSize: 28; anchors.verticalCenter: parent.verticalCenter }
            Repeater {
                model: [["auto", qsTr("по часам")], ["morning", qsTr("утро")], ["day", qsTr("день")], ["evening", qsTr("вечер")], ["night", qsTr("ночь")]]
                PillButton {
                    text: modelData[1]
                    accent: set.menuTime === modelData[0] || (modelData[0] === "evening" && set.menuTime === "sunset")
                    onClicked: { set.menuTime = modelData[0]; Engine.setSetting("menuTime", modelData[0]) }
                }
            }
        }
        // «Окно»: its size, right away
        Flow {
            width: 760
            spacing: 10
            Text { text: qsTr("Окно:"); color: Theme.ink; font.family: Theme.ui; font.pixelSize: 28; height: 44; verticalAlignment: Text.AlignVCenter }
            Repeater {
                model: set.sizes
                PillButton {
                    text: modelData[1]
                    accent: set.windowSize === modelData[0]
                    onClicked: {
                        set.windowSize = modelData[0]
                        Engine.setSetting("windowSize", modelData[0])
                        ApplicationWindow.window.applyWindowSize(modelData[0])
                    }
                }
            }
        }
        // «Масштаб интерфейса»: for a laptop whose Windows zoom makes the window small (1920x1080 at 150 %)
        Row {
            spacing: 10
            Text { text: qsTr("Масштаб:"); color: Theme.ink; font.family: Theme.ui; font.pixelSize: 28; anchors.verticalCenter: parent.verticalCenter }
            Repeater {
                model: [["", qsTr("как в Windows")], ["0.75", "75%"], ["0.85", "85%"], ["1.15", "115%"], ["1.25", "125%"]]
                PillButton {
                    text: modelData[1]
                    accent: set.uiScale === modelData[0]
                    onClicked: { set.uiScale = modelData[0]; Engine.setSetting("uiScale", modelData[0]) }
                }
            }
        }
        Row {
            spacing: 12
            visible: set.uiScale !== set.scaleAtStart
            Text {
                text: qsTr("Новый масштаб включится после перезапуска")
                color: "#8a1f1a"; font.family: Theme.ui; font.pixelSize: 22; anchors.verticalCenter: parent.verticalCenter
            }
            PillButton { accent: true; text: qsTr("Перезапустить"); onClicked: Engine.restartApp() }
        }
        Text {
            width: 760
            wrapMode: Text.Wrap
            text: qsTr("Бесконечное лето: ") + Engine.esRoot
            color: Theme.ink; font.family: Theme.ui; font.pixelSize: 22
        }
        Row {
            spacing: 10
            PillButton { text: qsTr("Другая папка БЛ…"); onClicked: esDialog.open() }
            PillButton { text: qsTr("Моды в игре"); onClicked: Engine.openFolder(Engine.esRoot + "/game/mods") }
            PillButton { text: qsTr("Проекты"); onClicked: Engine.openFolder(Engine.appRoot + "/projects") }
        }
        Text {
            width: 760
            wrapMode: Text.Wrap
            text: qsTr("Ассеты игры читаются прямо из archive.rpa — ничего не копируется на диск. «Играть» запускает БЛ окном сразу в нужной сцене, со своими сохранениями: твои обычные сейвы не трогаются.")
            color: "#6b5838"; font.family: Theme.ui; font.pixelSize: 21
        }
    }

    FolderDialog {
        id: esDialog
        title: qsTr("Папка «Everlasting Summer» (где лежит Everlasting Summer.exe)")
        onAccepted: {
            Engine.setSetting("esRoot", decodeURIComponent(String(selectedFolder)).replace("file:///", ""))
            Engine.toast(qsTr("Папка БЛ сохранится после перезапуска программы"), 0)
        }
    }

    Text {
        x: 590; y: 930
        text: qsTr("‹ Назад")
        color: bArea.containsMouse ? "#2f6b1c" : "#5a3e1e"
        font.family: Theme.riffic; font.pixelSize: 38; font.bold: true
        MouseArea { id: bArea; anchors.fill: parent; anchors.margins: -10; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); set.back() } }
    }
    LanguagePicker { id: langPicker2 }
}
