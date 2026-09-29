import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GenryBL

// «Название мода»: how the mod stands in ES «Моды и пользовательские сценарии» - its own
// font (the game's or an imported .ttf), colour, size, bold / italic - with that very page
// drawn live. Writes the @mod_name / @mod_title_* lines of the story.
Popup {
    id: mt
    property string storyText: ""
    property var fonts: []
    property string name: ""
    property string author: ""
    property string hero: ""
    property string heroAsk: ""         // "" | menu | start
    property bool heroShe: false
    property string fontRef: ""          // "" = ES default (corbel)
    property string colorHex: ""         // "" = ES default brown
    property int size: 36
    property bool bold: false
    property bool italic: false
    signal applied(string text)

    readonly property var swatches: ["", "#b3001b", "#ff4f4f", "#ffd27d", "#e0a526", "#2e7d32", "#1565c0", "#6a1b9a", "#000000", "#ffffff"]

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 1500)
    height: Math.min(parent.height - 50, 880)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openFor(text) {
        storyText = text
        const t = Engine.modTitle(text)
        name = t.name
        author = t.author || ""
        hero = t.hero || ""
        heroAsk = t.heroAsk || ""
        heroShe = !!t.heroShe
        fontRef = t.font || ""
        colorHex = t.color || ""
        size = parseInt(t.size) || 36
        bold = (t.style || "").indexOf("b") >= 0
        italic = (t.style || "").indexOf("i") >= 0
        fonts = Engine.modFonts()
        nameField.text = name
        authorField.text = author
        heroField.text = hero
        hexField.text = colorHex
        open()
    }
    function titleState() {
        return { name: name, author: author.trim(), hero: hero.trim(), heroAsk: heroAsk, heroShe: heroShe, font: fontRef, color: colorHex, size: size === 36 ? "" : String(size), style: (bold ? "b" : "") + (italic ? "i" : "") }
    }
    function familyOf(ref) {
        for (const f of fonts) if (f.ref === ref) return f.family
        return Theme.riffic
    }

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            InkText { text: "Название мода"; size: 28; color: Theme.gold }
            Text {
                Layout.fillWidth: true
                text: "так мод стоит в «Моды и пользовательские сценарии» самой игры"
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14
                elide: Text.ElideRight
            }
            PillButton { dark: true; text: "✕"; onClicked: mt.close() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            ColumnLayout {
                Layout.preferredWidth: 470
                Layout.fillHeight: true
                spacing: 10
                Text { text: "Название"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    implicitHeight: 38
                    color: Theme.text
                    font.family: Theme.ui; font.pixelSize: 17
                    selectByMouse: true
                    onTextEdited: mt.name = text
                    background: Rectangle { radius: 8; color: Theme.bg; border.color: nameField.activeFocus ? Theme.accent : Theme.line }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "Автор"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                    TextField {
                        id: authorField
                        Layout.fillWidth: true
                        implicitHeight: 34
                        placeholderText: "пусто — в меню мода автора не будет"
                        placeholderTextColor: Theme.faint
                        color: Theme.text
                        font.family: Theme.ui; font.pixelSize: 15
                        selectByMouse: true
                        onTextEdited: mt.author = text
                        background: Rectangle { radius: 8; color: Theme.bg; border.color: authorField.activeFocus ? Theme.accent : Theme.line }
                    }
                    PillButton { dark: true; text: "Убрать"; visible: mt.author !== ""; onClicked: { mt.author = ""; authorField.text = "" } }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "Герой"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                    TextField {
                        id: heroField
                        Layout.fillWidth: true
                        implicitHeight: 34
                        placeholderText: "Семён — или своё имя героя"
                        placeholderTextColor: Theme.faint
                        color: Theme.text
                        font.family: Theme.ui; font.pixelSize: 15
                        selectByMouse: true
                        onTextEdited: mt.hero = text
                        background: Rectangle { radius: 8; color: Theme.bg; border.color: heroField.activeFocus ? Theme.accent : Theme.line }
                    }
                    PillButton { text: "Он"; accent: !mt.heroShe; dark: mt.heroShe; onClicked: mt.heroShe = false }
                    PillButton { text: "Она"; accent: mt.heroShe; dark: !mt.heroShe; onClicked: mt.heroShe = true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: "Игрок вводит имя"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                    Item { Layout.fillWidth: true }
                    Repeater {
                        model: [["", "Нет"], ["menu", "В меню мода"], ["start", "В начале"]]
                        PillButton {
                            required property var modelData
                            text: modelData[1]
                            accent: mt.heroAsk === modelData[0]
                            dark: mt.heroAsk !== modelData[0]
                            onClicked: mt.heroAsk = modelData[0]
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: mt.heroAsk === "menu" ? "В меню мода сама появится кнопка «Имя: …» (нет меню — спросит в начале). В тексте имя — «[имя]»."
                        : mt.heroAsk === "start" ? "Перед первой строкой игрок увидит табличку «Как тебя зовут?». В тексте имя — «[имя]»."
                        : "Спросить можно и посреди истории: команда «Имя игрока». В тексте имя — «[имя]»."
                    // the forms are the same everywhere; one line of help under the choice
                    + " Падежи: «[имя кого]», «[имя кому]», «[имя кем]», «[имя о ком]». Он/она: «[проснулся/проснулась]» — при вводе имени игрок сам выберет пол."
                    color: Theme.faint; font.family: Theme.ui; font.pixelSize: 12
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Шрифт"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13; Layout.fillWidth: true }
                    PillButton { dark: true; text: "＋ Свой .ttf"; onClicked: fontDialog.open() }
                }
                ListView {
                    id: fontList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    model: [{ ref: "", label: "как в игре (corbel)", family: mt.familyOf("es:fonts/corbel.ttf") }].concat(mt.fonts)
                    delegate: Rectangle {
                        required property var modelData
                        width: fontList.width - 10
                        height: 50
                        radius: 8
                        color: mt.fontRef === modelData.ref ? Theme.panel3 : area.containsMouse ? Theme.panel2 : "transparent"
                        border.color: mt.fontRef === modelData.ref ? Theme.accent : "transparent"
                        Text {
                            x: 12; anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 170
                            text: mt.name || "Мой мод"
                            font.family: modelData.family
                            font.pixelSize: 24
                            color: Theme.text
                            elide: Text.ElideRight
                        }
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: Theme.faint; font.family: Theme.mono; font.pixelSize: 11
                        }
                        MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); mt.fontRef = modelData.ref } }
                    }
                }
                Text { text: "Цвет"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: mt.swatches
                        Rectangle {
                            required property string modelData
                            width: 34; height: 34; radius: 17
                            color: modelData || "#4d2e19"
                            border.width: mt.colorHex === modelData ? 3 : 1
                            border.color: mt.colorHex === modelData ? Theme.accent : Theme.line
                            Text { anchors.centerIn: parent; visible: !modelData; text: "БЛ"; color: "#f4ecd2"; font.pixelSize: 10; font.bold: true }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { mt.colorHex = modelData; hexField.text = modelData } }
                        }
                    }
                    TextField {
                        id: hexField
                        width: 110; height: 34
                        placeholderText: "#rrggbb"
                        placeholderTextColor: Theme.faint
                        color: Theme.text
                        font.family: Theme.mono; font.pixelSize: 14
                        onTextEdited: if (/^#[0-9a-fA-F]{6}$/.test(text) || text === "") mt.colorHex = text
                        background: Rectangle { radius: 17; color: Theme.bg; border.color: hexField.activeFocus ? Theme.accent : Theme.line }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text { text: "Размер"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13 }
                    Slider {
                        id: sizeSlider
                        Layout.fillWidth: true
                        from: 22; to: 64; stepSize: 1
                        value: mt.size
                        onMoved: mt.size = value
                    }
                    Text { text: mt.size; color: Theme.gold; font.family: Theme.mono; font.pixelSize: 16; Layout.preferredWidth: 28 }
                    PillButton { text: "Ж"; accent: mt.bold; dark: !mt.bold; onClicked: mt.bold = !mt.bold }
                    PillButton { text: "К"; accent: mt.italic; dark: !mt.italic; onClicked: mt.italic = !mt.italic }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 16
                    color: "black"
                    radius: 6
                    clip: true
                    SmoothImage {
                        anchors.fill: parent
                        fade: 100
                        source: Engine.modsListUrl({ name: mt.name, font: mt.fontRef, color: mt.colorHex, size: mt.size, style: (mt.bold ? "b" : "") + (mt.italic ? "i" : "") })
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "Свой шрифт кладётся в мод (fonts/…), шрифт игры не копируется. Автор пишется в меню мода («автор: …») и подставляется в следующие новые моды. Строки в сценарии: @mod_name, @author, @mod_title_font, @mod_title_color, @mod_title_size, @mod_title_style."
                    color: Theme.faint; font.family: Theme.ui; font.pixelSize: 13
                }
                Item { Layout.fillHeight: true }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            PillButton { dark: true; text: "Как в игре"; onClicked: { mt.fontRef = ""; mt.colorHex = ""; hexField.text = ""; mt.size = 36; mt.bold = false; mt.italic = false } }
            PillButton {
                dark: true
                text: "🎲 Наугад"
                // a ready look: the game's fonts with Cyrillic in colours that read on the list's paper
                onClicked: {
                    const looks = Engine.titleLooks().filter(l => l.font !== mt.fontRef)
                    const l = looks[Math.floor(Math.random() * looks.length)]
                    mt.fontRef = l.font; mt.colorHex = l.color; hexField.text = l.color; mt.size = l.size
                    mt.bold = l.style.indexOf("b") >= 0; mt.italic = l.style.indexOf("i") >= 0
                    Engine.toast("Стиль «" + l.name + "»", 0)
                }
            }
            Item { Layout.fillWidth: true }
            PillButton { dark: true; text: "Отмена"; onClicked: mt.close() }
            PillButton {
                accent: true
                text: "Сохранить"
                onClicked: { mt.applied(Engine.applyModTitle(mt.storyText, mt.titleState())); mt.close() }
            }
        }
    }

    FileDialog {
        id: fontDialog
        title: "Шрифт для названия (.ttf / .otf)"
        nameFilters: ["Шрифты (*.ttf *.otf)"]
        onAccepted: {
            const ref = Engine.importFont(selectedFile)
            if (ref) { mt.fonts = Engine.modFonts(); mt.fontRef = ref }
        }
    }
}
