import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GenryBL

// The fine-tuning window of one palette command (data/forms.json): variants as pills on top,
// every field with its picker, the game frame drawn live by THE renderer and the story line it
// writes. Opened from the palette (a new line) or on an existing line (Ctrl+E) to change it.
Popup {
    id: cf
    property string formId: ""
    property var form: ({})
    property var values: ({})
    property string storyText: ""
    property int storyLine: 1            // the cursor line (new) / the edited line (edit)
    property bool editing: false
    property var names: ({})
    property string built: ""
    property string previewSrc: ""
    property var issues: []
    property var fileCb: null
    signal accepted(string line, bool replace)

    readonly property string by: form.by || ""
    readonly property string variant: by ? String(values[by] === undefined ? "" : values[by]) : ""
    readonly property var byField: (form.fields || []).find(f => f.k === by) || null
    readonly property var shownFields: (form.fields || []).filter(f => f.k !== by && (!f.on || f.on.indexOf(variant) >= 0) &&
                                                                   (!f.needs || String(values[f.needs] === undefined ? "" : values[f.needs]) !== ""))
    readonly property color accentColor: Theme.categoryColor(form.cat || "")

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 50, 1560)
    height: Math.min(parent.height - 40, 920)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openNew(id, preset, text, line) {
        editing = false
        start(id, Engine.formDefaults(id, preset || {}), text, line)
    }
    function openEdit(id, vals, text, line) {
        editing = true
        start(id, vals, text, line)
    }
    function start(id, vals, text, line) {
        formId = id
        form = Engine.commandForm(id)
        values = vals
        storyText = text
        storyLine = line
        names = Engine.storyNames(text)
        rebuild()
        open()
    }
    function setValue(k, v) {
        const o = Object.assign({}, values)
        o[k] = v
        values = o
        rebuild()
    }
    function setVariant(v) {
        const old = variant
        const o = Object.assign({}, values)
        o[by] = v
        // fields whose default depends on the variant follow it, unless the writer changed them
        for (const f of form.fields || []) {
            if (!f.defs) continue
            const oldDef = f.defs[old] !== undefined ? String(f.defs[old]) : String(f.def)
            const newDef = f.defs[v] !== undefined ? String(f.defs[v]) : String(f.def)
            if (o[f.k] === undefined || String(o[f.k]) === oldDef) o[f.k] = newDef
        }
        values = o
        rebuild()
    }
    function rebuild() {
        built = Engine.buildCommand(formId, values)
        timer.restart()
    }
    function storyWith() {               // the story with the built line in place
        const lines = storyText.split("\n")
        const b = built.split("\n")
        if (editing) lines.splice(storyLine - 1, 1, ...b)
        else if ((lines[storyLine - 1] || "").trim() === "") lines.splice(storyLine - 1, 1, ...b)
        else lines.splice(storyLine, 0, ...b)
        return lines.join("\n")
    }
    function lineOfBuilt() {
        return editing || (storyText.split("\n")[storyLine - 1] || "").trim() === "" ? storyLine : storyLine + 1
    }
    function refreshPreview() {
        const at = lineOfBuilt()
        previewSrc = Engine.previewUrl(storyText, at - 1, built)
        const n = built.split("\n").length
        issues = Engine.lint(storyWith()).filter(i => i.line >= at && i.line < at + n)
    }
    function commit() {
        if (!built.trim()) return
        accepted(built, editing)
        close()
    }
    // pickers for FieldInput
    function pickImage(tab, tag, cb) {
        imagePicker.cb = cb
        imagePicker.tab = tab
        const c = Engine.cast.find(x => x.id === tag)
        imagePicker.tag = c ? tag : ""
        imagePicker.tagName = c ? c.name : ""
        imagePicker.open()
    }
    function pickFrom(title, items, cb) { itemPicker.show(title, items, cb) }
    function pickFile(title, filters, cb) {
        fileCb = cb
        fileDialog.title = title
        fileDialog.nameFilters = filters
        fileDialog.open()
    }

    Timer { id: timer; interval: 90; onTriggered: cf.refreshPreview() }
    ImagePicker { id: imagePicker; property var cb: null; onPicked: (img) => { if (cb) cb(img) } }
    ItemPicker { id: itemPicker }
    FileDialog { id: fileDialog; onAccepted: if (cf.fileCb) cf.fileCb(selectedFile) }
    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter"]; enabled: cf.opened; onActivated: cf.commit() }

    background: Rectangle {
        radius: 16
        color: Theme.panel
        border.color: cf.accentColor
        Rectangle { width: parent.width; height: 5; radius: 3; color: cf.accentColor; opacity: 0.8 }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        anchors.topMargin: 20
        spacing: 18

        // ---------------------------------------------------------------- fields
        ColumnLayout {
            Layout.preferredWidth: Math.min(600, cf.width * 0.42)
            Layout.maximumWidth: 620
            Layout.fillHeight: true
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Rectangle { width: 12; height: 12; radius: 6; color: cf.accentColor }
                InkText { text: cf.form.title || ""; size: 30; color: Theme.gold }
                PillButton {
                    visible: !!cf.form.dice
                    dark: true
                    text: "🎲 Удиви меня"
                    onClicked: { cf.values = Engine.diceForm(cf.formId, cf.values); cf.rebuild() }
                }
                Text {
                    visible: cf.editing
                    text: "строка " + cf.storyLine
                    color: Theme.dim
                    font.family: Theme.mono; font.pixelSize: 13
                }
                Item { Layout.fillWidth: true }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: cf.form.help || ""
                color: Theme.dim
                font.family: Theme.ui; font.pixelSize: 14
            }
            // variants: the merged commands of this form
            Flow {
                visible: !!cf.byField
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: cf.byField ? cf.byField.opts : []
                    AbstractButton {
                        required property var modelData
                        readonly property bool on: cf.variant === String(modelData[0])
                        implicitWidth: vt.implicitWidth + 30
                        implicitHeight: 38
                        hoverEnabled: true
                        onClicked: { Sfx.click(); cf.setVariant(String(modelData[0])) }
                        background: Rectangle {
                            radius: 10
                            color: parent.on ? cf.accentColor : parent.hovered ? Theme.panel3 : Theme.panel2
                            border.color: parent.on ? Qt.lighter(cf.accentColor, 1.2) : Theme.line
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                        contentItem: Text {
                            id: vt
                            text: modelData[1]
                            color: parent.on ? "#10150f" : Theme.text
                            font.family: Theme.ui; font.pixelSize: 15; font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line; visible: cf.shownFields.length > 0 }
            Flickable {
                id: fl
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: fieldsCol.implicitHeight + 10
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: fieldsCol
                    width: fl.width - 14
                    spacing: 14
                    Repeater {
                        model: cf.shownFields
                        Loader {
                            required property var modelData
                            Layout.fillWidth: true
                            sourceComponent: modelData.t === "list" ? listEditor : plainEditor
                            property var fieldData: modelData
                        }
                    }
                    Text {
                        visible: cf.shownFields.length === 0
                        text: "Настраивать нечего — жми «Вставить»."
                        color: Theme.faint
                        font.family: Theme.ui; font.pixelSize: 15
                    }
                }
            }
        }

        // ---------------------------------------------------------------- preview + the line
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Rectangle {
                    id: frame
                    readonly property real s: Math.min(parent.width / 1280, parent.height / 720)
                    width: 1280 * s; height: 720 * s
                    anchors.centerIn: parent
                    radius: 10
                    color: "black"
                    clip: true
                    SmoothImage { anchors.fill: parent; source: cf.previewSrc; fade: 120 }
                    Rectangle { anchors.fill: parent; radius: 10; color: "transparent"; border.color: Theme.line }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: lineText.implicitHeight + 22
                radius: 10
                color: Theme.bg
                border.color: cf.issues.some(i => i.level >= 2) ? Theme.bad : cf.issues.length ? Theme.warn : Theme.line
                Text {
                    id: lineText
                    x: 14; y: 11
                    width: parent.width - 28
                    wrapMode: Text.WrapAnywhere
                    text: cf.built
                    color: cf.accentColor
                    font.family: Theme.mono; font.pixelSize: 15
                }
            }
            Repeater {
                model: cf.issues
                Text {
                    required property var modelData
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: (modelData.level >= 2 ? "✖ " : "⚠ ") + modelData.msg
                    color: Theme.levelColor(modelData.level)
                    font.family: Theme.ui; font.pixelSize: 14
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    text: "Ctrl+Enter — " + (cf.editing ? "применить" : "вставить") + "   ·   Esc — отмена"
                    color: Theme.faint
                    font.family: Theme.ui; font.pixelSize: 13
                }
                Item { Layout.fillWidth: true }
                PillButton { dark: true; text: "Отмена"; onClicked: cf.close() }
                PillButton { accent: true; text: cf.editing ? "✓ Применить" : "＋ Вставить в сценарий"; onClicked: cf.commit() }
            }
        }
    }

    Component {
        id: plainEditor
        FieldInput {
            field: parent.fieldData
            value: cf.values[parent.fieldData.k]
            variant: cf.variant
            host: cf
            onEdited: (v) => cf.setValue(parent.fieldData.k, v)
        }
    }
    // a list field: one card per item (buttons of a hub, places on the map, tracks, …)
    Component {
        id: listEditor
        ColumnLayout {
            id: le
            readonly property var fd: parent.fieldData
            readonly property var items: cf.values[fd.k] || []
            spacing: 8
            function setItems(a) { cf.setValue(fd.k, a) }
            function setItem(i, k, v) {
                const a = items.map(x => Object.assign({}, x))
                a[i][k] = v
                setItems(a)
            }
            function move(i, d) {
                const a = items.slice()
                const j = i + d
                if (j < 0 || j >= a.length) return
                const t = a[i]; a[i] = a[j]; a[j] = t
                setItems(a)
            }
            function add() {
                const item = {}
                for (const f of fd.fields) item[f.k] = f.t === "bool" ? false : String(f.def === undefined ? "" : f.def)
                setItems(items.concat([item]))
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: le.fd.label; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14; font.bold: true }
                Text { text: le.items.length ? "· " + le.items.length : ""; color: Theme.faint; font.family: Theme.ui; font.pixelSize: 13 }
                Item { Layout.fillWidth: true }
            }
            Repeater {
                model: le.items.length
                Rectangle {
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: itemCol.implicitHeight + 20
                    radius: 10
                    color: Theme.panel2
                    border.color: Theme.line
                    ColumnLayout {
                        id: itemCol
                        x: 12; y: 10
                        width: parent.width - 24
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: String(index + 1); color: cf.accentColor; font.family: Theme.riffic; font.pixelSize: 18; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Text { text: "↑"; color: index > 0 ? Theme.dim : Theme.faint; font.pixelSize: 16; MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor; onClicked: le.move(index, -1) } }
                            Text { text: "↓"; color: index < le.items.length - 1 ? Theme.dim : Theme.faint; font.pixelSize: 16; MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor; onClicked: le.move(index, 1) } }
                            Text {
                                text: "✕"; color: Theme.bad; font.pixelSize: 15; leftPadding: 8
                                MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor; onClicked: le.setItems(le.items.filter((x, j) => j !== index)) }
                            }
                        }
                        Repeater {
                            model: le.fd.fields
                            FieldInput {
                                required property var modelData
                                compact: true
                                field: modelData
                                value: (le.items[index] || {})[modelData.k]
                                host: cf
                                onEdited: (v) => le.setItem(index, modelData.k, v)
                            }
                        }
                    }
                }
            }
            PillButton {
                dark: true
                visible: !le.fd.max || le.items.length < le.fd.max
                text: "＋ " + (le.fd.add || "Ещё")
                onClicked: le.add()
            }
        }
    }
}
