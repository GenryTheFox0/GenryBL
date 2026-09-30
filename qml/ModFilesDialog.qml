import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Файлы мода»: what really goes to players. The mod is built (as «Играть» does) and every file in its folder is
// counted by kind, the biggest first; the files the mod points at but does not have, and the project's own files the
// story never names (they would ride along for nothing) - those go to the project's «_unused» with one click.
Popup {
    id: mf
    property string projectId: ""
    property var report: ({})
    property bool working: false
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 1100)
    height: Math.min(parent.height - 50, 760)
    modal: true
    padding: 0
    closePolicy: working ? Popup.NoAutoClose : Popup.CloseOnEscape

    function openFor(id, text) {
        projectId = id
        report = ({})
        working = true
        open()
        Engine.modFiles(id, text)
    }
    Connections {
        target: Engine
        function onModFilesReady(r) {
            if (!mf.opened) return
            mf.working = false
            mf.report = r
        }
    }
    function mb(n) { return n >= 1048576 ? (n / 1048576).toFixed(1) + qsTr(" МБ") : Math.max(1, Math.round(n / 1024)) + qsTr(" КБ") }
    function kindName(k) {
        return { code: qsTr("код мода (.rpy)"), images: qsTr("твои картинки"), audio: qsTr("твой звук"), video: qsTr("видео"), fonts: qsTr("шрифты"),
                 wardrobe: qsTr("одежда из Мастерской"), patch: qsTr("CG из 18+ патча"), genrybl: qsTr("частицы, телефон, чиби GenryBL"), other: qsTr("другое") }[k] || k
    }
    readonly property var groups: {
        const files = report.files || []
        const by = {}
        for (const f of files) { if (!by[f.kind]) by[f.kind] = { kind: f.kind, size: 0, count: 0, files: [] }; by[f.kind].size += f.size; by[f.kind].count++; by[f.kind].files.push(f) }
        const out = Object.values(by)
        out.sort((a, b) => b.size - a.size)
        for (const g of out) g.files.sort((a, b) => b.size - a.size)
        return out
    }
    readonly property real maxGroup: groups.length ? groups[0].size : 1

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            InkText { text: qsTr("Файлы мода"); size: 32; color: Theme.gold }
            Text {
                visible: !mf.working && !mf.report.error
                text: "  " + mf.mb(mf.report.total || 0) + "  ·  " + (mf.report.files || []).length + qsTr(" файлов")
                color: Theme.text; font.family: Theme.ui; font.pixelSize: 18; font.bold: true
            }
            Item { Layout.fillWidth: true }
            PillButton { dark: true; text: "✕"; enabled: !mf.working; onClicked: mf.close() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Всё, что уедет к игрокам: мод собран так же, как для «Играть», и посчитан по файлам. Больше всего места — сверху.")
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15
        }
        BusyIndicator { visible: mf.working; running: visible; Layout.alignment: Qt.AlignHCenter }
        Text {
            visible: !!mf.report.error
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: mf.report.error || ""
            color: Theme.bad; font.family: Theme.ui; font.pixelSize: 16
        }
        ScrollView {
            visible: !mf.working && !mf.report.error
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ColumnLayout {
                width: mf.width - 60
                spacing: 8
                // what is missing: the mod points at it, it is not in the folder
                Rectangle {
                    visible: (mf.report.missing || []).length > 0
                    Layout.fillWidth: true
                    implicitHeight: missCol.implicitHeight + 20
                    radius: 10
                    color: "#22ff6b6b"
                    border.color: Theme.bad
                    Column {
                        id: missCol
                        x: 12; y: 10
                        width: parent.width - 24
                        spacing: 4
                        Text { text: qsTr("✖ Мод ссылается на файлы, которых в нём нет — у игрока будет ошибка:"); color: Theme.bad; font.family: Theme.ui; font.pixelSize: 15; font.bold: true }
                        Repeater { model: mf.report.missing || []; Text { required property var modelData; text: "   " + modelData; color: Theme.text; font.family: Theme.mono; font.pixelSize: 13 } }
                    }
                }
                // the groups, biggest first
                Repeater {
                    model: mf.groups
                    Rectangle {
                        id: grp
                        required property var modelData
                        property bool open: false
                        Layout.fillWidth: true
                        implicitHeight: 48 + (open ? fileCol.implicitHeight + 8 : 0)
                        radius: 10
                        color: Theme.bg2
                        border.color: Theme.line
                        clip: true
                        Rectangle {
                            x: 1; y: 1
                            width: (parent.width - 2) * Math.max(0.02, grp.modelData.size / mf.maxGroup)
                            height: 46
                            radius: 9
                            color: "#223ba0ff"
                        }
                        Text { x: 14; y: 14; text: (grp.open ? "▾ " : "▸ ") + mf.kindName(grp.modelData.kind); color: Theme.text; font.family: Theme.ui; font.pixelSize: 16; font.bold: true }
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: 14; y: 14
                            text: mf.mb(grp.modelData.size) + "  ·  " + grp.modelData.count + qsTr(" файлов")
                            color: Theme.dim; font.family: Theme.mono; font.pixelSize: 14
                        }
                        MouseArea { width: parent.width; height: 48; cursorShape: Qt.PointingHandCursor; onClicked: grp.open = !grp.open }
                        Column {
                            id: fileCol
                            visible: grp.open
                            x: 28; y: 50
                            width: parent.width - 42
                            Repeater {
                                model: grp.open ? grp.modelData.files.slice(0, 300) : []
                                Item {
                                    required property var modelData
                                    width: fileCol.width; height: 22
                                    Text { width: parent.width - 110; elide: Text.ElideMiddle; text: modelData.path; color: modelData.size > 5242880 ? Theme.warn : Theme.dim; font.family: Theme.mono; font.pixelSize: 13 }
                                    Text { anchors.right: parent.right; text: mf.mb(modelData.size); color: modelData.size > 5242880 ? Theme.warn : Theme.faint; font.family: Theme.mono; font.pixelSize: 13 }
                                }
                            }
                        }
                    }
                }
                // the project's files nobody uses
                Rectangle {
                    visible: (mf.report.unused || []).length > 0
                    Layout.fillWidth: true
                    implicitHeight: unCol.implicitHeight + 20
                    radius: 10
                    color: "#18ffc857"
                    border.color: Theme.warn
                    Column {
                        id: unCol
                        x: 12; y: 10
                        width: parent.width - 24
                        spacing: 6
                        RowLayout {
                            width: parent.width
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("⚠ В проекте есть файлы, которые история нигде не называет — они уедут в мод зря:")
                                color: Theme.warn; font.family: Theme.ui; font.pixelSize: 15; font.bold: true
                            }
                            PillButton {
                                accent: true
                                text: qsTr("Убрать из мода")
                                onClicked: {
                                    Engine.tidyUnused(mf.projectId, (mf.report.unused || []).map(u => u.path))
                                    const r = Object.assign({}, mf.report)
                                    r.unused = []
                                    mf.report = r
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Файлы переедут в папку проекта «_unused» — не удалятся, просто не попадут в мод")
                            }
                        }
                        Repeater {
                            model: mf.report.unused || []
                            Text { required property var modelData; text: "   " + modelData.path + "   " + mf.mb(modelData.size); color: Theme.text; font.family: Theme.mono; font.pixelSize: 13 }
                        }
                    }
                }
            }
        }
    }
}
