import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «История версий»: the project's past (src/core/History). GenryBL keeps a copy of the story by itself - every
// few minutes of work, when the project is opened, right BEFORE a big cut and before a version is brought back -
// so nothing typed is lost for good. Left: the versions; right: what bringing one back does to the text now.
Popup {
    id: hd
    property string projectId: ""
    property string currentText: ""
    property var versions: []
    property int current: -1
    property var diffRows: []
    signal restored(string text)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 1240)
    height: Math.min(parent.height - 50, 780)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openFor(id, text) {
        projectId = id
        currentText = text
        versions = Engine.historyList(id)
        current = -1
        diffRows = []
        open()
        if (versions.length) pick(0)
    }
    function pick(i) {
        current = i
        diffRows = i >= 0 ? Engine.historyDiff(projectId, versions[i].file, currentText) : []
        diffList.positionViewAtBeginning()
    }
    function tagText(t) {
        return t === "open" ? qsTr("проект открыт") : t === "cut" ? qsTr("перед большим удалением") : t === "restore" ? qsTr("перед откатом")
             : t === "modid" ? qsTr("перед сменой имени мода") : t === "doctor" ? qsTr("перед починкой доктором") : qsTr("работа")
    }
    readonly property int comes: diffRows.filter(r => r.kind === "+").length
    readonly property int goes: diffRows.filter(r => r.kind === "-").length

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            InkText { text: qsTr("История версий"); size: 32; color: Theme.gold }
            Item { Layout.fillWidth: true }
            PillButton { dark: true; text: "✕"; onClicked: hd.close() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("GenryBL сам хранит копии сценария: каждые несколько минут работы, при открытии проекта и прямо перед тем, как из текста пропадает большой кусок. ") +
                  qsTr("Выбери версию — справа видно, что вернётся (зелёным) и что уйдёт (красным). Текущий текст перед откатом тоже сохраняется.")
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            // ---- the versions
            Rectangle {
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                radius: 10
                color: Theme.bg2
                border.color: Theme.line
                ListView {
                    id: versionList
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    spacing: 3
                    model: hd.versions
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: versionList.width
                        height: 58
                        radius: 8
                        color: hd.current === index ? Theme.panel3 : va.containsMouse ? Theme.panel2 : "transparent"
                        border.color: hd.current === index ? Theme.gold : "transparent"
                        Column {
                            x: 12; anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Text { text: modelData.ago; color: Theme.text; font.family: Theme.ui; font.pixelSize: 16; font.bold: true }
                            Row {
                                spacing: 10
                                Text { text: hd.tagText(modelData.tag); color: modelData.tag === "cut" ? Theme.warn : Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                                Text { text: modelData.lines + qsTr(" стр."); color: Theme.faint; font.family: Theme.mono; font.pixelSize: 12 }
                                Text { visible: modelData.add > 0; text: "+" + modelData.add; color: Theme.good; font.family: Theme.mono; font.pixelSize: 12 }
                                Text { visible: modelData.del > 0; text: "−" + modelData.del; color: Theme.bad; font.family: Theme.mono; font.pixelSize: 12 }
                            }
                        }
                        MouseArea { id: va; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: hd.pick(index) }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 40
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: hd.versions.length === 0
                    text: qsTr("Пока ни одной копии — они появятся сами, пока ты пишешь")
                    color: Theme.faint; font.family: Theme.ui; font.pixelSize: 15
                }
            }

            // ---- what bringing it back changes
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    visible: hd.current >= 0
                    text: hd.current < 0 ? "" : hd.diffRows.length === 0 ? qsTr("Эта версия совпадает с текущим текстом")
                        : qsTr("Если вернуть: ") + "<font color='#57c785'>" + qsTr("вернётся строк: ") + hd.comes + "</font>   ·   <font color='#ff5566'>" + qsTr("уйдёт строк: ") + hd.goes + "</font>"
                    textFormat: Text.StyledText
                    color: Theme.text; font.family: Theme.ui; font.pixelSize: 15
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 10
                    color: Theme.bg
                    border.color: Theme.line
                    ListView {
                        id: diffList
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        model: hd.diffRows
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            width: diffList.width
                            height: Math.max(22, dt.implicitHeight + 4)
                            color: modelData.kind === "+" ? "#2257c785" : modelData.kind === "-" ? "#22ff5566" : "transparent"
                            Text {
                                width: 46
                                x: 4; anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignRight
                                text: modelData.kind === "~" ? "" : modelData.line
                                color: Theme.faint; font.family: Theme.mono; font.pixelSize: 12
                            }
                            Text {
                                id: dt
                                x: 58; width: parent.width - 64
                                anchors.verticalCenter: parent.verticalCenter
                                wrapMode: Text.WrapAnywhere
                                text: modelData.kind === "~" ? "⋯" : (modelData.kind === "+" ? "+ " : modelData.kind === "-" ? "− " : "  ") + modelData.text
                                textFormat: Text.PlainText
                                color: modelData.kind === "+" ? "#b8f0c8" : modelData.kind === "-" ? "#ffb3bd" : modelData.kind === "~" ? Theme.faint : Theme.dim
                                font.family: Theme.mono; font.pixelSize: 13
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Item { Layout.fillWidth: true }
                    PillButton {
                        dark: true
                        text: qsTr("Скопировать текст версии")
                        enabled: hd.current >= 0
                        onClicked: { Engine.copyText(Engine.historyText(hd.projectId, hd.versions[hd.current].file)); Engine.toast(qsTr("Текст версии скопирован"), 0) }
                    }
                    PillButton {
                        accent: true
                        text: qsTr("Вернуть эту версию")
                        enabled: hd.current >= 0 && hd.diffRows.length > 0
                        onClicked: {
                            const v = hd.versions[hd.current]
                            const text = Engine.restoreHistory(hd.projectId, v.file, hd.currentText)
                            if (text === undefined || text === null) return
                            Sfx.click()
                            hd.restored(text)
                            Engine.toast(qsTr("Вернул версию «") + v.ago + qsTr("». То, что было до отката, тоже лежит в истории"), 0)
                            hd.close()
                        }
                    }
                }
            }
        }
    }
}
