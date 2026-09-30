import QtQuick
import GenryBL

// The language of GenryBL: 20 flags + «as in Windows». Chosen - applied at once (Engine.setLanguage ->
// every qsTr in the window is re-read). Lives inside a 1920x1080 screen, so it scales with it.
Item {
    id: lp
    anchors.fill: parent
    z: 50
    property bool shown: false
    function open() { shown = true }
    function close() { shown = false }
    function flag(code) { return "file:///" + Engine.appRoot + "/data/flags/" + code + ".png" }
    function nameOf(code) {
        for (const l of Engine.languages) if (l.code === code) return l.name
        return code
    }
    opacity: shown ? 1 : 0
    visible: opacity > 0.01
    Behavior on opacity { NumberAnimation { duration: 200 } }

    Rectangle {
        anchors.fill: parent
        color: "#b0000000"
        MouseArea { anchors.fill: parent; onClicked: lp.close() }
    }
    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: 1040
        height: col.implicitHeight + 56
        radius: 16
        color: "#fbf5e1"
        border.color: "#c9b58a"
        border.width: 2
        scale: lp.shown ? 1 : 0.94
        Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
        MouseArea { anchors.fill: parent }
        Column {
            id: col
            x: 28; y: 28
            width: parent.width - 56
            spacing: 14
            Row {
                spacing: 14
                Text { text: qsTr("Язык"); font.family: Theme.riffic; font.pixelSize: 44; font.bold: true; color: "#5a3e1e" }
                Text {
                    anchors.baseline: parent.children[0].baseline
                    text: "Language · 言語 · 语言 · Idioma · Sprache"
                    font.family: Theme.ui; font.pixelSize: 20; color: "#8a7456"
                }
            }
            // as in Windows
            Rectangle {
                width: parent.width; height: 58; radius: 10
                readonly property bool chosen: Engine.languageSetting === "auto" || Engine.languageSetting === ""
                color: chosen ? "#f4e2a8" : autoArea.containsMouse ? "#f6ecd2" : "#fffaf0"
                border.color: chosen ? "#c99a2e" : "#dccda6"
                Row {
                    x: 14; anchors.verticalCenter: parent.verticalCenter
                    spacing: 14
                    Image { source: lp.flag(Engine.systemLanguage()); width: 48; height: 32; smooth: true; mipmap: true; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⟳  " + qsTr("Как в Windows") + " — " + lp.nameOf(Engine.systemLanguage())
                        font.family: Theme.ui; font.pixelSize: 22; font.bold: true; color: Theme.ink
                    }
                }
                MouseArea { id: autoArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { Sfx.click(); Engine.setLanguage("auto"); lp.close() } }
            }
            Grid {
                columns: 4
                spacing: 10
                Repeater {
                    model: Engine.languages
                    Rectangle {
                        width: (col.width - 30) / 4; height: 64; radius: 10
                        readonly property bool chosen: Engine.languageSetting === modelData.code
                        color: chosen ? "#f4e2a8" : lArea.containsMouse ? "#f6ecd2" : "#fffaf0"
                        border.color: chosen ? "#c99a2e" : "#dccda6"
                        scale: lArea.containsMouse ? 1.03 : 1
                        Behavior on scale { NumberAnimation { duration: 120 } }
                        Row {
                            x: 12; anchors.verticalCenter: parent.verticalCenter
                            spacing: 12
                            Image { source: lp.flag(modelData.code); width: 48; height: 32; smooth: true; mipmap: true; anchors.verticalCenter: parent.verticalCenter }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                Text { text: modelData.name; font.family: Theme.ui; font.pixelSize: 20; font.bold: true; color: Theme.ink }
                                Text { text: modelData.english; font.family: Theme.ui; font.pixelSize: 13; color: "#8a7456" }
                            }
                        }
                        MouseArea { id: lArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: { Sfx.click(); Engine.setLanguage(modelData.code); lp.close() } }
                    }
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Команды мода («фон», «показать», «выбор»…) остаются русскими — это язык модов «Бесконечного лета». Всё вокруг них — на твоём языке.")
                font.family: Theme.ui; font.pixelSize: 17; color: "#7a6a4f"
            }
        }
    }
}
