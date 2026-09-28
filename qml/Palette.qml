import QtQuick
import QtQuick.Controls
import GenryBL

// «Штаб» (the old constructor's left panel, rebuilt): search every command (Ctrl+K), the pinned
// quick inserts (★, Alt+1..9), recent commands, then every command of data/forms.json by category
// (headers fold). Hover = the preview tries it on, click = its window, ⤓ = the default line
// straight under the cursor. A merged command's variants are found by their own names
// («вспышка» -> «Тряска и вспышка · Вспышка»).
Item {
    id: pal
    signal insert(string cmd)
    signal hoverCommand(string cmd)
    signal openForm(var row)
    signal openTab(int index, string query)
    signal leave()                               // Esc in the search: back to the story

    // ---------------------------------------------------------------- data
    readonly property var all: Engine.commands   // read once: C++ builds every row's line
    readonly property var byKey: {
        const m = {}
        for (const r of all) {
            if (!r.also) m[r.key] = r
            for (const v of r.variants || []) if (!m[v.key]) m[v.key] = v
        }
        return m
    }
    readonly property var defaultPins: [
        { key: "label" }, { tab: 2, title: "Фон" }, { tab: 1, title: "Персонаж" },
        { key: "say" }, { key: "say/narr", title: "Рассказчик" }, { key: "choice", title: "Выбор" },
        { key: "dialogue", title: "Диалог" }, { key: "jump/jump", title: "Переход" }, { key: "note" },
        { tab: 3, title: "Музыка" }, { key: "stop/all", title: "Тишина" }, { key: "jump/scene", title: "Конец сцены" }
    ]
    property var pins: load("ui/quickPins", defaultPins)
    property var recent: load("ui/recentCmds", [])
    property var collapsed: load("ui/paletteCollapsed", ({}))
    property bool editPins: false
    property bool showHelp: String(Engine.setting("ui/paletteHelp", "false")) === "true"
    readonly property bool searching: search.text.trim() !== ""
    property alias searchText: search.text

    function load(k, def) {
        try { const v = JSON.parse(String(Engine.setting(k, "")) || "null"); return v === null ? def : v }
        catch (e) { return def }
    }
    function save(k, v) { Engine.setSetting(k, JSON.stringify(v)) }
    function rowOf(pin) { return pin && pin.key ? byKey[pin.key] || null : null }
    function titleOf(pin) { const r = rowOf(pin); return pin.title || (r ? r.short || r.title : "?") }
    function colorOf(pin) {
        if (pin.tab !== undefined) return ["", "#5fb3ff", "#8be9fd", "#50fa7b"][pin.tab] || Theme.accent
        const r = rowOf(pin); return r ? Theme.categoryColor(r.category) : Theme.faint
    }
    function isPinned(key) { return !!key && pins.some(p => p.key === key) }
    function setPins(a) { pins = a.slice(0, 15); save("ui/quickPins", pins) }
    function togglePin(row) { setPins(isPinned(row.key) ? pins.filter(p => p.key !== row.key) : pins.concat([{ key: row.key }])) }
    function movePin(i, d) {
        const a = pins.slice(), j = i + d
        if (j < 0 || j >= a.length) return
        const t = a[i]; a[i] = a[j]; a[j] = t
        setPins(a)
    }
    function touch(row) {
        recent = [row.key].concat(recent.filter(k => k !== row.key)).slice(0, 8)
        save("ui/recentCmds", recent)
    }
    function run(row) { if (!row) return; touch(row); Sfx.click(); pal.openForm(row) }
    function quick(row) { if (!row) return; touch(row); Sfx.click(); pal.insert(row.templ) }
    function runPin(i) {
        const pin = pins[i]
        if (!pin) return
        if (pin.tab !== undefined) { Sfx.click(); pal.openTab(pin.tab, ""); return }
        run(rowOf(pin))
    }
    function toggleCat(cat) {
        const c = Object.assign({}, collapsed)
        if (c[cat]) delete c[cat]; else c[cat] = true
        collapsed = c
        save("ui/paletteCollapsed", c)
    }
    function focusSearch() { search.forceActiveFocus(); search.selectAll() }

    // search: a merged form shows the variants that match by name or command word; the form
    // itself only when none of its variants matched (so «вспышка» is one row, not two)
    readonly property var found: {
        const q = search.text.trim().toLowerCase()
        if (!q) return []
        const out = []
        for (const r of all) {
            if (r.also) continue
            const vs = (r.variants || []).filter(v => (v.label + " " + v.word + " " + v.templ).toLowerCase().indexOf(q) >= 0)
            if (vs.length) { for (const v of vs) out.push(v); continue }
            if ((r.title + " " + r.help + " " + r.templ).toLowerCase().indexOf(q) >= 0) out.push(r)
        }
        return out
    }
    // the same word in the pictures and sounds: a chip jumps to that tab with the search filled in
    readonly property var assetHits: {
        const q = search.text.trim().toLowerCase()
        if (q.length < 2) return []
        const n = (arr, f) => arr.filter(x => String(f(x)).toLowerCase().indexOf(q) >= 0).length
        return [[1, "Персонажи", n(Engine.cast, c => c.name + " " + c.id)],
                [2, "Фоны и CG", n(Engine.backgrounds, b => b.id) + n(Engine.cgs, b => b.id)],
                [3, "Звук", n(Engine.music, m => m.word) + n(Engine.sounds, m => m.word) + n(Engine.ambience, m => m.word)]].filter(x => x[2] > 0)
    }
    readonly property var grouped: {
        const out = []
        let last = ""
        for (const r of all) {
            if (r.category !== last) {
                out.push({ header: r.category, count: all.filter(x => x.category === r.category).length })
                last = r.category
            }
            if (!collapsed[r.category]) out.push(r)
        }
        return out
    }

    // ---------------------------------------------------------------- search
    TextField {
        id: search
        x: 10; y: 10
        width: parent.width - 20
        placeholderText: "Найти команду…   Ctrl+K"
        placeholderTextColor: Theme.faint
        color: Theme.text
        font.family: Theme.ui
        font.pixelSize: 15
        selectByMouse: true
        rightPadding: 30
        background: Rectangle { radius: 8; color: Theme.bg; border.color: search.activeFocus ? Theme.accent : Theme.line }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
            visible: pal.searching
            text: "✕"; color: Theme.dim; font.pixelSize: 14
            MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: search.text = "" }
        }
        onTextChanged: list.currentIndex = pal.searching ? 0 : -1
        Keys.onDownPressed: list.incrementCurrentIndex()
        Keys.onUpPressed: list.decrementCurrentIndex()
        Keys.onReturnPressed: (e) => { const r = pal.found[list.currentIndex]; if (e.modifiers & Qt.ShiftModifier) pal.quick(r); else pal.run(r) }
        Keys.onEnterPressed: (e) => { const r = pal.found[list.currentIndex]; if (e.modifiers & Qt.ShiftModifier) pal.quick(r); else pal.run(r) }
        Keys.onEscapePressed: { search.text = ""; pal.leave() }
    }

    // ---------------------------------------------------------------- list (quick grid + recent as its header)
    ListView {
        id: list
        anchors.top: search.bottom; anchors.topMargin: 8
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        clip: true
        spacing: 2
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        currentIndex: -1
        highlightFollowsCurrentItem: true
        model: pal.searching ? pal.found : pal.grouped

        header: Column {
            width: list.width
            visible: !pal.searching
            height: pal.searching ? 0 : implicitHeight
            spacing: 6
            bottomPadding: 6

            // quick inserts
            Item {
                width: parent.width; height: 24
                Text { x: 12; anchors.verticalCenter: parent.verticalCenter; text: "БЫСТРЫЕ ВСТАВКИ"; color: Theme.gold; font.family: Theme.riffic; font.pixelSize: 13 }
                Row {
                    anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Text {
                        visible: pal.editPins
                        text: "сбросить"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 12
                        MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor; onClicked: pal.setPins(pal.defaultPins) }
                    }
                    Text {
                        text: pal.editPins ? "готово" : "✎"; color: pal.editPins ? Theme.accent : Theme.dim; font.family: Theme.ui; font.pixelSize: 13
                        MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor; onClicked: pal.editPins = !pal.editPins }
                        ToolTip.visible: !pal.editPins && hoverHint.containsMouse
                        ToolTip.text: "Убрать или переставить. Добавить — ★ у любой команды ниже"
                        MouseArea { id: hoverHint; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                    }
                }
            }
            Grid {
                x: 6
                width: parent.width - 12
                columns: 3
                spacing: 5
                Repeater {
                    model: pal.pins
                    delegate: Rectangle {
                        id: tile
                        required property var modelData
                        required property int index
                        readonly property var row: pal.rowOf(modelData)
                        width: (list.width - 12 - 10) / 3
                        height: 36
                        radius: 8
                        color: tileArea.containsMouse ? Theme.panel3 : Theme.panel2
                        border.color: tileArea.containsMouse ? pal.colorOf(modelData) : Theme.line
                        Rectangle { x: 0; width: 4; height: parent.height; radius: 2; color: pal.colorOf(modelData) }
                        Text {
                            x: 12; width: parent.width - 30
                            anchors.verticalCenter: parent.verticalCenter
                            text: pal.titleOf(modelData)
                            elide: Text.ElideRight
                            color: Theme.text; font.family: Theme.ui; font.pixelSize: 14; font.bold: true
                        }
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: 6; anchors.top: parent.top; anchors.topMargin: 3
                            visible: index < 9 && !pal.editPins
                            text: index + 1; color: Theme.faint; font.family: Theme.mono; font.pixelSize: 10
                        }
                        MouseArea {
                            id: tileArea
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onEntered: if (parent.row) pal.hoverCommand(parent.row.templ)
                            onExited: pal.hoverCommand("")
                            onClicked: (m) => {
                                if (m.button === Qt.RightButton) { tileMenu.index = index; tileMenu.popup(); return }
                                pal.runPin(index)
                            }
                        }
                        ToolTip.visible: tileArea.containsMouse && !!row
                        ToolTip.delay: 700
                        ToolTip.text: row ? row.title + (index < 9 ? "   ·   Alt+" + (index + 1) : "") + "\nПравый клик — вставить сразу / убрать" : ""
                        Row {                                   // edit mode: move / remove
                            visible: pal.editPins
                            anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Repeater {
                                model: [["‹", -1], ["›", 1], ["✕", 0]]
                                Text {
                                    required property var modelData
                                    text: modelData[0]; color: modelData[1] ? Theme.dim : Theme.bad; font.pixelSize: 14; leftPadding: 3; rightPadding: 3
                                    MouseArea {
                                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: parent.modelData[1] ? pal.movePin(tile.index, parent.modelData[1]) : pal.setPins(pal.pins.filter((p, j) => j !== tile.index))
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // recent
            Flow {
                x: 10; width: parent.width - 20
                spacing: 5
                visible: pal.recent.length > 0
                Text { text: "Недавние:"; color: Theme.faint; font.family: Theme.ui; font.pixelSize: 12; height: 26; verticalAlignment: Text.AlignVCenter }
                Repeater {
                    model: pal.recent.map(k => pal.byKey[k]).filter(r => !!r)
                    PillButton {
                        required property var modelData
                        dark: true
                        implicitHeight: 26
                        text: modelData.short || modelData.title
                        onClicked: pal.run(modelData)
                        onHoveredChanged: pal.hoverCommand(hovered ? modelData.templ : "")
                    }
                }
            }
        }

        footer: Flow {                              // search: the same word among pictures and sounds
            width: list.width
            visible: pal.searching
            height: pal.searching ? implicitHeight : 0
            padding: 10
            spacing: 6
            Text {
                text: pal.found.length ? "Ещё найдено:" : "Команд не нашлось. Поищи в картинках и звуках:"
                visible: pal.assetHits.length > 0
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13; height: 28; verticalAlignment: Text.AlignVCenter
            }
            Repeater {
                model: pal.assetHits
                PillButton {
                    required property var modelData
                    dark: true
                    text: modelData[1] + " · " + modelData[2]
                    onClicked: pal.openTab(modelData[0], search.text.trim())
                }
            }
        }

        delegate: Item {
            id: dl
            required property var modelData
            required property int index
            width: list.width
            height: modelData.header ? 34 : row.height

            // category header: click folds it
            Item {
                visible: !!dl.modelData.header
                anchors.fill: parent
                Row {
                    x: 12; y: 12; spacing: 8
                    Text { text: pal.collapsed[dl.modelData.header || ""] ? "▸" : "▾"; color: Theme.faint; font.pixelSize: 13 }
                    Rectangle { width: 10; height: 10; radius: 5; color: Theme.categoryColor(dl.modelData.header || ""); anchors.verticalCenter: parent.verticalCenter }
                    Text { text: (dl.modelData.header || "") + "  " + (dl.modelData.count || ""); color: Theme.categoryColor(dl.modelData.header || ""); font.family: Theme.riffic; font.pixelSize: 15 }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: pal.toggleCat(dl.modelData.header) }
            }

            Rectangle {
                id: row
                visible: !dl.modelData.header
                width: list.width - 12
                x: 6
                height: dl.modelData.header ? 0 : col.implicitHeight + 14
                radius: 8
                readonly property bool lit: area.containsMouse || (pal.searching && list.currentIndex === dl.index)
                color: lit ? Theme.panel3 : "transparent"
                border.color: lit ? Theme.categoryColor(dl.modelData.category || "") : "transparent"
                Behavior on color { ColorAnimation { duration: 100 } }
                Column {
                    id: col
                    x: 12; y: 7
                    width: parent.width - 24 - tools.width
                    spacing: 2
                    Text { text: dl.modelData.title || ""; color: Theme.text; font.family: Theme.ui; font.pixelSize: 15; font.bold: true; width: parent.width; elide: Text.ElideRight }
                    Text {
                        width: parent.width
                        text: (dl.modelData.templ || "").split("\n")[0]
                        color: Theme.categoryColor(dl.modelData.category || "")
                        font.family: Theme.mono; font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Text {                              // the old Command Center's description line
                        visible: pal.showHelp || pal.searching
                        width: parent.width
                        text: dl.modelData.help || ""
                        wrapMode: Text.Wrap
                        maximumLineCount: pal.searching ? 2 : 3
                        elide: Text.ElideRight
                        color: Theme.dim; font.family: Theme.ui; font.pixelSize: 12
                    }
                }
                ToolTip.visible: area.containsMouse && !!dl.modelData.help && !pal.showHelp && !pal.searching
                ToolTip.delay: 600
                ToolTip.text: dl.modelData.help || ""
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: pal.hoverCommand(dl.modelData.templ)
                    onExited: pal.hoverCommand("")
                    onClicked: pal.run(dl.modelData)
                }
                Row {
                    id: tools
                    anchors.right: parent.right; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    readonly property bool on: area.containsMouse || star.containsMouse || qa.containsMouse || pal.isPinned(dl.modelData.key)
                    // ★ pin to the quick inserts
                    Rectangle {
                        width: 30; height: 30; radius: 15
                        visible: tools.on
                        color: star.containsMouse ? Theme.panel2 : "transparent"
                        Text { anchors.centerIn: parent; text: pal.isPinned(dl.modelData.key) ? "★" : "☆"; color: pal.isPinned(dl.modelData.key) ? Theme.gold : Theme.dim; font.pixelSize: 16 }
                        MouseArea { id: star; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pal.togglePin(dl.modelData) }
                        ToolTip.visible: star.containsMouse
                        ToolTip.text: pal.isPinned(dl.modelData.key) ? "Убрать из быстрых вставок" : "В быстрые вставки"
                    }
                    // ⤓ straight in
                    Rectangle {
                        width: 30; height: 30; radius: 15
                        visible: area.containsMouse || qa.containsMouse || star.containsMouse
                        color: qa.containsMouse ? Theme.accent : Theme.panel2
                        border.color: Theme.categoryColor(dl.modelData.category || "")
                        Text { anchors.centerIn: parent; text: "⤓"; color: qa.containsMouse ? "#16240c" : Theme.text; font.pixelSize: 15 }
                        MouseArea {
                            id: qa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: pal.hoverCommand(dl.modelData.templ)
                            onClicked: pal.quick(dl.modelData)
                        }
                        ToolTip.visible: qa.containsMouse
                        ToolTip.text: "Вставить сразу, без настройки (Shift+Enter в поиске)"
                    }
                }
            }
        }
    }

    Menu {
        id: tileMenu
        property int index: -1
        readonly property var row: pal.rowOf(pal.pins[index])
        MenuItem { text: "Вставить сразу"; enabled: !!tileMenu.row; onTriggered: pal.quick(tileMenu.row) }
        MenuItem { text: "Открыть окно команды"; enabled: !!tileMenu.row; onTriggered: pal.run(tileMenu.row) }
        MenuSeparator {}
        MenuItem { text: "‹ Левее"; onTriggered: pal.movePin(tileMenu.index, -1) }
        MenuItem { text: "Правее ›"; onTriggered: pal.movePin(tileMenu.index, 1) }
        MenuItem { text: "Убрать из быстрых"; onTriggered: pal.setPins(pal.pins.filter((p, j) => j !== tileMenu.index)) }
        MenuSeparator {}
        MenuItem {
            text: pal.showHelp ? "Прятать описания команд" : "Показывать описания команд"
            onTriggered: { pal.showHelp = !pal.showHelp; Engine.setSetting("ui/paletteHelp", pal.showHelp ? "true" : "false") }
        }
    }
}
