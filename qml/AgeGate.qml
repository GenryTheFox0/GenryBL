import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «18+»: said once (the installer asks it too) - what is inside and who answers for the mods.
Popup {
    id: gate
    parent: Overlay.overlay
    x: 0; y: 0
    width: parent ? parent.width : 1600
    height: parent ? parent.height : 900
    modal: true
    padding: 0
    closePolicy: Popup.NoAutoClose
    signal accepted()
    // the same words in the installer
    readonly property string words:
        "GenryBL — конструктор модов для «Бесконечного лета», и он для взрослых.\n\n" +
        "Внутри есть официальный 18+ контент самой игры — сцены, вырезанные из Steam-версии (хентай-патч из Мастерской). " +
        "Если тебе нет восемнадцати — закрывай и приходи позже, лагерь никуда не денется.\n\n" +
        "Теперь по-честному. Я делаю инструмент, а не твои моды. Что ты в нём соберёшь, кому покажешь и куда выложишь — " +
        "всё на тебе. За моды, сделанные в GenryBL, я не отвечаю никак: ни за то, что в них, ни за то, что с ними будет потом.\n\n" +
        "Выкладываешь в Мастерскую Steam — ставь метку «для взрослых» и не нарушай её правила. Забанят тебя, а не меня."

    background: Rectangle { color: "#e6070b09" }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 980)
        height: col.implicitHeight + 70
        radius: 18
        color: "#f7f0da"
        border.color: "#b9a57a"; border.width: 3
        scale: gate.opened ? 1 : 0.94
        Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }
        ColumnLayout {
            id: col
            x: 44; y: 34
            width: parent.width - 88
            spacing: 18
            RowLayout {
                spacing: 16
                Rectangle {
                    width: 96; height: 96; radius: 48
                    color: "#c0392b"
                    border.color: "#7d1d14"; border.width: 4
                    Text { anchors.centerIn: parent; text: "18+"; color: "white"; font.family: Theme.riffic; font.pixelSize: 38; font.bold: true }
                }
                ColumnLayout {
                    spacing: 2
                    Text { text: "Только для взрослых"; color: "#8a1f1a"; font.family: Theme.riffic; font.pixelSize: 40; font.bold: true }
                    Text { text: "прочитай, это недолго"; color: "#7a6a4f"; font.family: Theme.ui; font.pixelSize: 18 }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: gate.words
                color: Theme.ink; font.family: Theme.ui; font.pixelSize: 21
                lineHeight: 1.12
            }
            Text { text: "— Генри"; color: "#8a1f1a"; font.family: Theme.riffic; font.pixelSize: 24; font.italic: true; Layout.alignment: Qt.AlignRight }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 12
                PillButton { text: "Мне нет 18 — выйти"; onClicked: Qt.quit() }
                PillButton {
                    accent: true
                    text: "Мне есть 18, погнали"
                    implicitWidth: 260; implicitHeight: 46
                    onClicked: { Engine.ageOk = true; gate.accepted(); gate.close() }
                }
            }
        }
    }
}
