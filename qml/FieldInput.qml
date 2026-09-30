import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// One field of a command form (data/forms.json): label, the right editor for its type and a hint.
// Pictures, music, videos and files open the pickers of the host form (CommandForm).
ColumnLayout {
    id: fe
    property var field: ({})
    property var value
    property string variant: ""
    property var host: null
    property bool compact: false          // inside a list item: smaller label
    signal edited(var value)

    readonly property string type: field.t || "text"
    readonly property string def: field.defs && field.defs[variant] !== undefined ? String(field.defs[variant]) : (field.def === undefined ? "" : String(field.def))
    readonly property string str: value === undefined || value === null ? "" : String(value)
    readonly property var opts: field.opts || []
    readonly property bool optional: def === "" && ["bool", "list", "choice", "effect", "pos", "zone", "chibi"].indexOf(type) < 0
    spacing: 5
    Layout.fillWidth: true

    function set(v) { edited(v) }
    function decimals() {
        const s = String(field.step === undefined ? 0.1 : field.step)
        return s.indexOf(".") < 0 ? 0 : s.length - s.indexOf(".") - 1
    }
    function num(v) {                      // «1.00» at the default stays «1.0» - the default, so the line stays short
        const f = Number(v).toFixed(decimals())
        return Number(f) === Number(def) ? def : f
    }
    function picName(v) {                  // "bg ext_x" / "cg x" / "dv smile pioneer" -> thumbnail url
        if (!v) return ""
        if (v.indexOf("bg ") === 0) return "image://gb/bg/" + encodeURIComponent(v.substring(3))
        if (v.indexOf("cg ") === 0) return "image://gb/cg/" + encodeURIComponent(v.substring(3))
        return "image://gb/face/" + encodeURIComponent(v)
    }

    RowLayout {
        visible: fe.type !== "bool"          // a switch carries its own label
        Layout.fillWidth: true
        spacing: 8
        Text {
            text: qsTr(fe.field.label || "")
            color: Theme.dim
            font.family: Theme.ui; font.pixelSize: fe.compact ? 13 : 14; font.bold: true
        }
        Text {
            visible: fe.optional && !fe.compact
            text: qsTr("необязательно")
            color: Theme.faint
            font.family: Theme.ui; font.pixelSize: 12
        }
        Item { Layout.fillWidth: true }
        Text {
            visible: fe.optional && fe.str !== ""
            text: qsTr("✕ очистить")
            color: Theme.faint
            font.family: Theme.ui; font.pixelSize: 12
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: fe.set("") }
        }
    }

    Loader {
        Layout.fillWidth: true
        sourceComponent: {
            switch (fe.type) {
            case "long": return longEd
            case "num": return numEd
            case "int": return intEd
            case "color": return colorEd
            case "bool": return boolEd
            case "choice": case "effect": case "pos": case "zone": case "chibi": return fe.opts.length <= 9 ? pillsEd : comboEd
            case "sprite": case "bg": case "cg": case "image": return picEd
            case "music": case "sound": case "ambience": case "audiofile": return audioEd
            case "imagefile": return fileEd
            case "video": return videoEd
            case "tag": return tagEd
            case "menubutton": return menuBtnEd
            case "scene": case "speaker": case "var": case "meter": case "item": case "menuaction": return namedEd
            }
            return textEd
        }
    }

    Text {
        visible: !!fe.field.hint && !fe.compact
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: qsTr(fe.field.hint || "")
        color: Theme.faint
        font.family: Theme.ui; font.pixelSize: 12
    }

    // ------------------------------------------------------------------ editors
    component Field: TextField {
        implicitHeight: 34
        color: Theme.text
        placeholderTextColor: Theme.faint
        font.family: Theme.ui; font.pixelSize: 15
        selectByMouse: true
        background: Rectangle { radius: 8; color: Theme.bg; border.color: parent.activeFocus ? Theme.accent : Theme.line }
    }

    Component {
        id: textEd
        Field {
            text: fe.str
            placeholderText: fe.def
            onTextEdited: fe.set(text)
        }
    }
    Component {
        id: longEd
        ScrollView {
            implicitHeight: 84
            TextArea {
                text: fe.str
                wrapMode: TextEdit.Wrap
                color: Theme.text
                placeholderText: fe.def
                placeholderTextColor: Theme.faint
                font.family: Theme.ui; font.pixelSize: 15
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.bg; border.color: parent.activeFocus ? Theme.accent : Theme.line }
                onTextChanged: if (activeFocus && text !== fe.str) fe.set(text)
            }
        }
    }
    Component {
        id: numEd
        RowLayout {
            spacing: 10
            Slider {
                id: sl
                Layout.fillWidth: true
                from: fe.field.min === undefined ? 0 : fe.field.min
                to: fe.field.max === undefined ? 10 : fe.field.max
                stepSize: fe.field.step === undefined ? 0.1 : fe.field.step
                value: Number(fe.str || fe.def)
                onMoved: fe.set(fe.num(value))
            }
            Field {
                Layout.preferredWidth: 74
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.mono
                text: fe.str || fe.def
                validator: RegularExpressionValidator { regularExpression: /-?\d*[.,]?\d*/ }
                onTextEdited: if (text !== "" && text !== "-") fe.set(text.replace(",", "."))
            }
        }
    }
    Component {
        id: intEd
        RowLayout {
            spacing: 6
            PillButton { dark: true; text: "−"; onClicked: fe.set(String(Math.max(fe.field.min === undefined ? -9999 : fe.field.min, Number(fe.str || fe.def) - 1))) }
            Field {
                Layout.preferredWidth: 70
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.mono
                text: fe.str || fe.def
                validator: IntValidator {}
                onTextEdited: if (text !== "" && text !== "-") fe.set(text)
            }
            PillButton { dark: true; text: "+"; onClicked: fe.set(String(Math.min(fe.field.max === undefined ? 9999 : fe.field.max, Number(fe.str || fe.def) + 1))) }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: colorEd
        RowLayout {
            spacing: 6
            Rectangle {
                width: 34; height: 34; radius: 8
                color: /^#[0-9a-fA-F]{3,8}$/.test(fe.str || fe.def) ? (fe.str || fe.def) : "transparent"
                border.color: Theme.line
            }
            Field {
                Layout.preferredWidth: 96
                font.family: Theme.mono
                text: fe.str
                placeholderText: fe.def
                onTextEdited: fe.set(text)
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: ["#ffaa00", "#ffd200", "#b956ff", "#ff3200", "#00deff", "#00ea32", "#ff7a9c", "#ffd27d", "#ffffff", "#9fc6ff", "#143247", "#111827", "#b3001b", "#2b173d"]
                    Rectangle {
                        width: 22; height: 22; radius: 11
                        color: modelData
                        border.color: (fe.str || fe.def).toLowerCase() === modelData ? Theme.text : Theme.line
                        border.width: (fe.str || fe.def).toLowerCase() === modelData ? 2 : 1
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: fe.set(modelData) }
                    }
                }
            }
        }
    }
    Component {
        id: boolEd
        Row {
            PillButton {
                accent: !!fe.value
                dark: !fe.value
                text: (fe.value ? "✓ " : "○ ") + qsTr(fe.field.label || "")
                onClicked: fe.set(!fe.value)
            }
        }
    }
    Component {
        id: pillsEd
        Flow {
            spacing: 6
            Repeater {
                model: fe.opts
                PillButton {
                    required property var modelData
                    accent: fe.str === String(modelData[0]) || (fe.str === "" && fe.def === String(modelData[0]))
                    dark: !accent
                    text: qsTr(modelData[1])
                    onClicked: fe.set(String(modelData[0]))
                }
            }
        }
    }
    Component {
        id: comboEd
        DarkCombo {
            implicitHeight: 34
            model: fe.opts.map(o => qsTr(o[1]))
            currentIndex: Math.max(0, fe.opts.findIndex(o => String(o[0]) === (fe.str || fe.def)))
            onActivated: (i) => fe.set(String(fe.opts[i][0]))
        }
    }
    Component {
        id: picEd
        RowLayout {
            spacing: 10
            readonly property string full: fe.type === "bg" && fe.str ? "bg " + fe.str : fe.type === "cg" && fe.str ? "cg " + fe.str : fe.str
            Rectangle {
                width: fe.type === "sprite" ? 64 : 112
                height: 64
                radius: 8
                color: Theme.bg
                border.color: Theme.line
                clip: true
                Image {
                    anchors.fill: parent
                    anchors.margins: 2
                    source: fe.picName(parent.parent.full)
                    sourceSize: Qt.size(224, 128)
                    asynchronous: true
                    fillMode: fe.type === "sprite" || !/^(bg|cg) /.test(parent.parent.full) ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                }
                Text { anchors.centerIn: parent; visible: !fe.str; text: "—"; color: Theme.faint; font.pixelSize: 20 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: pick() }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Field {
                    Layout.fillWidth: true
                    font.family: Theme.mono; font.pixelSize: 14
                    text: fe.str
                    placeholderText: fe.def || qsTr("не выбрано")
                    onTextEdited: fe.set(text)
                }
                PillButton { dark: true; text: fe.type === "sprite" ? qsTr("Выбрать героя и эмоцию…") : qsTr("Выбрать картинку…"); onClicked: pick() }
            }
            function pick() {
                const tab = fe.type === "sprite" ? 0 : fe.type === "bg" ? 1 : fe.type === "cg" ? 2 : (fe.str.indexOf("cg ") === 0 ? 2 : fe.str.indexOf("bg ") === 0 ? 1 : 0)
                fe.host.pickImage(tab, fe.type === "sprite" ? fe.str.split(" ")[0] : "", (img) => {
                    if (fe.type === "bg" || fe.type === "cg") {
                        if (img.indexOf(fe.type + " ") !== 0) { Engine.toast(fe.type === "bg" ? qsTr("Здесь нужен фон") : qsTr("Здесь нужен CG"), 1); return }
                        fe.set(img.substring(3))
                    } else if (fe.type === "sprite" && (img.indexOf("bg ") === 0 || img.indexOf("cg ") === 0)) {
                        Engine.toast(qsTr("Здесь нужен герой"), 1)
                    } else fe.set(img)
                })
            }
        }
    }
    Component {
        id: audioEd
        RowLayout {
            spacing: 6
            readonly property var list: {
                if (fe.type === "music") return Engine.music.filter(m => !m.custom).map(m => ({ value: m.word, label: m.title ? m.title + "  ·  " + m.word : m.word, audio: m.path }))
                if (fe.type === "sound") return Engine.sounds.filter(m => !m.custom).map(m => ({ value: m.word, label: m.title ? m.title + "  ·  " + m.word : m.word, audio: m.path }))
                if (fe.type === "ambience") return Engine.ambience.map(m => ({ value: m.custom ? m.path : m.word, label: (m.title ? m.title + "  ·  " : "") + m.word + (m.custom ? "  ★" : ""), audio: m.path }))
                return Engine.projectAudio().map(m => ({ value: m.path, label: m.title, audio: m.path }))
            }
            readonly property string audioPath: {
                const v = fe.str || fe.def
                const hit = list.find(i => i.value === v)
                return hit ? hit.audio : (fe.type === "audiofile" ? v : "")
            }
            Rectangle {
                width: 34; height: 34; radius: 17
                readonly property bool playing: parent.audioPath !== "" && Music.previewing === parent.audioPath
                color: playing ? Theme.accent : Theme.panel2
                border.color: Theme.accent
                opacity: parent.audioPath ? 1 : 0.4
                Text { anchors.centerIn: parent; text: parent.playing ? "■" : "▶"; color: parent.playing ? "#16240c" : Theme.text; font.pixelSize: 13 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; enabled: parent.parent.audioPath !== ""; onClicked: Music.preview(parent.parent.audioPath) }
            }
            Field {
                Layout.fillWidth: true
                font.family: Theme.mono; font.pixelSize: 14
                text: fe.str
                placeholderText: fe.def || qsTr("не выбрано")
                onTextEdited: fe.set(text)
            }
            PillButton {
                dark: true
                text: fe.type === "audiofile" ? qsTr("Файлы мода…") : qsTr("Список…")
                onClicked: fe.host.pickFrom(qsTr(fe.field.label), parent.list, (v) => fe.set(v))
            }
            PillButton {
                visible: fe.type === "audiofile" || fe.type === "ambience"
                dark: true
                text: qsTr("＋ Файл")
                onClicked: fe.host.pickFile(qsTr("Звук, музыка или атмосфера (ogg/mp3/wav/flac… — переделаю в ogg)"), [qsTr("Аудио (*.ogg *.mp3 *.wav *.flac *.m4a *.opus)")], (url) => {
                    // the file goes into its own tab of «Звук»: a song stays music, a voice line stays a voice line
                    const form = fe.host ? fe.host.formId : ""
                    const kind = fe.type === "ambience" ? "ambience" : form === "sound" ? "sfx"
                               : form === "say" || form === "phone" ? "voice" : "music"
                    const p = Engine.importAudio(url, kind)
                    if (p) fe.set(p)
                })
            }
        }
    }
    Component {
        id: fileEd
        RowLayout {
            spacing: 6
            Rectangle {
                width: 44; height: 44; radius: 8
                color: Theme.bg
                border.color: Theme.line
                clip: true
                Image {
                    anchors.fill: parent; anchors.margins: 2
                    readonly property var hit: Engine.projectFiles("images").find(f => f.path === fe.str)
                    source: hit ? hit.url : ""
                    sourceSize: Qt.size(88, 88)
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                }
            }
            Field {
                Layout.fillWidth: true
                font.family: Theme.mono; font.pixelSize: 14
                text: fe.str
                placeholderText: fe.def || qsTr("нет")
                onTextEdited: fe.set(text)
            }
            PillButton {
                dark: true
                text: qsTr("Из мода…")
                onClicked: fe.host.pickFrom(qsTr(fe.field.label), Engine.projectFiles("images").map(f => ({ value: f.path, label: f.title, thumb: f.url })), (v) => fe.set(v))
            }
            PillButton {
                dark: true
                text: qsTr("＋ Картинка")
                onClicked: fe.host.pickFile(qsTr("Картинка (png/jpg/webp)"), [qsTr("Картинки (*.png *.jpg *.jpeg *.webp)")], (url) => {
                    const p = Engine.importFile(url, "images")
                    if (p) fe.set(p)
                })
            }
        }
    }
    Component {
        id: videoEd
        RowLayout {
            spacing: 6
            readonly property var list: Engine.videos().map(v => ({ value: v.path, label: v.title, sub: v.es ? qsTr("из игры") : qsTr("свой, ") + v.path }))
            Field {
                Layout.fillWidth: true
                font.family: Theme.mono; font.pixelSize: 14
                text: fe.str
                placeholderText: fe.def
                onTextEdited: fe.set(text)
            }
            PillButton { dark: true; text: qsTr("Список…"); onClicked: fe.host.pickFrom(qsTr("Видео"), parent.list, (v) => fe.set(v)) }
            PillButton {
                dark: true
                text: qsTr("＋ Свой ролик")
                onClicked: fe.host.pickFile(qsTr("Видео (webm/ogv/mp4/mkv…)"), [qsTr("Видео (*.webm *.ogv *.mp4 *.mkv *.mov *.avi)")], (url) => {
                    const p = Engine.importVideo(url)
                    if (p) fe.set(p)
                })
            }
        }
    }
    Component {
        id: tagEd
        Flow {
            spacing: 6
            Repeater {
                model: Engine.cast
                PillButton {
                    required property var modelData
                    accent: (fe.str || fe.def) === modelData.id
                    dark: !accent
                    text: modelData.name
                    onClicked: fe.set(modelData.id)
                }
            }
        }
    }
    Component {
        id: menuBtnEd
        ColumnLayout {
            spacing: 4
            Field {
                Layout.fillWidth: true
                text: fe.str
                placeholderText: fe.def
                onTextEdited: fe.set(text)
            }
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: fe.opts
                    Text {
                        required property var modelData
                        text: modelData[0]
                        color: fe.str === modelData[0] ? Theme.accent : Theme.faint
                        font.family: Theme.ui; font.pixelSize: 12
                        rightPadding: 8
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: fe.set(modelData[0]) }
                    }
                }
            }
        }
    }
    Component {
        id: namedEd
        RowLayout {
            spacing: 6
            readonly property var list: {
                const n = fe.host ? fe.host.names : {}
                if (fe.type === "scene") return (n.scenes || []).map(s => ({ value: s, label: s }))
                // a menu button: what it does, whatever its caption says («Выселение» -> выход)
                if (fe.type === "menuaction")
                    return [{ value: "start", label: qsTr("Начать игру") }, { value: "загрузить", label: qsTr("Продолжить (загрузка)") },
                            { value: "главы", label: qsTr("Главы") }, { value: "галерея", label: qsTr("Галерея") },
                            { value: "достижения", label: qsTr("Достижения") }, { value: "отношения", label: qsTr("Отношения") },
                            { value: "настройки", label: qsTr("Настройки") }, { value: "имя", label: qsTr("Имя игрока") },
                            { value: "статистика", label: qsTr("Статистика") }, { value: "стример", label: qsTr("Режим стримера") },
                            { value: "выход", label: qsTr("Выход") }]
                           .concat((n.scenes || []).map(s => ({ value: s, label: "→ " + s })))
                if (fe.type === "speaker") return Engine.storySpeakers(fe.host ? fe.host.storyText : "").map(s => ({ value: s.name, label: s.name, color: s.color }))
                const l = fe.type === "meter" ? n.meters : fe.type === "item" ? n.items : n.vars
                return (l || []).map(s => ({ value: s, label: s }))
            }
            Field {
                Layout.fillWidth: true
                text: fe.str
                placeholderText: fe.def || qsTr("не задано")
                onTextEdited: fe.set(text)
            }
            PillButton {
                dark: true
                text: "▾"
                enabled: parent.list.length > 0
                onClicked: fe.host.pickFrom(qsTr(fe.field.label), parent.list, (v) => fe.set(v))
            }
            Text {
                visible: (fe.type === "scene" || fe.type === "menuaction") && fe.str !== "" && parent.list.findIndex(i => i.value === fe.str) < 0
                text: qsTr("новая")
                color: Theme.warn
                font.family: Theme.ui; font.pixelSize: 12
            }
        }
    }
}
