import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Сломай мой мод» (src/core/Fuzz): GenryBL plays the mod through hundreds of times the way the cinema does - by
// random players and stubborn ones - and says what only a playthrough shows: an ending no choices lead to, a lock
// that never opens, a choice with every option shut, a circle with no way out, a mod that just stops. Every find
// has «▶ Как туда попасть»: the cinema plays that very route up to that moment.
Popup {
    id: bd
    property string storyText: ""
    property var report: null
    property bool waiting: false
    signal gotoLine(int line)
    signal watch(var route, int seed)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 1180)
    height: Math.min(parent.height - 40, 820)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openFor(text) {
        storyText = text
        report = null
        open()
        run()
    }
    function run() {
        waiting = true
        Engine.breakMod(storyText)
    }
    Connections {
        target: Engine
        function onBreakModReady(r) { if (bd.waiting) { bd.waiting = false; bd.report = r } }
    }

    function sceneName(s) { return s ? "«" + s + "»" : qsTr("до первой сцены") }
    // the report as rows: a head per group, then its finds
    readonly property var rows: {
        const r = report
        if (!r) return []
        const out = []
        const n = r.runs
        const bad = (r.problems || []).map(h => {
            let t
            if (h.kind === "fell") t = qsTr("Мод обрывается: сцена ") + sceneName(h.scene) + qsTr(" кончилась без перехода и без «конецигры» — игрок вылетит в меню БЛ без титров.")
            else if (h.kind === "missing") t = qsTr("Переход в сцену «") + h.detail + qsTr("», а её в истории нет — у игрока мод тут оборвётся.")
            else if (h.kind === "loop") t = qsTr("История ходит по кругу без единой реплики (сцена ") + sceneName(h.scene) + qsTr(") — игра зависнет.")
            else if (h.kind === "stuck") t = qsTr("Игрок застревает на выборе: все варианты закрыты замками, а таймера нет.")
            else t = qsTr("Мод не кончился и за тысячи кликов — сцены гоняют игрока по кругу без выхода (сцена ") + sceneName(h.scene) + ")."
            return { text: t, sub: qsTr("Так закончились ") + h.share + qsTr("% прохождений"), line: h.line, route: h.route, seed: h.seed, color: Theme.bad }
        })
        const locks = (r.locked || []).map(h => ({
            text: h.kind === "lock" ? qsTr("Вариант «") + h.detail + qsTr("» не открылся ни у одного игрока") + (h.hint ? " (" + h.hint + ")" : "") + qsTr(" — то, что под ним, не увидит никто.")
                                    : qsTr("Вариант «") + h.detail + qsTr("» ни разу не появился: его условие «если» не выполнилось ни у кого."),
            sub: qsTr("Выбор в сцене ") + sceneName(h.scene), line: h.line, route: h.route, seed: h.seed, color: Theme.warn }))
        const unseen = (r.unseen || []).map(h => ({
            text: h.kind === "blocked" ? qsTr("Сцена «") + h.scene + qsTr("»: пути к ней есть, но ни одно прохождение туда не попало — не пускают условия или очки.")
                                       : qsTr("Сцена «") + h.scene + qsTr("»: к ней не ведёт ни один путь."),
            sub: "", line: h.line, route: [], color: Theme.warn }))
        const never = (r.never || []).map(h => ({
            text: qsTr("«конецигры» в сцене ") + sceneName(h.scene) + qsTr(" не сработал ни разу — до этой концовки не дойти."),
            sub: "", line: h.line, route: [], color: Theme.warn }))
        const ends = (r.endings || []).map(h => ({
            text: (h.kind === "sceneend" ? qsTr("«конецсцены» в сцене ") : qsTr("Концовка в сцене ")) + sceneName(h.scene),
            sub: h.share + qsTr("% прохождений"), share: h.share, line: h.line, route: h.route, seed: h.seed, color: Theme.good }))
        if (bad.length) out.push({ head: qsTr("Ломается"), color: Theme.bad }, ...bad)
        if (locks.length) out.push({ head: qsTr("Замки и условия, которые не открылись"), color: Theme.warn }, ...locks)
        if (unseen.length || never.length) out.push({ head: qsTr("Куда не попал никто"), color: Theme.warn }, ...unseen, ...never)
        if (ends.length) out.push({ head: qsTr("Концовки, до которых дошли"), color: Theme.good }, ...ends)
        return out
    }
    readonly property int troubles: report ? (report.problems || []).length + (report.locked || []).length + (report.unseen || []).length + (report.never || []).length : 0

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }

    component Stat: Rectangle {
        property string value
        property string label
        property color tint: Theme.text
        Layout.fillWidth: true
        Layout.preferredHeight: 74
        radius: 10
        color: Theme.bg2
        border.color: Theme.line
        Column {
            anchors.centerIn: parent
            spacing: 2
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: parent.parent.value; color: parent.parent.tint; font.family: Theme.ui; font.pixelSize: 26; font.bold: true }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: parent.parent.label; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            InkText { text: qsTr("Сломай мой мод"); size: 32; color: Theme.gold }
            Item { Layout.fillWidth: true }
            PillButton { dark: true; text: qsTr("↻ Ещё раз"); enabled: !bd.waiting; onClicked: bd.run() }
            PillButton { dark: true; text: "✕"; onClicked: bd.close() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("GenryBL проходит мод сотни раз, как разные игроки: наугад, упрямо первым вариантом, упрямо последним и тем, что выбирали реже всего. ") +
                  qsTr("Находит то, что видно только в игре: концовку, до которой не дойти, замок, который не открывается, выбор-тупик, петлю без выхода.")
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: !!bd.report
            Stat { value: bd.report ? bd.report.runs : ""; label: qsTr("прохождений") }
            Stat { value: bd.report ? (bd.report.endings || []).length : ""; label: qsTr("концовок достигнуто"); tint: Theme.good }
            Stat { value: bd.report ? bd.report.scenesSeen + " / " + bd.report.scenes : ""; label: qsTr("сцен пройдено"); tint: bd.report && bd.report.scenesSeen < bd.report.scenes ? Theme.warn : Theme.text }
            Stat { value: bd.report ? (bd.report.clicksMin === bd.report.clicksMax ? bd.report.clicksMin : bd.report.clicksMin + "–" + bd.report.clicksMax) : ""; label: qsTr("кликов до конца") }
            Stat { value: bd.report ? "≈ " + (bd.report.minutesMin === bd.report.minutesMax ? bd.report.minutesMin : bd.report.minutesMin + "–" + bd.report.minutesMax) : ""; label: qsTr("минут чтения") }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: Theme.bg2
            border.color: Theme.line
            Column {
                anchors.centerIn: parent
                visible: bd.waiting
                spacing: 10
                BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: bd.waiting }
                Text { text: qsTr("Прохожу мод за сотню игроков…"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 16 }
            }
            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 8
                visible: !bd.waiting
                clip: true
                spacing: 4
                model: bd.rows
                ScrollBar.vertical: ScrollBar {}
                // nothing broke: said under the endings
                footer: Item {
                    width: list.width
                    height: clean ? 150 : 0
                    readonly property bool clean: !!bd.report && bd.troubles === 0 && (bd.report.endings || []).length > 0
                    Column {
                        visible: parent.clean
                        anchors.centerIn: parent
                        spacing: 8
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "✓"; color: Theme.good; font.pixelSize: 44 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Сломать не вышло: все прохождения дошли до концовки"); color: Theme.text; font.family: Theme.ui; font.pixelSize: 17 }
                    }
                }
                delegate: Item {
                    id: row
                    required property var modelData
                    width: list.width - 12
                    height: modelData.head ? 40 : Math.max(52, body.implicitHeight + 18)
                    Text {
                        visible: !!row.modelData.head
                        x: 6; anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                        text: row.modelData.head || ""
                        color: row.modelData.color || Theme.gold
                        font.family: Theme.ui; font.pixelSize: 16; font.bold: true
                    }
                    Rectangle {
                        visible: !row.modelData.head
                        anchors.fill: parent
                        radius: 8
                        color: Theme.panel2
                        Rectangle { width: 4; height: parent.height; radius: 2; color: row.modelData.color || Theme.line }
                        // an ending: its share of the walks as a bar
                        Rectangle {
                            visible: row.modelData.share !== undefined
                            x: 4; y: parent.height - 4
                            width: (parent.width - 8) * (row.modelData.share || 0) / 100; height: 3
                            color: Theme.good; opacity: 0.6
                        }
                        Column {
                            id: body
                            x: 16; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 16 - btns.width - 20
                            spacing: 3
                            Text { width: parent.width; wrapMode: Text.Wrap; text: row.modelData.text || ""; color: Theme.text; font.family: Theme.ui; font.pixelSize: 15 }
                            Text { visible: !!row.modelData.sub; width: parent.width; text: row.modelData.sub || ""; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                        }
                        Row {
                            id: btns
                            anchors.right: parent.right; anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            PillButton {
                                visible: !!row.modelData.route && row.modelData.route.length > 0
                                dark: true
                                text: qsTr("▶ Как туда попасть")
                                onClicked: bd.watch(row.modelData.route, row.modelData.seed || 0)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Кино проиграет именно этот путь — выборы, клики — и остановится на этом месте")
                            }
                            PillButton {
                                visible: row.modelData.line > 0
                                dark: true
                                text: qsTr("стр. ") + row.modelData.line
                                onClicked: bd.gotoLine(row.modelData.line)
                            }
                        }
                    }
                }
            }
        }
    }
}
