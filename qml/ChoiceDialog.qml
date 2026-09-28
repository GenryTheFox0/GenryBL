import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Выбор и последствия»: the old Branch Studio. The frame before the choice (background,
// music, a line), the options - each with its own picture for the 7DL-style menu, points and
// what happens in its branch - and one scene everybody meets again after. It writes plain
// story lines; the preview is THE renderer drawing the menu exactly as the game will.
Popup {
    id: cd
    property string storyText: ""
    property int storyLine: 1
    property var existingScenes: []
    property string mode: "images"       // "images" visual choice (7DL) | "es" the game's own menu | "buttons" dark buttons
    readonly property bool images: mode === "images"
    property int current: 0
    property string previewSrc: ""
    property string bg: ""               // "" = keep the current picture
    property string music: ""
    property int timedSeconds: 8
    property int pickFor: -1             // option waiting for ImagePicker (-2 = the background)
    signal insertBlock(string block)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 50, 1640)
    height: Math.min(parent.height - 40, 960)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    ListModel { id: opts }

    function openFor(text, line) {
        storyText = text
        storyLine = line
        existingScenes = Engine.sceneNames(text)
        bg = ""; music = ""
        prompt.text = ""
        timeout.text = ""
        variable.text = ""
        after.text = "после выбора"
        opts.clear()
        opts.append({ caption: "", scene: "", image: "", points: 0, body: "" })
        opts.append({ caption: "", scene: "", image: "", points: 0, body: "" })
        current = 0
        open()
        refresh()
        Qt.callLater(() => { const it = list.itemAtIndex(0); if (it) it.focusCaption() })
    }
    function fillDemo() {                // --shot editor-choice
        prompt.text = "Славя: Куда пойдём?"
        variable.text = "симпатия"
        opts.clear()
        opts.append({ caption: "Пойти со Славей", scene: "", image: "sl smile pioneer", points: 1, body: "Славя: Отлично, пойдём!" })
        opts.append({ caption: "Остаться с Алисой", scene: "", image: "dv grin pioneer", points: -1, body: "Алиса: Ну и правильно." })
        opts.append({ caption: "На пляж", scene: "", image: "bg ext_beach_day", points: 0, body: "" })
        refresh()
    }

    function openPickerDemo() { pickFor = 0; picker.tab = 0; picker.tag = "sl"; picker.tagName = "Славя"; picker.open() }   // --shot
    function sceneOf(i) {
        const o = opts.get(i)
        const s = (o.scene || "").trim()
        if (s) return s
        const c = (o.caption || "").trim()
        return c ? c : "вариант " + (i + 1)
    }
    function exists(name) { return existingScenes.indexOf(name) >= 0 }
    function block(forPreview) {
        const out = []
        if (bg) out.push((bg.indexOf("cg ") === 0 ? "цг " + bg.substring(3) : "фон " + bg.replace(/^bg /, "")) + " fade")
        if (music) out.push("музыка " + music)
        const q = prompt.text.trim()
        if (q) out.push(q.indexOf(":") > 0 ? q : "текст " + q)
        out.push(mode === "images" ? "выбор визуальный" : mode === "buttons" ? "выбор кнопки" : mode === "timed" ? "выбор на время " + timedSeconds : "выбор")
        const scenes = []
        for (let i = 0; i < opts.count; ++i) {
            const o = opts.get(i)
            const cap = o.caption.trim() || (forPreview ? "Вариант " + (i + 1) : "")
            if (!cap) continue
            const sc = sceneOf(i)
            scenes.push(i)
            out.push("- " + cap + " -> " + sc + (images && o.image ? " | " + o.image : ""))
        }
        const late = mode === "timed" ? timeout.text.trim() : ""
        if (late) out.push("время вышло -> " + late)
        out.push("конецвыбора")
        if (forPreview) return out
        const v = variable.text.trim()
        const aft = after.text.trim()
        const made = {}
        if (late && !exists(late)) {                     // «не успел»: its own scene, then everybody meets again
            made[late] = true
            out.push("", ": " + late)
            if (aft) out.push("переход " + aft)
        }
        for (const i of scenes) {
            const o = opts.get(i)
            const sc = sceneOf(i)
            if (exists(sc) || made[sc]) continue           // leads into a scene that already exists
            made[sc] = true
            out.push("", ": " + sc)
            if (v && o.points) out.push("прибавить " + v + " " + o.points)
            const body = o.body.split("\n").map(l => l.trim()).filter(l => l)
            for (const l of body) out.push(l)
            const last = body.length ? body[body.length - 1].split(" ")[0] : ""
            if (aft && ["переход", "конецигры", "конецсцены", "вызов"].indexOf(last) < 0) out.push("переход " + aft)
        }
        if (aft && !exists(aft)) out.push("", ": " + aft, "конецсцены")
        return out
    }
    function problems() {
        const p = []
        let n = 0
        for (let i = 0; i < opts.count; ++i) if (opts.get(i).caption.trim()) ++n
        if (n < 2) p.push("нужно хотя бы два варианта с текстом")
        const seen = {}
        for (let i = 0; i < opts.count; ++i) {
            if (!opts.get(i).caption.trim()) continue
            const sc = sceneOf(i)
            if (seen[sc] && !exists(sc)) p.push("два варианта ведут в одну новую сцену «" + sc + "»")
            seen[sc] = true
        }
        if (/[^\wа-яё ]/i.test(variable.text.trim())) p.push("в названии очков только буквы, цифры и _")
        return p
    }
    function refresh() { previewTimer.restart() }
    function addOption() {
        opts.append({ caption: "", scene: "", image: "", points: 0, body: "" })
        current = opts.count - 1
        refresh()
        Qt.callLater(() => { list.positionViewAtEnd(); const it = list.itemAtIndex(cd.current); if (it) it.focusCaption() })
    }
    function commit() {
        const p = problems()
        if (p.length) { Engine.toast(p[0], 2); return }
        insertBlock(block(false).join("\n"))
        close()
    }

    Timer {
        id: previewTimer
        interval: 60
        onTriggered: cd.previewSrc = Engine.previewUrl(cd.storyText, cd.storyLine, cd.block(true).join("\n"), cd.current)
    }

    ImagePicker {
        id: picker
        onPicked: (img) => {
            if (cd.pickFor === -2) {
                if (!/^(bg|cg) /.test(img)) { Engine.toast("Для фона выбери фон или CG", 1); return }
                cd.bg = img
            }
            else if (cd.pickFor >= 0) opts.setProperty(cd.pickFor, "image", img)
            cd.refresh()
        }
    }

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }

    component Label2: Text { color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
    component Field: TextField {
        implicitHeight: 34
        placeholderTextColor: Theme.faint
        color: Theme.text
        font.family: Theme.ui; font.pixelSize: 15
        selectByMouse: true
        background: Rectangle { radius: 8; color: Theme.bg; border.color: parent.activeFocus ? Theme.accent : Theme.line }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            InkText { text: "Выбор и последствия"; size: 28; color: Theme.gold }
            Text {
                Layout.fillWidth: true
                text: "варианты → свои сцены с очками → общая сцена после"
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14
                elide: Text.ElideRight
            }
            Repeater {
                model: [["images", "Визуальный выбор"], ["timed", "На время"], ["es", "Как в БЛ"], ["buttons", "Кнопки"]]
                PillButton {
                    required property var modelData
                    text: modelData[1]
                    accent: cd.mode === modelData[0]
                    dark: cd.mode !== modelData[0]
                    onClicked: { cd.mode = modelData[0]; cd.refresh() }
                }
            }
            PillButton { dark: true; text: "✕"; onClicked: cd.close() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // ---------------- left: the frame, the options, the scene after
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                // the frame before the choice
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: frameCol.implicitHeight + 20
                    radius: 10
                    color: Theme.panel2
                    border.color: Theme.line
                    ColumnLayout {
                        id: frameCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        Label2 { text: "Кадр перед выбором" }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            AbstractButton {
                                implicitWidth: 112; implicitHeight: 63
                                onClicked: { cd.pickFor = -2; picker.tab = 1; picker.open() }
                                background: Rectangle { radius: 6; color: Theme.bg; border.color: parent.hovered ? Theme.accent : Theme.line; clip: true
                                    Image { anchors.fill: parent; anchors.margins: 1; visible: !!cd.bg; source: cd.bg ? "image://gb/" + cd.bg.substring(0, 2) + "/" + encodeURIComponent(cd.bg.substring(3)) : ""; sourceSize: Qt.size(224, 126); asynchronous: true; fillMode: Image.PreserveAspectCrop }
                                    Text { anchors.centerIn: parent; visible: !cd.bg; text: "фон\nкак есть"; horizontalAlignment: Text.AlignHCenter; color: Theme.faint; font.family: Theme.ui; font.pixelSize: 12 }
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: cd.bg ? cd.bg + " — клик: другой" : "Сменить фон перед выбором"
                            }
                            PillButton { dark: true; visible: !!cd.bg; text: "✕"; onClicked: { cd.bg = ""; cd.refresh() } }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Field {
                                    id: prompt
                                    Layout.fillWidth: true
                                    placeholderText: "Реплика перед выбором: «Славя: Куда пойдём?» или просто текст"
                                    onTextEdited: cd.refresh()
                                }
                                DarkCombo {
                                    id: musicBox
                                    Layout.fillWidth: true
                                    model: ["♪ музыка как есть"].concat(Engine.music.filter(m => !m.custom).map(m => m.word))
                                    onActivated: { cd.music = currentIndex > 0 ? currentText : ""; cd.refresh() }
                                }
                            }
                        }
                    }
                }

                // «На время»: seconds + where the story goes when the player did not make it
                RowLayout {
                    visible: cd.mode === "timed"
                    Layout.fillWidth: true
                    spacing: 8
                    Label2 { text: "Секунд" }
                    PillButton { dark: true; text: "−"; implicitWidth: 30; onClicked: { cd.timedSeconds = Math.max(2, cd.timedSeconds - 1); cd.refresh() } }
                    Text { text: cd.timedSeconds; color: Theme.gold; font.family: Theme.mono; font.pixelSize: 18; font.bold: true; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 30 }
                    PillButton { dark: true; text: "+"; implicitWidth: 30; onClicked: { cd.timedSeconds = Math.min(60, cd.timedSeconds + 1); cd.refresh() } }
                    Label2 { text: "Не успел →" }
                    Field {
                        id: timeout
                        Layout.fillWidth: true
                        placeholderText: "сцена «молчание» (пусто — история просто идёт дальше)"
                        onTextEdited: cd.refresh()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label2 { text: "Очки" }
                    Field {
                        id: variable
                        Layout.preferredWidth: 220
                        placeholderText: "симпатия_алисы (можно пусто)"
                        onTextEdited: cd.refresh()
                    }
                    Label2 { Layout.fillWidth: true; text: variable.text.trim() ? "у каждого варианта: сколько прибавить" : "без очков — просто развилка"; elide: Text.ElideRight }
                }

                // the options
                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: opts
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: card
                        required property int index
                        required property string caption
                        required property string scene
                        required property string image
                        required property int points
                        required property string body
                        function focusCaption() { capField.forceActiveFocus() }
                        width: list.width - 12
                        height: cardCol.implicitHeight + 16
                        radius: 10
                        color: cd.current === index ? Theme.panel3 : Theme.panel2
                        border.color: cd.current === index ? Theme.accent : Theme.line
                        MouseArea { anchors.fill: parent; onClicked: { cd.current = card.index; cd.refresh() } }
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 10
                            // the picture of this option (7DL mode)
                            AbstractButton {
                                visible: cd.images
                                Layout.preferredWidth: 96
                                Layout.fillHeight: true
                                onClicked: { cd.current = card.index; cd.pickFor = card.index; picker.tab = card.image.indexOf("bg ") === 0 ? 1 : card.image.indexOf("cg ") === 0 ? 2 : 0; picker.tag = ""; picker.open() }
                                background: Rectangle {
                                    radius: 8; color: Theme.bg; clip: true
                                    border.color: parent.hovered ? Theme.accent : Theme.line
                                    Image {
                                        anchors.fill: parent; anchors.margins: 1
                                        visible: !!card.image
                                        source: !card.image ? "" : (card.image.indexOf("bg ") === 0 || card.image.indexOf("cg ") === 0)
                                                ? "image://gb/" + card.image.substring(0, 2) + "/" + encodeURIComponent(card.image.substring(3))
                                                : "image://gb/face/" + encodeURIComponent(card.image)
                                        sourceSize: Qt.size(192, 192); asynchronous: true
                                        fillMode: Image.PreserveAspectCrop
                                    }
                                    Text { anchors.centerIn: parent; visible: !card.image; text: "＋\nкартинка"; horizontalAlignment: Text.AlignHCenter; color: Theme.faint; font.family: Theme.ui; font.pixelSize: 13 }
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: card.image ? card.image + " — клик: другая" : "Герой, фон или CG для этой полосы меню"
                            }
                            ColumnLayout {
                                id: cardCol
                                Layout.fillWidth: true
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text { text: card.index + 1; color: Theme.faint; font.family: Theme.mono; font.pixelSize: 13 }
                                    Field {
                                        id: capField
                                        Layout.fillWidth: true
                                        text: card.caption
                                        placeholderText: "Текст варианта, например «Пойти со Славей»"
                                        font.bold: true
                                        onTextEdited: { opts.setProperty(card.index, "caption", text); cd.current = card.index; cd.refresh() }
                                        onActiveFocusChanged: if (activeFocus && cd.current !== card.index) { cd.current = card.index; cd.refresh() }
                                    }
                                    // points
                                    Row {
                                        visible: variable.text.trim() !== ""
                                        spacing: 2
                                        PillButton { dark: true; text: "−"; implicitWidth: 30; onClicked: { opts.setProperty(card.index, "points", Math.max(-9, card.points - 1)); cd.refresh() } }
                                        Text {
                                            width: 38; height: 30
                                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                            text: (card.points > 0 ? "+" : "") + card.points
                                            color: card.points > 0 ? Theme.good : card.points < 0 ? Theme.bad : Theme.dim
                                            font.family: Theme.mono; font.pixelSize: 16; font.bold: true
                                        }
                                        PillButton { dark: true; text: "+"; implicitWidth: 30; onClicked: { opts.setProperty(card.index, "points", Math.min(9, card.points + 1)); cd.refresh() } }
                                    }
                                    AbstractButton {
                                        implicitWidth: 30; implicitHeight: 30
                                        enabled: opts.count > 2
                                        opacity: enabled ? 1 : 0.3
                                        onClicked: { opts.remove(card.index); cd.current = Math.min(cd.current, opts.count - 1); cd.refresh() }
                                        background: Rectangle { radius: 15; color: parent.hovered ? "#44ff6b6b" : "transparent" }
                                        contentItem: Text { text: "✖"; color: Theme.dim; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Убрать вариант"
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text { text: "→"; color: Theme.faint; font.pixelSize: 15 }
                                    Field {
                                        Layout.preferredWidth: 210
                                        text: card.scene
                                        placeholderText: "сцена: " + cd.sceneOf(card.index)
                                        onTextEdited: { opts.setProperty(card.index, "scene", text); cd.refresh() }
                                        ToolTip.visible: activeFocus
                                        ToolTip.text: cd.exists(cd.sceneOf(card.index)) ? "Такая сцена уже есть — вариант поведёт в неё" : "Новая сцена этой ветки"
                                    }
                                    Field {
                                        Layout.fillWidth: true
                                        text: card.body
                                        placeholderText: cd.exists(cd.sceneOf(card.index)) ? "ведёт в существующую сцену" : "что происходит: «Славя: Отлично!»"
                                        enabled: !cd.exists(cd.sceneOf(card.index))
                                        onTextEdited: { opts.setProperty(card.index, "body", text); cd.refresh() }
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    PillButton { dark: true; text: "＋ Вариант"; enabled: opts.count < 6; onClicked: cd.addOption() }
                    Item { Layout.fillWidth: true }
                    Label2 { text: "Потом все встречаются в сцене" }
                    Field { id: after; Layout.preferredWidth: 220; placeholderText: "пусто — ветки не сходятся"; onTextEdited: cd.refresh() }
                }
            }

            // ---------------- right: the menu as the player sees it + the story lines
            ColumnLayout {
                Layout.preferredWidth: Math.min(700, cd.width * 0.44)
                Layout.fillHeight: true
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 16
                    color: "black"
                    radius: 6
                    clip: true
                    SmoothImage { anchors.fill: parent; source: cd.previewSrc; fade: 120 }
                }
                Label2 {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: cd.mode === "images" ? "Визуальный выбор как в 7ДЛ: подсвечен выбранный вариант, остальные притушены — в игре загорается тот, что под мышкой"
                        : cd.mode === "es" ? "Родное меню выбора самой БЛ: цвета по времени суток, подсвечен выбранный вариант"
                        : cd.mode === "timed" ? "Как в Telltale: полоса тает к центру, не успел — своя сцена"
                        : "Тёмные кнопки старого конструктора"
                }
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
                            text: { cd.previewSrc; return cd.block(false).join("\n") }
                            color: "#d8d8e4"
                            font.family: Theme.mono; font.pixelSize: 13
                            background: null
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: { cd.previewSrc; const p = cd.problems(); return p.length ? "⚠ " + p.join(" · ") : "" }
                color: Theme.warn; font.family: Theme.ui; font.pixelSize: 14
                elide: Text.ElideRight
            }
            PillButton { dark: true; text: "Отмена"; onClicked: cd.close() }
            PillButton { accent: true; text: "Вставить в сцену"; onClicked: cd.commit() }
        }
    }
}
