import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Поиск по всем модам» (Ctrl+Shift+F): a word through the stories of every project at once - where that song
// played, which mod had that line, where Славя said it. The open project first (as it is in the editor, unsaved
// too); a click opens the line, the word stays in the editor's find bar.
Popup {
    id: sa
    property string projectId: ""
    property string storyText: ""
    property var hits: []
    property bool caseOn: false
    signal openAt(string id, int line, string query, bool cs)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 1100)
    height: Math.min(parent.height - 50, 760)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openWith(q) {
        Engine.refreshProjects()
        if (q) field.text = q
        open()
        field.forceActiveFocus()
        field.selectAll()
        run()
    }
    function run() { hits = Engine.searchProjects(field.text, caseOn, projectId, storyText); list.currentIndex = -1; list.positionViewAtBeginning() }
    readonly property int modCount: { const s = {}; for (const h of hits) s[h.id] = 1; return Object.keys(s).length }
    function esc(t) { return t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
    function rich(h) {
        let pre = h.text.substring(0, h.col).replace(/^\s+/, "")
        if (pre.length > 60) pre = "…" + pre.substring(pre.length - 60)
        return esc(pre) + "<b><font color='#ffc857'>" + esc(h.text.substr(h.col, h.len)) + "</font></b>" + esc(h.text.substring(h.col + h.len))
    }

    Timer { id: typing; interval: 220; onTriggered: sa.run() }
    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            InkText { text: qsTr("Поиск по всем модам"); size: 32; color: Theme.gold }
            Item { Layout.fillWidth: true }
            PillButton { dark: true; text: "✕"; onClicked: sa.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: field
                Layout.fillWidth: true
                placeholderText: qsTr("Слово, реплика, песня, фон, имя сцены…")
                placeholderTextColor: Theme.faint
                color: Theme.text
                font.family: Theme.mono; font.pixelSize: 16
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.bg; border.color: field.activeFocus ? Theme.accent : Theme.line }
                onTextEdited: typing.restart()
                Keys.onReturnPressed: { typing.stop(); sa.run(); if (sa.hits.length) sa.openAt(sa.hits[0].id, sa.hits[0].line, field.text, sa.caseOn) }
                Keys.onEnterPressed: { typing.stop(); sa.run(); if (sa.hits.length) sa.openAt(sa.hits[0].id, sa.hits[0].line, field.text, sa.caseOn) }
                Keys.onDownPressed: list.forceActiveFocus()
            }
            PillButton {
                dark: !sa.caseOn
                accent: sa.caseOn
                text: "Aa"
                onClicked: { sa.caseOn = !sa.caseOn; sa.run() }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Учитывать большие и маленькие буквы")
            }
        }
        Text {
            Layout.fillWidth: true
            text: !field.text.trim() ? qsTr("Ищет по сценариям всех твоих проектов сразу. Клик по строке — откроется это место.")
                : !sa.hits.length ? qsTr("Нигде нет")
                : qsTr("Найдено строк: ") + sa.hits.length + (sa.hits.length >= 3000 ? "+" : "") + qsTr(" · модов: ") + sa.modCount
            color: field.text.trim() && !sa.hits.length ? Theme.warn : Theme.dim
            font.family: Theme.ui; font.pixelSize: 14
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: Theme.bg2
            border.color: Theme.line
            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: sa.hits
                keyNavigationEnabled: true
                currentIndex: -1
                onActiveFocusChanged: if (activeFocus && currentIndex < 0 && count) currentIndex = 0
                ScrollBar.vertical: ScrollBar {}
                Keys.onReturnPressed: if (currentIndex >= 0) sa.openAt(sa.hits[currentIndex].id, sa.hits[currentIndex].line, field.text, sa.caseOn)
                delegate: Column {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool head: index === 0 || sa.hits[index - 1].id !== modelData.id
                    width: list.width
                    Item {
                        visible: row.head
                        width: parent.width
                        height: visible ? 34 : 0
                        Text {
                            x: 8; anchors.bottom: parent.bottom; anchors.bottomMargin: 5
                            text: row.modelData.name + (row.modelData.id === sa.projectId ? qsTr("   · открыт сейчас") : "")
                            color: row.modelData.id === sa.projectId ? Theme.accent : Theme.gold
                            font.family: Theme.ui; font.pixelSize: 15; font.bold: true
                        }
                    }
                    Rectangle {
                        width: parent.width
                        height: 30
                        radius: 6
                        color: list.activeFocus && list.currentIndex === row.index ? "#33ff5fae" : ha.containsMouse ? Theme.panel3 : "transparent"
                        Text {
                            x: 14; width: 70
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("стр. ") + row.modelData.line
                            color: Theme.dim; font.family: Theme.mono; font.pixelSize: 13
                        }
                        Text {
                            x: 92; width: parent.width - 104
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            textFormat: Text.StyledText
                            text: sa.rich(row.modelData)
                            color: Theme.text; font.family: Theme.mono; font.pixelSize: 14
                        }
                        MouseArea {
                            id: ha
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: sa.openAt(row.modelData.id, row.modelData.line, field.text, sa.caseOn)
                        }
                    }
                }
            }
        }
    }
}
