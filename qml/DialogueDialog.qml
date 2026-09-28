import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GenryBL

// «Диалог + озвучка»: the old constructor's three voice wizards in one window.
// A table of lines (who / what / voice), audio dropped or picked in bulk fills the rows
// in order (the old «серия реплик»), and the right side is the real game frame of the
// selected line - drawn by THE renderer over the scene under the editor's cursor.
Popup {
    id: dd
    property string storyText: ""
    property int storyLine: 1
    property var speakers: []            // [{name, color}]
    property int current: 0
    property string previewSrc: ""
    signal insertBlock(string block)

    readonly property string narrator: "Текст"
    readonly property var audioExt: ["ogg", "mp3", "wav", "flac", "m4a", "opus", "aac", "wma"]

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 1560)
    height: Math.min(parent.height - 50, 920)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    onClosed: if (Music.previewing) Music.stopPreview()

    ListModel { id: rows }

    function openFor(text, line) {
        storyText = text
        storyLine = line
        speakers = Engine.storySpeakers(text)
        rows.clear()
        rows.append({ who: speakers.length ? speakers[0].name : "Алиса", text: "", voice: "" })
        current = 0
        open()
        refresh()
        Qt.callLater(() => { const it = list.itemAtIndex(0); if (it) it.focusText() })
    }
    function fillDemo() {             // --shot editor-dialogue
        rows.clear()
        rows.append({ who: "Алиса", text: "Ну и чего ты тут стоишь?", voice: "audio/alisa_01.ogg" })
        rows.append({ who: "Семён", text: "Жду, пока ты опять что-нибудь придумаешь.", voice: "" })
        rows.append({ who: "Алиса", text: "Дождёшься!", voice: "audio/alisa_02.ogg" })
        rows.append({ who: narrator, text: "Она фыркнула и отвернулась.", voice: "" })
        Qt.callLater(() => { current = 2; refresh() })     // after openFor's focus on row 1
    }
    function speakerNames() {
        const n = speakers.map(s => s.name)
        n.push(narrator)
        return n
    }
    function colorOf(name) {
        if (name === narrator) return Theme.dim
        for (const s of speakers) if (s.name === name) return s.color
        return "#e8e8e8"
    }
    function linesFor(r, preview) {
        let t = r.text.trim()
        if (!t) { if (!preview) return []; t = "…" }
        if (r.who === narrator) return r.voice ? ["озвучкафайл " + r.voice, "текст " + t] : ["текст " + t]
        const who = r.who.trim() || "Семён"
        return r.voice ? ["озвученнаяреплика " + who + " | " + r.voice + " | " + t] : [who + ": " + t]
    }
    function block(upto, preview) {
        let out = []
        const n = upto === undefined ? rows.count : Math.min(rows.count, upto + 1)
        for (let i = 0; i < n; ++i) out = out.concat(linesFor(rows.get(i), preview && i === n - 1))
        return out
    }
    function refresh() { previewTimer.restart() }
    function nextSpeaker() {
        // a dialogue alternates: the new line goes to whoever spoke before the last speaker
        if (!rows.count) return speakers.length ? speakers[0].name : "Алиса"
        const last = rows.get(rows.count - 1).who
        for (let i = rows.count - 2; i >= 0; --i) if (rows.get(i).who !== last) return rows.get(i).who
        const other = speakers.find(s => s.name !== last)
        return other ? other.name : last
    }
    function addRow(focus) {
        rows.append({ who: nextSpeaker(), text: "", voice: "" })
        current = rows.count - 1
        refresh()
        if (focus) Qt.callLater(() => { list.positionViewAtEnd(); const it = list.itemAtIndex(dd.current); if (it) it.focusText() })
    }
    function assignVoices(rels) {
        // from the selected row down; files past the end become new lines of the same speaker
        const who = rows.count ? rows.get(current).who : nextSpeaker()
        let i = current
        for (const rel of rels) {
            if (i >= rows.count) rows.append({ who: who, text: "", voice: "" })
            rows.setProperty(i, "voice", rel)
            ++i
        }
        refresh()
    }
    function importUrls(urls) {
        const audio = []
        for (const u of urls) {
            const s = u.toString()
            const ext = s.substring(s.lastIndexOf(".") + 1).toLowerCase()
            if (audioExt.indexOf(ext) >= 0) audio.push(s)
        }
        if (!audio.length) { Engine.toast("Это не аудио: ogg, mp3, wav, flac, m4a, opus", 1); return }
        const rels = Engine.importAudioFiles(audio)
        if (rels.length) assignVoices(rels)
    }
    function missingText() {
        const bad = []
        for (let i = 0; i < rows.count; ++i) if (rows.get(i).voice && !rows.get(i).text.trim()) bad.push(i + 1)
        return bad
    }
    function commit() {
        const bad = missingText()
        if (bad.length) { Engine.toast("У строк " + bad.join(", ") + " есть озвучка, но нет текста", 2); return }
        const lines = block()
        if (!lines.length) { Engine.toast("Нет ни одной реплики", 1); return }
        insertBlock(lines.join("\n"))
        close()
    }

    Timer {
        id: previewTimer
        interval: 60
        onTriggered: dd.previewSrc = Engine.previewUrl(dd.storyText, dd.storyLine, dd.block(dd.current, true).join("\n"))
    }

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }

    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (d) => { if (d.hasUrls) dd.importUrls(d.urls) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ---- header
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            InkText { text: "Диалог + озвучка"; size: 28; color: Theme.gold }
            Text {
                Layout.fillWidth: true
                text: "Enter — следующая реплика  ·  аудио можно перетащить прямо сюда: файлы лягут по строкам по порядку"
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14
                elide: Text.ElideRight
            }
            PillButton { dark: true; text: "✕"; onClicked: dd.close() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // ---- the lines
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: rows
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property string who
                        required property string text
                        required property string voice
                        function focusText() { lineText.forceActiveFocus() }
                        width: list.width - 12
                        height: 94
                        radius: 10
                        color: dd.current === index ? Theme.panel3 : Theme.panel2
                        border.color: dd.current === index ? Theme.accent : Theme.line
                        MouseArea { anchors.fill: parent; onClicked: { dd.current = row.index; dd.refresh() } }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            anchors.leftMargin: 10
                            spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: row.index + 1
                                color: Theme.faint; font.family: Theme.mono; font.pixelSize: 13
                                Layout.preferredWidth: 22
                            }
                            Rectangle { width: 10; height: 10; radius: 5; color: dd.colorOf(row.who) }
                            ComboBox {
                                id: whoBox
                                Layout.preferredWidth: 190
                                implicitHeight: 34
                                editable: true
                                model: dd.speakerNames()
                                Component.onCompleted: editText = row.who
                                onActivated: commitWho(currentText)
                                onAccepted: commitWho(editText)
                                onActiveFocusChanged: if (!activeFocus && editText !== row.who) commitWho(editText)
                                function commitWho(t) {
                                    const v = t.trim()
                                    if (!v) { editText = row.who; return }
                                    rows.setProperty(row.index, "who", v)
                                    editText = v
                                    dd.current = row.index
                                    dd.refresh()
                                }
                                contentItem: TextField {
                                    text: whoBox.editText
                                    onTextEdited: whoBox.editText = text
                                    onAccepted: whoBox.commitWho(text)
                                    color: dd.colorOf(row.who)
                                    font.family: Theme.ui; font.pixelSize: 15; font.bold: true
                                    leftPadding: 12; rightPadding: 22
                                    selectByMouse: true
                                    background: null
                                }
                                background: Rectangle { radius: 17; color: Theme.bg; border.color: whoBox.activeFocus ? Theme.accent : Theme.line }
                                indicator: Text { x: whoBox.width - width - 10; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: Theme.dim; font.pixelSize: 12 }
                                popup: Popup {
                                    y: whoBox.height + 4
                                    width: whoBox.width
                                    implicitHeight: Math.min(360, contentItem.implicitHeight + 8)
                                    padding: 4
                                    contentItem: ListView {
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: whoBox.popup.visible ? whoBox.delegateModel : null
                                        currentIndex: whoBox.highlightedIndex
                                        ScrollBar.vertical: ScrollBar {}
                                    }
                                    background: Rectangle { color: Theme.panel2; radius: 10; border.color: Theme.accent }
                                }
                                delegate: ItemDelegate {
                                    required property int index
                                    required property string modelData
                                    width: whoBox.width - 8
                                    height: 30
                                    highlighted: whoBox.highlightedIndex === index
                                    contentItem: Text { text: modelData; color: dd.colorOf(modelData); font.family: Theme.ui; font.pixelSize: 14; font.bold: true; verticalAlignment: Text.AlignVCenter }
                                    background: Rectangle { color: parent.highlighted ? Theme.panel3 : "transparent"; radius: 6 }
                                }
                            }
                            Item { Layout.fillWidth: true }
                            // voice
                            Rectangle {
                                Layout.preferredWidth: 280
                                implicitHeight: 34
                                radius: 17
                                color: row.voice ? "#2a9bd35a" : Theme.bg
                                border.color: row.voice ? Theme.accent : Theme.line
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 4
                                    anchors.rightMargin: 6
                                    spacing: 4
                                    AbstractButton {
                                        implicitWidth: 28; implicitHeight: 28
                                        visible: !!row.voice
                                        readonly property bool playing: Music.previewing === row.voice
                                        onClicked: Music.preview(row.voice)
                                        background: Rectangle { radius: 14; color: parent.playing ? Theme.accent : Theme.panel3 }
                                        contentItem: Text { text: parent.playing ? "■" : "▶"; color: parent.playing ? "#16240c" : Theme.text; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        leftPadding: row.voice ? 0 : 8
                                        text: row.voice ? "♪ " + row.voice.substring(6) : "без озвучки"
                                        color: row.voice ? Theme.text : Theme.faint
                                        font.family: Theme.mono; font.pixelSize: 12
                                        elide: Text.ElideMiddle
                                    }
                                    AbstractButton {
                                        implicitWidth: 28; implicitHeight: 28
                                        onClicked: { dd.current = row.index; row.voice ? rows.setProperty(row.index, "voice", "") : oneDialog.open(); dd.refresh() }
                                        background: Rectangle { radius: 14; color: parent.hovered ? Theme.panel3 : "transparent" }
                                        contentItem: Text { text: row.voice ? "✕" : "＋"; color: Theme.dim; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        ToolTip.visible: hovered
                                        ToolTip.text: row.voice ? "Убрать озвучку" : "Выбрать файл озвучки"
                                    }
                                }
                            }
                            AbstractButton {
                                implicitWidth: 30; implicitHeight: 30
                                enabled: rows.count > 1
                                opacity: enabled ? 1 : 0.3
                                onClicked: { rows.remove(row.index); dd.current = Math.min(dd.current, rows.count - 1); dd.refresh() }
                                background: Rectangle { radius: 15; color: parent.hovered ? "#44ff6b6b" : "transparent" }
                                contentItem: Text { text: "✖"; color: Theme.dim; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                ToolTip.visible: hovered
                                ToolTip.text: "Удалить строку"
                            }
                        }
                            TextField {
                                id: lineText
                                Layout.fillWidth: true
                                Layout.leftMargin: 30
                                implicitHeight: 36
                                text: row.text
                                placeholderText: row.who === dd.narrator ? "Текст рассказчика…" : "Что говорит " + row.who + "…"
                                placeholderTextColor: Theme.faint
                                color: Theme.text
                                font.family: Theme.ui; font.pixelSize: 15
                                selectByMouse: true
                                onTextEdited: { rows.setProperty(row.index, "text", text); dd.current = row.index; dd.refresh() }
                                onActiveFocusChanged: if (activeFocus && dd.current !== row.index) { dd.current = row.index; dd.refresh() }
                                function advance() {
                                    if (row.index === rows.count - 1) { dd.addRow(true); return }
                                    dd.current = row.index + 1
                                    dd.refresh()
                                    const it = list.itemAtIndex(dd.current)
                                    if (it) it.focusText()
                                }
                                Keys.onReturnPressed: advance()
                                Keys.onEnterPressed: advance()
                                background: Rectangle { radius: 8; color: Theme.bg; border.color: lineText.activeFocus ? Theme.accent : Theme.line }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    PillButton { dark: true; text: "＋ Реплика"; onClicked: dd.addRow(true) }
                    PillButton { dark: true; text: "♪ Озвучка пачкой…"; enabled: !Engine.busy; onClicked: bulkDialog.open() }
                    Text {
                        Layout.fillWidth: true
                        text: Engine.busy ? Engine.busyText : "файлы лягут начиная со строки " + (dd.current + 1)
                        color: Engine.busy ? Theme.warn : Theme.faint
                        font.family: Theme.ui; font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }
            }

            // ---- what the player sees + the code that goes into the story
            ColumnLayout {
                Layout.preferredWidth: Math.min(680, dd.width * 0.42)
                Layout.fillHeight: true
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 16
                    color: "black"
                    radius: 6
                    clip: true
                    SmoothImage { anchors.fill: parent; source: dd.previewSrc; fade: 120 }
                }
                Text { text: "Строка " + (dd.current + 1) + " — так её увидит игрок"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: Theme.bg
                    border.color: Theme.line
                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 6
                        TextArea {
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            text: { rows.count; dd.previewSrc; return dd.block().join("\n") }
                            color: "#d8d8e4"
                            font.family: Theme.mono; font.pixelSize: 13
                            background: null
                        }
                    }
                }
            }
        }

        // ---- footer
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Item { Layout.fillWidth: true }
            PillButton { dark: true; text: "Отмена"; onClicked: dd.close() }
            PillButton {
                accent: true
                text: { dd.previewSrc; return "Вставить в сцену  (" + dd.block().length + ")" }
                enabled: !Engine.busy
                onClicked: dd.commit()
            }
        }
    }

    // drag-over hint
    Rectangle {
        anchors.fill: parent
        radius: 14
        visible: drop.containsDrag
        color: "#cc0d1310"
        border.color: Theme.accent
        border.width: 3
        Text {
            anchors.centerIn: parent
            text: "Отпусти — озвучка ляжет со строки " + (dd.current + 1) + " по порядку"
            color: Theme.accent; font.family: Theme.ui; font.pixelSize: 24; font.bold: true
        }
    }

    FileDialog {
        id: oneDialog
        title: "Озвучка строки"
        nameFilters: ["Аудио (*.ogg *.mp3 *.wav *.flac *.m4a *.opus *.aac *.wma)"]
        onAccepted: dd.importUrls([selectedFile])
    }
    FileDialog {
        id: bulkDialog
        title: "Озвучка пачкой — по порядку строк"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Аудио (*.ogg *.mp3 *.wav *.flac *.m4a *.opus *.aac *.wma)"]
        onAccepted: {
            const files = selectedFiles.slice().sort((a, b) => a.toString().localeCompare(b.toString(), undefined, { numeric: true }))
            dd.importUrls(files)
        }
    }
}
