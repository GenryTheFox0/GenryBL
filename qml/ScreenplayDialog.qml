import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Пиши как сценарий»: paste a screenplay, a chat log or a chapter of prose — it becomes story lines
// (headings -> backgrounds, «Алиса (злая, слева): …» -> the sprite, «— Привет, — улыбнулась Славя» ->
// her line). The play form stays readable in the story; «сразу командами» writes the plain commands.
Popup {
    id: sd
    property string storyText: ""
    property int storyLine: 1
    property bool expand: false
    property var result: ({ lines: [], notes: [] })
    property int current: 0
    property string previewSrc: ""
    signal insertBlock(string block)

    readonly property var outLines: result.lines || []
    readonly property var cheatRows: [
        ["НАТ. ПЛЯЖ — ДЕНЬ", "фон и время суток (ИНТ. — внутри)"],
        ["Алиса (злая, слева): …", "эмоция, место, одежда, близко/вдали"],
        ["Славя (входит справа)", "выход на сцену / (уходит налево)"],
        ["— Привет, — улыбнулась Лена.", "реплика как в книге"],
        ["Семён (думает): …", "мысли героя, (кричит) — крик с тряской"],
        ["Солнце садилось.", "просто текст — рассказчик"],
        ["(Пауза)  ЗАТЕМНЕНИЕ.", "пауза, чёрный экран"]
    ]
    readonly property var notes: result.notes || []

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 50, 1640)
    height: Math.min(parent.height - 40, 920)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openFor(text, line, pasted) {
        storyText = text
        storyLine = line
        src.text = pasted || ""
        current = 0
        refresh()
        open()
        src.forceActiveFocus()
    }
    function refresh() {
        result = Engine.convertScreenplay(src.text, expand)
        current = Math.max(0, Math.min(current, outLines.length - 1))
        previewTimer.restart()
    }
    function fillDemo() {
        src.text = "НАТ. ПЛЯЖ — ДЕНЬ\n\nЖаркий полдень. На песке ни души, только чайки орут над водой.\n\n"
                 + "АЛИСА\n(ухмыляясь, слева)\nНу что, струсил?\n\n"
                 + "— Ещё чего, — буркнул я.\n\n"
                 + "Славя (входит справа, улыбается): Вот вы где! Ольга Дмитриевна всех ищет.\n"
                 + "Алиса (недовольно): Опять эта вожатая...\n"
                 + "Алиса (уходит налево)\n\n"
                 + "ИНТ. СТОЛОВАЯ — ВЕЧЕР\n\n"
                 + "Лена (смущённо, в центре): Можно... я сяду с тобой?\n"
                 + "Семён (думает): Кажется, день налаживается."
        refresh()
    }
    function noteFor(i) { return notes.filter(n => n.line === i + 1) }
    function commit() {
        if (!outLines.length) { Engine.toast("Вставь текст слева — сценарий, переписку или главу книги", 1); return }
        insertBlock(outLines.join("\n"))
        Engine.toast("Вставлено строк: " + outLines.length, 0)
        close()
    }
    function kindColor(line) {
        const k = Engine.screenplayKind(line)
        if (k === "heading") return Theme.gold
        if (k === "say") return Theme.text
        if (k === "direction") return "#8be9fd"
        if (k === "prose") return Theme.dim
        if (/^[^:]{1,40}:/.test(line)) return Theme.text
        return Theme.accent
    }

    Timer {
        id: previewTimer
        interval: 60
        onTriggered: sd.previewSrc = sd.outLines.length ? Engine.previewUrl(sd.storyText, sd.storyLine, sd.outLines.slice(0, sd.current + 1).join("\n")) : ""
    }
    Timer { id: convertTimer; interval: 180; onTriggered: sd.refresh() }
    onCurrentChanged: previewTimer.restart()
    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter"]; enabled: sd.opened; onActivated: sd.commit() }

    background: Rectangle {
        radius: 16
        color: Theme.panel
        border.color: Theme.categoryColor("Диалог")
        Rectangle { width: parent.width; height: 5; radius: 3; color: Theme.categoryColor("Диалог"); opacity: 0.8 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        anchors.topMargin: 20
        spacing: 12

        RowLayout {
            spacing: 12
            Rectangle { width: 12; height: 12; radius: 6; color: Theme.categoryColor("Диалог") }
            InkText { text: "Пиши как сценарий"; size: 30; color: Theme.gold }
            Text {
                Layout.fillWidth: true
                text: "Вставь сценарий, переписку или главу книги — заголовки станут фонами, ремарки в скобках — спрайтами, проза — текстом рассказчика"
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14
                elide: Text.ElideRight
            }
            PillButton { dark: true; text: "Пример"; onClicked: sd.fillDemo() }
            PillButton { dark: true; text: "Очистить"; onClicked: { src.text = ""; sd.refresh() } }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // ------------------------------------------------ the pasted text
            ColumnLayout {
                Layout.preferredWidth: 5
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                Text { text: "ТЕКСТ"; color: Theme.faint; font.family: Theme.ui; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.5 }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    background: Rectangle { radius: 10; color: Theme.bg; border.color: src.activeFocus ? Theme.accent : Theme.line }
                    TextArea {
                        id: src
                        wrapMode: TextEdit.Wrap
                        color: Theme.text
                        selectionColor: Theme.panel3
                        font.family: Theme.ui
                        font.pixelSize: 15
                        placeholderText: "НАТ. ПЛЯЖ — ДЕНЬ\n\nАлиса (злая, слева): Ну и чего ты встал?\n— Иду, — буркнул я.\nСлавя (уходит направо)\n\nСолнце садилось за лес."
                        placeholderTextColor: Theme.faint
                        background: null
                        onTextChanged: convertTimer.restart()
                    }
                }
                // the cheat sheet
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: cheat.implicitHeight + 20
                    radius: 10
                    color: Theme.bg2
                    border.color: Theme.line
                    GridLayout {
                        id: cheat
                        anchors.fill: parent
                        anchors.margins: 10
                        columns: 2
                        columnSpacing: 14
                        rowSpacing: 3
                        Repeater {
                            model: sd.cheatRows
                            delegate: Text {
                                required property var modelData
                                required property int index
                                Layout.column: 0
                                Layout.row: index
                                text: modelData[0]
                                color: Theme.gold; font.family: Theme.mono; font.pixelSize: 13
                            }
                        }
                        Repeater {
                            model: sd.cheatRows
                            delegate: Text {
                                required property var modelData
                                required property int index
                                Layout.column: 1
                                Layout.row: index
                                Layout.fillWidth: true
                                text: modelData[1]
                                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------ what it becomes
            ColumnLayout {
                Layout.preferredWidth: 5
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                RowLayout {
                    Text { text: "В ИСТОРИЮ · " + sd.outLines.length + " стр."; color: Theme.faint; font.family: Theme.ui; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.5 }
                    Item { Layout.fillWidth: true }
                    PillButton {
                        dark: !sd.expand
                        accent: sd.expand
                        text: sd.expand ? "✓ сразу командами" : "сразу командами"
                        onClicked: { sd.expand = !sd.expand; sd.refresh() }
                        ToolTip.visible: hovered
                        ToolTip.text: "Выкл: в истории остаётся читаемый сценарий (он сам развернётся при сборке). Вкл: сразу «показать dv …», «фон …»"
                    }
                }
                ListView {
                    id: outList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: sd.outLines
                    currentIndex: sd.current
                    spacing: 1
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        readonly property var ln: sd.noteFor(index)
                        width: outList.width - 10
                        height: col.implicitHeight + 8
                        radius: 6
                        color: index === sd.current ? Theme.panel3 : rowArea.containsMouse ? Theme.panel2 : "transparent"
                        Column {
                            id: col
                            x: 8; y: 4
                            width: parent.width - 16
                            Text {
                                width: parent.width
                                text: modelData
                                color: sd.kindColor(modelData)
                                font.family: Theme.mono; font.pixelSize: 14
                                wrapMode: Text.Wrap
                            }
                            Repeater {
                                model: ln
                                Text {
                                    required property var modelData
                                    width: col.width
                                    text: (modelData.warn ? "⚠ " : "ℹ ") + modelData.text
                                    color: modelData.warn ? Theme.warn : Theme.faint
                                    font.family: Theme.ui; font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                        MouseArea { id: rowArea; anchors.fill: parent; hoverEnabled: true; onClicked: sd.current = index }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !sd.outLines.length
                        text: "Слева — текст, здесь — строки мода"
                        color: Theme.faint; font.family: Theme.ui; font.pixelSize: 15
                    }
                }
            }

            // ------------------------------------------------ the frame
            ColumnLayout {
                Layout.preferredWidth: 6
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                Text { text: "КАДР · строка " + (sd.current + 1); color: Theme.faint; font.family: Theme.ui; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.5 }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 16
                    radius: 10
                    color: "black"
                    border.color: Theme.line
                    clip: true
                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        source: sd.previewSrc
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        smooth: true
                    }
                }
                RowLayout {
                    spacing: 8
                    PillButton { dark: true; text: "↑"; enabled: sd.current > 0; onClicked: sd.current-- }
                    PillButton { dark: true; text: "↓"; enabled: sd.current < sd.outLines.length - 1; onClicked: sd.current++ }
                    Text {
                        Layout.fillWidth: true
                        text: sd.notes.filter(n => n.warn).length ? "⚠ есть что поправить: " + sd.notes.filter(n => n.warn).length : sd.outLines.length ? "✓ всё понял" : ""
                        color: sd.notes.filter(n => n.warn).length ? Theme.warn : Theme.good
                        font.family: Theme.ui; font.pixelSize: 14; font.bold: true
                    }
                }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 10
                    PillButton { dark: true; text: "Отмена"; onClicked: sd.close() }
                    PillButton { accent: true; text: "＋ Вставить " + sd.outLines.length + " стр."; enabled: sd.outLines.length > 0; onClicked: sd.commit() }
                }
            }
        }
    }
}
