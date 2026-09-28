import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Карта лагеря» as the game draws it (control/mapclass.rpyc): click a place to open it, pick
// the scene it leads to and who waits there (the chibi). Writes «карта zone: scene @chibi, …».
Popup {
    id: me
    property var zones: []               // [{zone, scene, chibi}] - the open places, in order
    property string storyText: ""
    property int storyLine: 1
    property bool editing: false
    property var scenes: []
    property string selId: ""
    property string hoverId: ""
    signal accepted(string line, bool replace)
    readonly property var allZones: Engine.mapZones()
    readonly property var chibiList: Engine.chibis()
    readonly property string built: Engine.buildCommand("map", { zones: zones })

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 50, 1600)
    height: Math.min(parent.height - 40, 900)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openNew(text, line) {
        editing = false
        start(Engine.formDefaults("map").zones, text, line)
    }
    function openEdit(values, text, line) {
        editing = true
        start(values.zones || [], text, line)
    }
    function start(z, text, line) {
        zones = z.map(x => ({ zone: String(x.zone), scene: String(x.scene || ""), chibi: String(x.chibi || "") }))
        storyText = text
        storyLine = line
        scenes = Engine.sceneNames(text)
        selId = zones.length ? zones[0].zone : ""
        open()
    }
    function entry(id) { return zones.find(z => z.zone === id) }
    function titleOf(id) { const z = allZones.find(x => x.id === id); return z ? z.title : id }
    function toggle(id) {
        if (entry(id)) {
            zones = zones.filter(z => z.zone !== id)
            if (selId === id) selId = zones.length ? zones[zones.length - 1].zone : ""
        } else {
            zones = zones.concat([{ zone: id, scene: id, chibi: "" }])
            selId = id
        }
    }
    function setField(id, k, v) {
        zones = zones.map(z => z.zone === id ? Object.assign({}, z, { [k]: v }) : z)
    }
    function commit() {
        if (!zones.length) { Engine.toast("Открой хотя бы одно место на карте", 1); return }
        accepted(built, editing)
        close()
    }

    ItemPicker { id: picker }
    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter"]; enabled: me.opened; onActivated: me.commit() }

    background: Rectangle {
        radius: 16
        color: Theme.panel
        border.color: Theme.categoryColor("Сцены")
        Rectangle { width: parent.width; height: 5; radius: 3; color: Theme.categoryColor("Сцены"); opacity: 0.8 }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        anchors.topMargin: 20
        spacing: 18

        // ---------------------------------------------------------------- the map itself
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Rectangle {
                id: canvas
                readonly property real s: Math.min(parent.width / 1920, parent.height / 1080)
                width: 1920 * s; height: 1080 * s
                anchors.centerIn: parent
                color: "black"
                radius: 8
                clip: true
                Image {
                    anchors.fill: parent
                    source: "image://gb/file/images/maps/map.jpg"
                    sourceSize: Qt.size(1920, 1080)
                    asynchronous: true
                    smooth: true
                }
                Repeater {
                    model: me.allZones
                    Item {
                        required property var modelData
                        readonly property var ent: me.entry(modelData.id)
                        readonly property bool isOpen: !!ent
                        readonly property bool lit: area.containsMouse || me.selId === modelData.id
                        x: modelData.x1 * canvas.s
                        y: modelData.y1 * canvas.s
                        width: (modelData.x2 - modelData.x1) * canvas.s
                        height: (modelData.y2 - modelData.y1) * canvas.s
                        clip: true
                        // the open place: the same spot of map_available.jpg (map_selected.jpg under the mouse)
                        Image {
                            visible: parent.isOpen
                            x: -parent.x; y: -parent.y
                            width: canvas.width; height: canvas.height
                            source: parent.lit ? "image://gb/file/images/maps/map_selected.jpg" : "image://gb/file/images/maps/map_available.jpg"
                            sourceSize: Qt.size(1920, 1080)
                            asynchronous: true
                            smooth: true
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: parent.isOpen ? "transparent" : (area.containsMouse ? "#33ffffff" : "transparent")
                            border.color: me.selId === modelData.id ? Theme.gold : area.containsMouse ? "#aaffffff" : "transparent"
                            border.width: 2
                        }
                        Image {
                            visible: parent.isOpen && !!parent.ent.chibi
                            source: parent.isOpen && parent.ent.chibi ? ((me.chibiList.find(c => c.id === parent.ent.chibi) || {}).icon || "") : ""
                            width: sourceSize.width * canvas.s * 1.0
                            height: sourceSize.height * canvas.s * 1.0
                            asynchronous: true
                            SequentialAnimation on opacity {           // ES: anim.Blink
                                running: me.opened
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.35; duration: 700 }
                                NumberAnimation { to: 1.0; duration: 700 }
                            }
                        }
                        Rectangle {
                            visible: area.containsMouse
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                            width: tl.implicitWidth + 16; height: tl.implicitHeight + 8; radius: 6
                            color: "#d0101410"
                            Text {
                                id: tl
                                anchors.centerIn: parent
                                text: modelData.title + (parent.parent.isOpen ? " → " + parent.parent.ent.scene : "  (закрыто)")
                                color: Theme.text; font.family: Theme.ui; font.pixelSize: 13; font.bold: true
                            }
                        }
                        MouseArea {
                            id: area
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Sfx.click()
                                if (parent.isOpen && me.selId !== modelData.id) me.selId = modelData.id
                                else me.toggle(modelData.id)
                            }
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------------------- open places
        ColumnLayout {
            Layout.preferredWidth: 400
            Layout.maximumWidth: 420
            Layout.fillHeight: true
            spacing: 10
            RowLayout {
                spacing: 10
                Rectangle { width: 12; height: 12; radius: 6; color: Theme.categoryColor("Сцены") }
                InkText { text: "Карта лагеря"; size: 30; color: Theme.gold }
                Text { visible: me.editing; text: "строка " + me.storyLine; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 13 }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Клик по месту — открыть его. Клик по открытому — выбрать, ещё клик — закрыть. Игрок сам выберет, куда идти."
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14
            }
            RowLayout {
                spacing: 6
                PillButton { dark: true; text: "Открыть все"; onClicked: me.zones = me.allZones.map(z => (me.entry(z.id) || { zone: z.id, scene: z.id, chibi: "" })) }
                PillButton { dark: true; text: "Закрыть все"; onClicked: { me.zones = []; me.selId = "" } }
            }
            Flickable {
                id: fl
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: col.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: col
                    width: fl.width - 12
                    spacing: 8
                    Repeater {
                        model: me.zones
                        Rectangle {
                            required property var modelData
                            readonly property bool sel: me.selId === modelData.zone
                            Layout.fillWidth: true
                            implicitHeight: zc.implicitHeight + 18
                            radius: 10
                            color: sel ? Theme.panel3 : Theme.panel2
                            border.color: sel ? Theme.gold : Theme.line
                            MouseArea { anchors.fill: parent; onClicked: me.selId = modelData.zone }
                            ColumnLayout {
                                id: zc
                                x: 12; y: 9
                                width: parent.width - 24
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: me.titleOf(modelData.zone); color: Theme.text; font.family: Theme.ui; font.pixelSize: 16; font.bold: true }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: "✕"; color: Theme.bad; font.pixelSize: 15
                                        MouseArea { anchors.fill: parent; anchors.margins: -5; cursorShape: Qt.PointingHandCursor; onClicked: me.toggle(modelData.zone) }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "→"; color: Theme.dim; font.pixelSize: 15 }
                                    TextField {
                                        Layout.fillWidth: true
                                        implicitHeight: 30
                                        text: modelData.scene
                                        placeholderText: "сцена"
                                        placeholderTextColor: Theme.faint
                                        color: Theme.text
                                        font.family: Theme.ui; font.pixelSize: 14
                                        selectByMouse: true
                                        background: Rectangle { radius: 7; color: Theme.bg; border.color: parent.activeFocus ? Theme.accent : Theme.line }
                                        onTextEdited: me.setField(modelData.zone, "scene", text)
                                    }
                                    PillButton {
                                        dark: true
                                        text: "▾"
                                        enabled: me.scenes.length > 0
                                        onClicked: picker.show("Сцена для «" + me.titleOf(modelData.zone) + "»", me.scenes.map(s => ({ value: s, label: s })),
                                                               (v) => me.setField(modelData.zone, "scene", v))
                                    }
                                    Text {
                                        visible: modelData.scene !== "" && me.scenes.indexOf(modelData.scene) < 0
                                        text: "новая"; color: Theme.warn; font.family: Theme.ui; font.pixelSize: 12
                                    }
                                }
                                Flow {
                                    id: chibiRow
                                    readonly property var zoneEnt: modelData
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Rectangle {
                                        width: 34; height: 34; radius: 6
                                        color: modelData.chibi === "" ? Theme.accent : Theme.bg
                                        border.color: Theme.line
                                        Text { anchors.centerIn: parent; text: "—"; color: modelData.chibi === "" ? "#16240c" : Theme.dim; font.pixelSize: 14 }
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: me.setField(modelData.zone, "chibi", "") }
                                        ToolTip.visible: false
                                    }
                                    Repeater {
                                        model: me.chibiList
                                        Rectangle {
                                            required property var modelData
                                            readonly property bool on: chibiRow.zoneEnt.chibi === modelData.id
                                            width: 34; height: 34; radius: 6
                                            color: on ? Theme.accent : Theme.bg
                                            border.color: on ? Theme.gold : Theme.line
                                            Image {
                                                anchors.fill: parent; anchors.margins: 2
                                                source: modelData.icon
                                                sourceSize: Qt.size(64, 64)
                                                fillMode: Image.PreserveAspectFit
                                                asynchronous: true
                                            }
                                            MouseArea {
                                                id: ca
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: me.setField(chibiRow.zoneEnt.zone, "chibi", modelData.id)
                                            }
                                            ToolTip.visible: ca.containsMouse
                                            ToolTip.text: modelData.name
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        visible: me.zones.length === 0
                        text: "Все места закрыты — кликни по карте."
                        color: Theme.faint; font.family: Theme.ui; font.pixelSize: 15
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: lt.implicitHeight + 18
                radius: 10
                color: Theme.bg
                border.color: Theme.line
                Text {
                    id: lt
                    x: 12; y: 9
                    width: parent.width - 24
                    wrapMode: Text.WrapAnywhere
                    text: me.built
                    color: Theme.categoryColor("Сцены")
                    font.family: Theme.mono; font.pixelSize: 13
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PillButton { dark: true; text: "Отмена"; onClicked: me.close() }
                PillButton { accent: true; text: me.editing ? "✓ Применить" : "＋ Вставить"; onClicked: me.commit() }
            }
        }
    }
}
