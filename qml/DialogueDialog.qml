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

    readonly property string narrator: qsTr("Текст")
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
        rows.append({ who: speakers.length ? speakers[0].name : "Алиса", text: "", voice: "", tag: "", look: "", pos: "" })
        current = 0
        open()
        refresh()
        Qt.callLater(() => { const it = list.itemAtIndex(0); if (it) it.focusText() })
    }
    function fillDemo() {             // --shot editor-dialogue
        rows.clear()
        rows.append({ who: "Алиса", text: "Ну и чего ты тут стоишь?", voice: "audio/alisa_01.ogg", tag: "dv", look: "grin pioneer", pos: "left" })
        rows.append({ who: "Семён", text: "Жду, пока ты опять что-нибудь придумаешь.", voice: "", tag: "", look: "", pos: "" })
        rows.append({ who: "Алиса", text: "Дождёшься!", voice: "audio/alisa_02.ogg", tag: "dv", look: "angry pioneer", pos: "left" })
        rows.append({ who: narrator, text: "Она фыркнула и отвернулась.", voice: "", tag: "", look: "", pos: "" })
        Qt.callLater(() => { current = 2; refresh() })     // after openFor's focus on row 1
    }
    function speakerNames() {
        const n = speakers.map(s => s.name)
        n.push(narrator)
        return n
    }
    // the heroine (sprite tag) a name stands for: «Алиса» -> dv; a name of your own -> "" (pick a face in «Спрайт»)
    function tagOf(name) {
        const low = (name || "").trim().toLowerCase()
        for (const c of Engine.cast) if (c.name.toLowerCase() === low || c.id === low) return c.id
        return ""
    }
    function castOf(tag) {
        for (const c of Engine.cast) if (c.id === tag) return c
        return null
    }
    readonly property var places: [["left", qsTr("слева")], ["cleft", qsTr("левее центра")], ["center", qsTr("в центре")], ["cright", qsTr("правее центра")], ["right", qsTr("справа")]]
    function placeName(p) {
        for (const x of places) if (x[0] === p) return x[1]
        return p
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
        const shown = ({})                      // tag -> "look|pos" already on screen in this block
        const n = upto === undefined ? rows.count : Math.min(rows.count, upto + 1)
        for (let i = 0; i < n; ++i) {
            const r = rows.get(i)
            if (r.tag && r.look) {
                const key = r.look + "|" + (r.pos || "")
                if (shown[r.tag] !== key) {
                    out.push("показать " + r.tag + " " + r.look + (r.pos ? " " + r.pos : ""))
                    shown[r.tag] = key
                }
            }
            out = out.concat(linesFor(r, preview && i === n - 1))
        }
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
        rows.append({ who: nextSpeaker(), text: "", voice: "", tag: "", look: "", pos: "" })
        current = rows.count - 1
        refresh()
        if (focus) Qt.callLater(() => { list.positionViewAtEnd(); const it = list.itemAtIndex(dd.current); if (it) it.focusText() })
    }
    function assignVoices(rels) {
        // from the selected row down; files past the end become new lines of the same speaker
        const who = rows.count ? rows.get(current).who : nextSpeaker()
        let i = current
        for (const rel of rels) {
            if (i >= rows.count) rows.append({ who: who, text: "", voice: "", tag: "", look: "", pos: "" })
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
        if (!audio.length) { Engine.toast(qsTr("Это не аудио: ogg, mp3, wav, flac, m4a, opus"), 1); return }
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
        if (bad.length) { Engine.toast(qsTr("У строк ") + bad.join(", ") + qsTr(" есть озвучка, но нет текста"), 2); return }
        const lines = block()
        if (!lines.length) { Engine.toast(qsTr("Нет ни одной реплики"), 1); return }
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
            InkText { text: qsTr("Диалог + озвучка"); size: 28; color: Theme.gold }
            Text {
                Layout.fillWidth: true
                text: qsTr("Enter — следующая реплика  ·  аудио можно перетащить прямо сюда: файлы лягут по строкам по порядку")
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
                        required property string tag
                        required property string look
                        required property string pos
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
                            // who speaks: click = the whole list at once, typing filters it, Enter takes the first match
                            // or keeps a name of your own
                            TextField {
                                id: whoField
                                Layout.fillWidth: true
                                Layout.preferredWidth: 180
                                Layout.minimumWidth: 110
                                Layout.maximumWidth: 200
                                implicitHeight: 34
                                text: row.who
                                placeholderText: qsTr("имя…")
                                placeholderTextColor: Theme.faint
                                color: dd.colorOf(row.who)
                                font.family: Theme.ui; font.pixelSize: 15; font.bold: true
                                leftPadding: 12; rightPadding: 24
                                selectByMouse: true
                                property string filter: ""
                                readonly property var matches: {
                                    const f = filter.trim().toLowerCase()
                                    return dd.speakerNames().filter(n => !f || n.toLowerCase().indexOf(f) >= 0)
                                }
                                property int hi: 0
                                function pick(name) {
                                    const v = (name || "").trim()
                                    if (!v) { text = row.who; namePop.close(); return }
                                    rows.setProperty(row.index, "who", v)
                                    // a known heroine: her face goes with the line until you pick another
                                    const t = dd.tagOf(v)
                                    if (t && row.tag !== t) {
                                        const c = dd.castOf(t)
                                        rows.setProperty(row.index, "tag", t)
                                        rows.setProperty(row.index, "look", "")
                                    }
                                    text = v
                                    namePop.close()
                                    dd.current = row.index
                                    dd.refresh()
                                }
                                onPressed: { filter = ""; hi = 0; namePop.open() }
                                onActiveFocusChanged: {
                                    if (activeFocus) { filter = ""; hi = 0; namePop.open(); selectAll() }
                                    else if (!namePop.opened && text.trim() && text.trim() !== row.who) pick(text)
                                }
                                onTextEdited: { filter = text; hi = 0; if (!namePop.opened) namePop.open() }
                                Keys.onDownPressed: hi = Math.min(hi + 1, matches.length - 1)
                                Keys.onUpPressed: hi = Math.max(hi - 1, 0)
                                Keys.onEscapePressed: { text = row.who; namePop.close() }
                                onAccepted: pick(filter && matches.length ? matches[hi] : text)
                                background: Rectangle { radius: 17; color: Theme.bg; border.color: whoField.activeFocus ? Theme.accent : Theme.line }
                                Text { x: whoField.width - 18; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: Theme.dim; font.pixelSize: 12 }
                                Popup {
                                    id: namePop
                                    y: whoField.height + 4
                                    width: Math.max(whoField.width, 240)
                                    height: Math.min(380, nameList.contentHeight + 12)
                                    padding: 6
                                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                                    background: Rectangle { color: Theme.panel2; radius: 10; border.color: Theme.accent }
                                    ListView {
                                        id: nameList
                                        anchors.fill: parent
                                        clip: true
                                        model: whoField.matches
                                        currentIndex: whoField.hi
                                        ScrollBar.vertical: ScrollBar {}
                                        footer: Text {
                                            visible: whoField.filter.trim() !== "" && whoField.matches.indexOf(whoField.filter.trim()) < 0
                                            height: visible ? 30 : 0
                                            leftPadding: 8
                                            verticalAlignment: Text.AlignVCenter
                                            text: qsTr("Enter — новый герой «") + whoField.filter.trim() + "»"
                                            color: Theme.gold; font.family: Theme.ui; font.pixelSize: 13
                                        }
                                        delegate: Rectangle {
                                            required property int index
                                            required property string modelData
                                            width: nameList.width
                                            height: 30
                                            radius: 6
                                            color: index === whoField.hi || nameMouse.containsMouse ? Theme.panel3 : "transparent"
                                            Row {
                                                anchors.verticalCenter: parent.verticalCenter
                                                x: 8
                                                spacing: 8
                                                Rectangle { width: 9; height: 9; radius: 5; color: dd.colorOf(modelData); anchors.verticalCenter: parent.verticalCenter }
                                                Text { text: modelData; color: dd.colorOf(modelData); font.family: Theme.ui; font.pixelSize: 14; font.bold: true }
                                            }
                                            MouseArea { id: nameMouse; anchors.fill: parent; hoverEnabled: true; onClicked: whoField.pick(modelData) }
                                        }
                                    }
                                }
                            }
                            // the heroine's look for this line: a face, an outfit, a place -> «показать …» before it
                            AbstractButton {
                                id: spriteBtn
                                implicitWidth: 140
                                implicitHeight: 34
                                visible: row.who !== dd.narrator
                                onClicked: { dd.current = row.index; dd.refresh(); lookPop.openFor(row.index, spriteBtn) }
                                background: Rectangle {
                                    radius: 17
                                    color: row.look ? "#2a9bd35a" : (spriteBtn.hovered ? Theme.panel3 : Theme.bg)
                                    border.color: row.look ? Theme.accent : Theme.line
                                }
                                contentItem: Row {
                                    spacing: 6
                                    leftPadding: 4
                                    Rectangle {
                                        width: 28; height: 28; radius: 14; clip: true
                                        color: Theme.panel3
                                        anchors.verticalCenter: parent.verticalCenter
                                        Image {
                                            anchors.fill: parent
                                            visible: !!row.tag
                                            fillMode: Image.PreserveAspectCrop
                                            sourceSize.width: 64
                                            source: row.tag ? "image://gb/face/" + encodeURIComponent(row.tag + (row.look ? " " + row.look : "")) : ""
                                        }
                                        Text { anchors.centerIn: parent; visible: !row.tag; text: "?"; color: Theme.dim; font.pixelSize: 14 }
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 96
                                        elide: Text.ElideRight
                                        text: row.look ? row.look.split(" ")[0] + (row.pos ? " · " + dd.placeName(row.pos) : "") : qsTr("＋ спрайт")
                                        color: row.look ? Theme.text : Theme.dim
                                        font.family: Theme.ui; font.pixelSize: 13
                                    }
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Кого показать с этой репликой: лицо, одежда, место. Не менялось — второй раз не вставлю")
                            }
                            // voice: takes what is left of the row, so the ✖ never falls off the edge
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 70
                                Layout.maximumWidth: 300
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
                                        text: row.voice ? "♪ " + row.voice.substring(6) : qsTr("без озвучки")
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
                                        ToolTip.text: row.voice ? qsTr("Убрать озвучку") : qsTr("Выбрать файл озвучки")
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
                                ToolTip.text: qsTr("Удалить строку")
                            }
                        }
                            TextField {
                                id: lineText
                                Layout.fillWidth: true
                                Layout.leftMargin: 30
                                implicitHeight: 36
                                text: row.text
                                placeholderText: row.who === dd.narrator ? qsTr("Текст рассказчика…") : qsTr("Что говорит ") + row.who + "…"
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
                    PillButton { dark: true; text: qsTr("＋ Реплика"); onClicked: dd.addRow(true) }
                    PillButton { dark: true; text: qsTr("♪ Озвучка пачкой…"); enabled: !Engine.busy; onClicked: bulkDialog.open() }
                    Text {
                        Layout.fillWidth: true
                        text: Engine.busy ? Engine.busyText : qsTr("файлы лягут начиная со строки ") + (dd.current + 1)
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
                Text { text: qsTr("Строка ") + (dd.current + 1) + qsTr(" — так её увидит игрок"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
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
            PillButton { dark: true; text: qsTr("Отмена"); onClicked: dd.close() }
            PillButton {
                accent: true
                text: { dd.previewSrc; return qsTr("Вставить в сцену  (") + dd.block().length + ")" }
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
            text: qsTr("Отпусти — озвучка ляжет со строки ") + (dd.current + 1) + qsTr(" по порядку")
            color: Theme.accent; font.family: Theme.ui; font.pixelSize: 24; font.bold: true
        }
    }


    // «Спрайт» of a line: which heroine (any of the cast - a name of your own can wear Shurik's face), her outfit,
    // her face (the game's own pictures), where she stands
    Popup {
        id: lookPop
        property int rowIndex: -1
        property string tag: ""
        property string outfit: ""
        property string pos: "center"
        readonly property var names: tag ? Engine.spriteNames(tag) : []
        readonly property var outfits: tag ? Engine.spriteOutfits(tag) : []
        readonly property var faces: names.filter(n => !outfit || n.split(" ").slice(1).join(" ") === outfit)
        function openFor(i, anchorItem) {
            rowIndex = i
            const r = rows.get(i)
            tag = r.tag || dd.tagOf(r.who) || ""
            const look = r.look || ""
            outfit = look ? look.split(" ").slice(1).join(" ") : (outfits.indexOf("pioneer") >= 0 ? "pioneer" : (outfits[0] || ""))
            pos = r.pos || (i % 2 ? "right" : "left")
            open()
        }
        function choose(name) {
            rows.setProperty(rowIndex, "tag", tag)
            rows.setProperty(rowIndex, "look", name)
            rows.setProperty(rowIndex, "pos", pos)
            dd.refresh()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 1000)
        height: Math.min(parent.height - 80, 680)
        modal: true
        padding: 16
        background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.accent }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                InkText { text: qsTr("Спрайт реплики ") + (lookPop.rowIndex + 1); size: 24; color: Theme.gold }
                Item { Layout.fillWidth: true }
                PillButton {
                    dark: true; text: qsTr("Без спрайта")
                    onClicked: { rows.setProperty(lookPop.rowIndex, "look", ""); dd.refresh(); lookPop.close() }
                }
                PillButton { accent: true; text: qsTr("Готово"); onClicked: lookPop.close() }
            }
            // who
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 58
                contentWidth: castRow.implicitWidth
                clip: true
                Row {
                    id: castRow
                    spacing: 6
                    Repeater {
                        model: Engine.cast
                        Rectangle {
                            required property var modelData
                            width: 54; height: 54; radius: 27; clip: true
                            color: Theme.panel3
                            border.width: lookPop.tag === modelData.id ? 3 : 1
                            border.color: lookPop.tag === modelData.id ? Theme.gold : Theme.line
                            Image {
                                anchors.fill: parent; anchors.margins: 2
                                fillMode: Image.PreserveAspectCrop
                                sourceSize.width: 96
                                source: "image://gb/face/" + encodeURIComponent(modelData.id + (modelData.pose ? " " + modelData.pose : ""))
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    lookPop.tag = modelData.id
                                    lookPop.outfit = lookPop.outfits.indexOf("pioneer") >= 0 ? "pioneer" : (lookPop.outfits[0] || "")
                                }
                                ToolTip.visible: containsMouse
                                ToolTip.text: modelData.name
                                hoverEnabled: true
                            }
                        }
                    }
                }
            }
            // outfit + place
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: qsTr("Одежда:"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: lookPop.outfits
                        PillButton {
                            required property string modelData
                            dark: lookPop.outfit !== modelData
                            accent: lookPop.outfit === modelData
                            text: modelData
                            onClicked: lookPop.outfit = modelData
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: qsTr("Где стоит:"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14 }
                Repeater {
                    model: dd.places
                    PillButton {
                        required property var modelData
                        dark: lookPop.pos !== modelData[0]
                        accent: lookPop.pos === modelData[0]
                        text: modelData[1]
                        onClicked: {
                            lookPop.pos = modelData[0]
                            if (rows.get(lookPop.rowIndex).look) { rows.setProperty(lookPop.rowIndex, "pos", lookPop.pos); dd.refresh() }
                        }
                    }
                }
            }
            // faces
            GridView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: 118
                cellHeight: 138
                model: lookPop.faces
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property string modelData
                    width: 110; height: 130; radius: 10
                    readonly property bool on: rows.count > lookPop.rowIndex && lookPop.rowIndex >= 0 && rows.get(lookPop.rowIndex).look === modelData && rows.get(lookPop.rowIndex).tag === lookPop.tag
                    color: on ? "#2a9bd35a" : (faceMouse.containsMouse ? Theme.panel3 : Theme.panel2)
                    border.color: on ? Theme.accent : Theme.line
                    Image {
                        x: 5; y: 5; width: 100; height: 100
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 160
                        source: "image://gb/face/" + encodeURIComponent(lookPop.tag + " " + modelData)
                    }
                    Text {
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                        text: modelData.split(" ")[0]
                        color: Theme.text; font.family: Theme.ui; font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    MouseArea { id: faceMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: lookPop.choose(modelData) }
                }
            }
        }
    }

    FileDialog {
        id: oneDialog
        title: qsTr("Озвучка строки")
        nameFilters: [qsTr("Аудио (*.ogg *.mp3 *.wav *.flac *.m4a *.opus *.aac *.wma)")]
        onAccepted: dd.importUrls([selectedFile])
    }
    FileDialog {
        id: bulkDialog
        title: qsTr("Озвучка пачкой — по порядку строк")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Аудио (*.ogg *.mp3 *.wav *.flac *.m4a *.opus *.aac *.wma)")]
        onAccepted: {
            const files = selectedFiles.slice().sort((a, b) => a.toString().localeCompare(b.toString(), undefined, { numeric: true }))
            dd.importUrls(files)
        }
    }
}
