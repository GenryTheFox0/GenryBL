import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Играть» -> the game fell (src/core/Crash): what went wrong in plain words, the line of the story it fell on (ours),
// or whose mod it was (a Workshop mod, another folder in game/mods) - and the game's own report under a fold.
Popup {
    id: cd
    property var crash: ({})
    property bool showRaw: false
    signal gotoLine(string project, int line)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 80, 900)
    height: Math.min(parent.height - 60, col.implicitHeight + 40)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function show(c) {
        crash = c
        showRaw = false
        open()
    }
    readonly property bool ours: !!crash.ours
    readonly property bool gameItself: !crash.ours && !crash.mod
    readonly property string human: {
        const a = crash.arg || ""
        if (crash.what === "image") return qsTr("В игре нет картинки «") + a + qsTr("» — проверь имя спрайта или фона.")
        if (crash.what === "label") return qsTr("Переход в сцену «") + a + qsTr("», которой нет.")
        if (crash.what === "name") return qsTr("Переменная «") + a + qsTr("» нигде не задана — её нужно сначала установить.")
        if (crash.what === "file") return qsTr("Нет файла «") + a + qsTr("» — положи его в проект или проверь имя.")
        if (crash.what === "syntax") return qsTr("Игра не смогла прочитать скрипт: ") + (crash.error || "")
        return crash.error || ""
    }

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.bad; border.width: 2 }

    ColumnLayout {
        id: col
        x: 22; y: 20
        width: parent.width - 44
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            InkText {
                text: cd.ours ? qsTr("Игра упала на твоём моде") : cd.gameItself ? qsTr("Игра упала") : qsTr("Игра упала на чужом моде")
                size: 30; color: Theme.bad
            }
            Item { Layout.fillWidth: true }
            PillButton { dark: true; text: "✕"; onClicked: cd.close() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: cd.human
            color: Theme.text; font.family: Theme.ui; font.pixelSize: 17
        }
        // our mod: the story line it fell on
        Rectangle {
            visible: cd.ours
            Layout.fillWidth: true
            Layout.preferredHeight: storyCol.implicitHeight + 20
            radius: 10
            color: Theme.bg2
            border.color: Theme.line
            Column {
                id: storyCol
                x: 12; y: 10
                width: parent.width - 24
                spacing: 4
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: cd.crash.line > 0 ? qsTr("Строка ") + cd.crash.line + (cd.crash.scene ? qsTr(" · сцена «") + cd.crash.scene + "»" : "")
                                            : qsTr("Ошибка в служебной части мода, не в строке истории — пришли текст ошибки Генри.")
                    color: Theme.gold; font.family: Theme.ui; font.pixelSize: 14; font.bold: true
                }
                Text {
                    visible: !!cd.crash.text
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: cd.crash.text || ""
                    color: Theme.text; font.family: Theme.mono; font.pixelSize: 15
                }
            }
        }
        // somebody else's mod
        Text {
            visible: !cd.ours && !cd.gameItself
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Это не твой мод: «") + (cd.crash.mod || "") + "»" + (cd.crash.workshopId ? qsTr(" из Мастерской Steam") : qsTr(" из папки game/mods")) +
                  qsTr(". Если он мешает — отпишись от него в Мастерской или убери его папку, пока тестируешь свой.")
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15
        }
        Text {
            visible: cd.gameItself
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Ошибка в самой игре, а не в модах. Проверь файлы игры в Steam: Свойства → Установленные файлы → Проверить.")
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15
        }
        // the game's own report
        Rectangle {
            visible: cd.showRaw
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(260, rawText.implicitHeight + 16)
            radius: 8
            color: Theme.bg
            border.color: Theme.line
            ScrollView {
                anchors.fill: parent
                anchors.margins: 6
                TextArea {
                    id: rawText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    text: cd.crash.raw || ""
                    font.family: Theme.mono; font.pixelSize: 12
                    color: "#d8d8e4"
                    background: null
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 4
            spacing: 10
            PillButton {
                visible: cd.ours && cd.crash.line > 0
                accent: true
                text: qsTr("К строке ") + (cd.crash.line || "")
                onClicked: { cd.close(); cd.gotoLine(cd.crash.project, cd.crash.line) }
            }
            PillButton {
                visible: !!cd.crash.workshopId
                dark: true
                text: qsTr("Страница в Мастерской")
                onClicked: Qt.openUrlExternally("https://steamcommunity.com/sharedfiles/filedetails/?id=" + cd.crash.workshopId)
            }
            PillButton { dark: true; text: cd.showRaw ? qsTr("Скрыть текст ошибки") : qsTr("Текст ошибки"); onClicked: cd.showRaw = !cd.showRaw }
            PillButton { dark: true; text: qsTr("Скопировать"); onClicked: { Engine.copyText(cd.crash.raw || ""); Engine.toast(qsTr("Текст ошибки скопирован"), 0) } }
            Item { Layout.fillWidth: true }
            PillButton { visible: Engine.gameRunning; dark: true; text: qsTr("■ Закрыть игру"); onClicked: Engine.stopGame() }
        }
    }
}
