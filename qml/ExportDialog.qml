import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Экспорт мода»: the mod as other people get it. Every way first builds it and lets the game itself check it
// (Ren'Py lint) - a broken mod never leaves; then either the archive for players or the folder for the game's
// own Steam Workshop uploader, both in Documents\GenryBL, shown selected in Explorer.
Popup {
    id: ex
    property string projectId: ""
    property string storyText: ""
    property string state_: "choose"            // choose | working | done | failed
    property string message: ""
    property string path: ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 980)
    height: Math.min(parent.height - 50, 620)
    modal: true
    padding: 0
    closePolicy: state_ === "working" ? Popup.NoAutoClose : Popup.CloseOnEscape

    function openFor(id, text) {
        projectId = id
        storyText = text
        state_ = "choose"
        message = ""
        path = ""
        open()
    }
    function run(kind) {
        Sfx.click()
        state_ = "working"
        Engine.exportMod(projectId, storyText, kind)
    }
    Connections {
        target: Engine
        function onExportFinished(ok, message, path) {
            if (!ex.opened) return
            ex.message = message
            ex.path = path
            ex.state_ = ok ? "done" : "failed"
        }
    }

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }

    component Way: Rectangle {
        id: way
        property string title
        property string body
        property string kind
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: 12
        color: wayArea.containsMouse ? Theme.panel3 : Theme.bg2
        border.color: wayArea.containsMouse ? Theme.gold : Theme.line
        border.width: wayArea.containsMouse ? 2 : 1
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12
            Text { text: way.title; color: Theme.gold; font.family: Theme.riffic; font.pixelSize: 26; font.bold: true }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: way.body
                color: Theme.text; font.family: Theme.ui; font.pixelSize: 17; lineHeight: 1.15
            }
            Item { Layout.fillHeight: true }
            PillButton { accent: true; text: qsTr("Собрать"); onClicked: ex.run(way.kind) }
        }
        MouseArea { id: wayArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ex.run(way.kind); z: -1 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            InkText { text: qsTr("Экспорт мода"); size: 32; color: Theme.gold }
            Item { Layout.fillWidth: true }
            PillButton { dark: true; text: "✕"; enabled: ex.state_ !== "working"; onClicked: ex.close() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: ex.state_ === "choose"
            text: qsTr("Сначала мод соберётся и его проверит сама игра (1–2 минуты) — сломанный мод никому не уйдёт. ") +
                  qsTr("Заодно проверю, что каждая картинка, звук и шрифт лежат внутри мода: у другого человека нет твоих файлов.")
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 16
        }

        // ---- choose
        RowLayout {
            visible: ex.state_ === "choose"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16
            Way {
                title: qsTr("Архив для игроков")
                kind: "zip"
                body: qsTr("ZIP: папка мода и «КАК_УСТАНОВИТЬ.txt». Человек распаковал её в game\\mods — и играет.\n\n") +
                      qsTr("Кидай куда хочешь: ВК, Телеграм, Дискорд, диск. Русские имена файлов не побьются.")
            }
            Way {
                title: qsTr("Папка для Мастерской")
                kind: "workshop"
                body: qsTr("Готовая папка для загрузчика самой игры (ES_Content_Uploader): mods\\<мод> с .rpyc, ") +
                      qsTr("обложка preview.jpg из первого кадра мода и «КАК_ВЫЛОЖИТЬ.txt» по шагам.")
            }
            Way {
                title: qsTr("Андроид")
                kind: "android"
                body: qsTr("ZIP для мобильного «Бесконечного лета»: картинки и разметка уменьшены под экран телефона (1280×720), ") +
                      qsTr("имена файлов латиницей, внутри «КАК_УСТАНОВИТЬ_ANDROID.txt» — куда положить папку mods.")
            }
        }

        // ---- working / done / failed
        ColumnLayout {
            visible: ex.state_ !== "choose"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14
            Item { Layout.fillHeight: true }
            BusyIndicator { visible: ex.state_ === "working"; running: visible; Layout.alignment: Qt.AlignHCenter }
            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: ex.state_ !== "working"
                text: ex.state_ === "done" ? "✓" : "✖"
                color: ex.state_ === "done" ? Theme.good : Theme.bad
                font.pixelSize: 64
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: ex.state_ === "working" ? (Engine.busyText || qsTr("Собираю…")) : ex.message
                color: ex.state_ === "failed" ? Theme.bad : Theme.text
                font.family: Theme.ui; font.pixelSize: 18
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                visible: ex.state_ === "done"
                wrapMode: Text.WrapAnywhere
                text: ex.path
                color: Theme.dim; font.family: Theme.mono; font.pixelSize: 13
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 10
                visible: ex.state_ !== "working"
                PillButton { visible: ex.state_ === "done"; accent: true; text: qsTr("Показать в папке"); onClicked: Engine.revealFile(ex.path) }
                PillButton { dark: true; text: ex.state_ === "failed" ? qsTr("Назад") : qsTr("Ещё экспорт"); onClicked: ex.state_ = "choose" }
                PillButton { dark: true; text: qsTr("Закрыть"); onClicked: ex.close() }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: qsTr("Куда кладу: ") + Engine.exportDir()
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14
            }
            PillButton { dark: true; text: qsTr("Открыть папку"); onClicked: Engine.openFolder(Engine.exportDir()) }
        }
    }
}
