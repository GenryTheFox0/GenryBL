import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// The Steam Workshop as a library: every picture of every installed ES mod (loose and inside
// .rpa), by mod and folder or by search across all of them. «В мой мод» copies the picture into
// the project as a character, background or CG and notes the author in CREDITS.txt.
Popup {
    id: lb
    property string itemId: ""
    property string itemTitle: ""
    property string folder: ""
    property string selected: ""           // ref "<id>/<vpath>"
    property string kind: "sprite"
    property var listing: ({ folders: [], files: [], total: 0 })
    signal insertLine(string cmd)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 1700)
    height: Math.min(parent.height - 30, 960)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    onOpened: { Engine.libraryScan(); refresh() }

    function refresh() {
        if (Engine.libraryState !== 2) return
        if (!itemId && !search.text.trim()) { listing = { folders: [], files: [], total: 0 }; return }
        listing = Engine.libraryList(itemId, folder, search.text.trim(), 600)
    }
    function openItem(id, title) { itemId = id; itemTitle = title; folder = ""; selected = ""; refresh() }
    function openFolder(name) { folder = folder ? folder + "/" + name : name; selected = ""; refresh() }
    function up(level) {                 // breadcrumb: keep the first `level` parts
        folder = folder.split("/").slice(0, level).join("/")
        selected = ""
        refresh()
    }
    function baseName(ref) { const f = ref.split("/").pop(); return f.replace(/\.[^.]+$/, "") }
    function guessKind(ref) {
        const l = ref.toLowerCase()
        if (/(^|\/)(bg|backgrounds?|фоны?)\//.test(l)) return "bg"
        if (/(^|\/)(cg|арты?)\//.test(l)) return "cg"
        return "sprite"
    }
    function select(ref) {
        selected = ref
        kind = guessKind(ref)
        nameField.text = kind === "sprite" ? "" : baseName(ref)
    }
    function take() {
        if (!selected) return
        const n = nameField.text.trim() || baseName(selected)
        const img = Engine.libraryImport(selected, kind, n)
        if (!img) return
        Engine.toast("В мод: " + img, 0)
        lastImported = img
    }
    property string lastImported: ""

    property string pendingSearch: ""        // --shot editor-library <query>
    function applyPending() {
        if (!pendingSearch || Engine.libraryState !== 2) return
        search.text = pendingSearch
        pendingSearch = ""
        refresh()
        if (listing.files.length) select(listing.files[0])
    }
    onPendingSearchChanged: applyPending()
    Connections { target: Engine; function onLibraryChanged() { if (Engine.libraryState === 2) { lb.refresh(); lb.applyPending() } } }
    Timer { id: searchTimer; interval: 250; onTriggered: lb.refresh() }

    background: Rectangle { radius: 16; color: Theme.panel; border.color: Theme.accent }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12

        // ---------------------------------------------------------------- mods
        ColumnLayout {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            spacing: 8
            InkText { text: "Библиотека мастерской"; size: 26; color: Theme.gold }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Картинки всех модов из твоей мастерской БЛ: спрайты, фоны, CG. Бери в свой мод — автор запишется в CREDITS.txt."
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13
            }
            ListView {
                id: mods
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: Engine.libraryState === 2 ? Engine.libraryItems() : []
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    width: mods.width - 10
                    height: 50
                    radius: 8
                    color: lb.itemId === modelData.id ? Theme.panel3 : ma.containsMouse ? Theme.panel2 : "transparent"
                    border.color: lb.itemId === modelData.id ? Theme.accent : "transparent"
                    Column {
                        x: 10; anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 20
                        Text { width: parent.width; text: modelData.title; elide: Text.ElideRight; color: Theme.text; font.family: Theme.ui; font.pixelSize: 14; font.bold: true }
                        Text { text: modelData.count + " картинок · " + modelData.id; color: Theme.faint; font.family: Theme.mono; font.pixelSize: 11 }
                    }
                    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { search.text = ""; lb.openItem(modelData.id, modelData.title) } }
                }
                Column {
                    anchors.centerIn: parent
                    visible: Engine.libraryState !== 2
                    spacing: 8
                    BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: visible }
                    Text { width: 260; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; text: "Собираю картинки мастерской… (десятки тысяч файлов, это разово)"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                }
            }
        }

        // ---------------------------------------------------------------- folder / search results
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: search
                    Layout.fillWidth: true
                    implicitHeight: 34
                    placeholderText: lb.itemId ? "Искать в «" + lb.itemTitle + "»…" : "Искать по всей мастерской: dv, naked, swim, beach…"
                    placeholderTextColor: Theme.faint
                    color: Theme.text
                    font.family: Theme.ui; font.pixelSize: 14
                    selectByMouse: true
                    background: Rectangle { radius: 17; color: Theme.bg; border.color: search.activeFocus ? Theme.accent : Theme.line }
                    onTextEdited: searchTimer.restart()
                }
                Repeater {
                    // label -> query («a|b» = any of them)
                    // the public build has no «18+» shortcut to the unofficial pictures (people look for what they want themselves)
                    model: ([["18+", "naked|nude|hentai|undress|topless|underwear|lingerie|bra_|panties|body_n|голая|голые|нагая"]])
                           .concat([["Купальники", "swim|bikini|купальн"], ["Фоны", "bg/|backgrounds/|фоны/"], ["CG", "cg/"], ["Крупно", "close/"]])
                    PillButton {
                        required property var modelData
                        dark: search.text !== modelData[1]
                        accent: search.text === modelData[1]
                        text: modelData[0]
                        onClicked: { search.text = search.text === modelData[1] ? "" : modelData[1]; lb.refresh() }
                    }
                }
                PillButton { dark: true; text: "Все моды"; onClicked: { lb.itemId = ""; lb.itemTitle = ""; lb.folder = ""; lb.refresh() } }
                PillButton { dark: true; text: "✕"; onClicked: lb.close() }
            }
            // breadcrumb
            Flow {
                Layout.fillWidth: true
                visible: !!lb.itemId && !search.text.trim()
                spacing: 4
                Text {
                    text: lb.itemTitle
                    color: Theme.accent; font.family: Theme.ui; font.pixelSize: 14; font.bold: true
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: lb.up(0) }
                }
                Repeater {
                    model: lb.folder ? lb.folder.split("/") : []
                    Text {
                        required property var modelData
                        required property int index
                        text: " › " + modelData
                        color: Theme.text; font.family: Theme.ui; font.pixelSize: 14
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: lb.up(index + 1) }
                    }
                }
            }
            Text {
                visible: !!search.text.trim()
                text: "Найдено: " + lb.listing.total + (lb.listing.total > lb.listing.files.length ? " (показаны первые " + lb.listing.files.length + ")" : "")
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13
            }
            GridView {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cacheBuffer: 600
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 170)))
                cellHeight: cellWidth + 26
                model: lb.listing.folders.map(f => ({ folder: f.name, count: f.count })).concat(lb.listing.files.map(r => ({ ref: r })))
                delegate: Item {
                    required property var modelData
                    width: grid.cellWidth
                    height: grid.cellHeight
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: 8
                        color: modelData.folder !== undefined ? Theme.panel2 : Theme.bg
                        border.color: lb.selected === modelData.ref ? Theme.gold : ga.containsMouse ? Theme.accent : Theme.line
                        border.width: lb.selected === modelData.ref ? 2 : 1
                        clip: true
                        Image {
                            visible: modelData.ref !== undefined
                            x: 4; y: 4
                            width: parent.width - 8; height: width
                            source: modelData.ref !== undefined ? "image://gb/ws/" + encodeURIComponent(modelData.ref) : ""
                            sourceSize: Qt.size(220, 220)
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                        }
                        Column {
                            visible: modelData.folder !== undefined
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: -10
                            spacing: 4
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "▰"; color: Theme.gold; font.pixelSize: 38 }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: (modelData.count || 0) + " шт."; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 12 }
                        }
                        Text {
                            anchors.bottom: parent.bottom; anchors.bottomMargin: 5
                            x: 6; width: parent.width - 12
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideMiddle
                            text: modelData.folder !== undefined ? modelData.folder : modelData.ref.split("/").pop()
                            color: modelData.folder !== undefined ? Theme.text : Theme.dim
                            font.family: modelData.folder !== undefined ? Theme.ui : Theme.mono
                            font.pixelSize: 12
                            font.bold: modelData.folder !== undefined
                        }
                        MouseArea {
                            id: ga
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { Sfx.click(); if (modelData.folder !== undefined) lb.openFolder(modelData.folder); else lb.select(modelData.ref) }
                            onDoubleClicked: if (modelData.ref !== undefined) { lb.select(modelData.ref); lb.take() }
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: Engine.libraryState === 2 && grid.count === 0
                    text: lb.itemId || search.text.trim() ? "Пусто" : "← Выбери мод слева или ищи по всей мастерской"
                    color: Theme.faint; font.family: Theme.ui; font.pixelSize: 16
                }
            }
        }

        // ---------------------------------------------------------------- the picked picture
        ColumnLayout {
            Layout.preferredWidth: 330
            Layout.fillHeight: true
            spacing: 10
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 420
                radius: 10
                color: "#0a0a0a"
                border.color: Theme.line
                clip: true
                Image {
                    anchors.fill: parent; anchors.margins: 6
                    source: lb.selected ? "image://gb/ws/" + encodeURIComponent(lb.selected) : ""
                    sourceSize: Qt.size(640, 820)
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                }
                Text { anchors.centerIn: parent; visible: !lb.selected; text: "Кликни картинку"; color: Theme.faint; font.family: Theme.ui; font.pixelSize: 15 }
            }
            Text {
                Layout.fillWidth: true
                visible: !!lb.selected
                wrapMode: Text.WrapAnywhere
                text: lb.selected
                color: Theme.faint; font.family: Theme.mono; font.pixelSize: 11
            }
            Text { text: "Взять в мод как"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14; font.bold: true }
            Row {
                spacing: 6
                Repeater {
                    model: [["sprite", "Персонаж"], ["bg", "Фон"], ["cg", "CG"]]
                    PillButton { accent: lb.kind === modelData[0]; dark: !accent; text: modelData[1]; onClicked: lb.kind = modelData[0] }
                }
            }
            TextField {
                id: nameField
                Layout.fillWidth: true
                implicitHeight: 34
                placeholderText: lb.kind === "sprite" ? "имя и эмоция: «вика smile»" : "название"
                placeholderTextColor: Theme.faint
                color: Theme.text
                font.family: Theme.ui; font.pixelSize: 14
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.bg; border.color: nameField.activeFocus ? Theme.accent : Theme.line }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: lb.kind === "sprite" ? "Первое слово — кто (новый персонаж или dv/sl…), дальше — эмоция/одежда. Потом: «показать вика smile»."
                                           : "Потом: «" + (lb.kind === "bg" ? "фон" : "цг") + " <название>»."
                color: Theme.faint; font.family: Theme.ui; font.pixelSize: 12
            }
            PillButton { accent: true; enabled: !!lb.selected; text: "＋ В мой мод"; onClicked: lb.take() }
            PillButton {
                dark: true
                visible: !!lb.lastImported
                text: "Вставить «" + (lb.lastImported.indexOf("bg ") === 0 ? "фон " + lb.lastImported.substring(3) : lb.lastImported.indexOf("cg ") === 0 ? "цг " + lb.lastImported.substring(3) : "показать " + lb.lastImported) + "»"
                onClicked: {
                    const n = lb.lastImported
                    lb.insertLine(n.indexOf("bg ") === 0 ? "фон " + n.substring(3) + " fade" : n.indexOf("cg ") === 0 ? "цг " + n.substring(3) + " dissolve" : "показать " + n + " center dissolve")
                    lb.close()
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
