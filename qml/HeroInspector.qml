import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// V2.1.2 «Герой»: the character's properties as Filmora keeps them - place, size, turn, see-through, flip, and ◆
// a keyframe. «Автоключ» on: a change is an animation from where the character stood (Ren'Py eases it); off: the
// character stands so at once. Every change is a «ключ» line in the story - the editor writes it.
Item {
    id: hi
    property var boxes: []                 // Engine.spriteBoxes at the cursor
    property string selected: ""
    property bool autoKey: false
    property real seconds: 0.6
    readonly property var box: {
        for (const b of boxes) if (b.tag === selected) return b
        return boxes.length ? boxes[boxes.length - 1] : null
    }
    signal key(var box, var change, bool fresh)    // fresh: a new keyframe even when the cursor stands on its own one
    signal pick(string tag)
    signal autoKeyEdited(bool on)
    signal secondsEdited(real seconds)

    // one property row: the name, a slider, the value, ◆
    component PropRow: RowLayout {
        id: row
        property string label
        property real from: 0
        property real to: 1
        property real value: 0
        property real stepSize: 0.01
        property real factor: 100            // shown = value * factor
        property string unit: "%"
        signal commit(real v)
        signal keyHere()
        spacing: 8
        Text { Layout.preferredWidth: 104; text: row.label; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14; elide: Text.ElideRight }
        Slider {
            id: s
            Layout.fillWidth: true
            from: row.from; to: row.to; stepSize: row.stepSize
            onPressedChanged: if (!pressed && Math.abs(value - row.value) > row.stepSize / 2) row.commit(value)
            Binding on value { value: row.value; when: !s.pressed }
        }
        Text {
            Layout.preferredWidth: 56
            horizontalAlignment: Text.AlignRight
            text: Math.round(s.value * row.factor) + row.unit
            color: Theme.text; font.family: Theme.mono; font.pixelSize: 13
        }
        AbstractButton {
            implicitWidth: 28; implicitHeight: 28
            hoverEnabled: true
            onClicked: row.keyHere()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("◆ Ключевой кадр здесь")
            background: Rectangle { radius: 14; color: parent.hovered ? Theme.accent : "transparent"; border.color: Theme.line }
            contentItem: Text { text: "◆"; color: parent.hovered ? "#16240c" : Theme.gold; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 10
        contentWidth: width
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: col
            width: parent.width
            spacing: 8

            Text {
                Layout.fillWidth: true
                visible: !hi.box
                wrapMode: Text.Wrap
                text: qsTr("Здесь герои той строки, где стоит курсор. Покажи кого-нибудь («показать …») — и двигай ползунками или мышкой прямо в превью.")
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15
            }
            // who: the characters on the frame
            Flow {
                Layout.fillWidth: true
                visible: hi.boxes.length > 1
                spacing: 6
                Repeater {
                    model: hi.boxes
                    PillButton {
                        required property var modelData
                        dark: !hi.box || hi.box.tag !== modelData.tag
                        accent: !!hi.box && hi.box.tag === modelData.tag
                        text: modelData.image
                        onClicked: hi.pick(modelData.tag)
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !!hi.box
                text: hi.box ? hi.box.image : ""
                elide: Text.ElideRight
                color: Theme.gold; font.family: Theme.ui; font.pixelSize: 17; font.bold: true
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !!hi.box
                spacing: 10
                DarkCheck {
                    text: qsTr("◆ Автоключ")
                    checked: hi.autoKey
                    onToggled: hi.autoKeyEdited(checked)
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: hi.autoKey ? qsTr("изменение — плавная анимация, как в Филморе") : qsTr("изменение ставит героя так сразу")
                    color: Theme.faint; font.family: Theme.ui; font.pixelSize: 13
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !!hi.box && hi.autoKey
                spacing: 8
                Text { Layout.preferredWidth: 104; text: qsTr("Длится"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14 }
                Slider {
                    id: secs
                    Layout.fillWidth: true
                    from: 0.1; to: 3; stepSize: 0.1
                    value: hi.seconds
                    onMoved: hi.secondsEdited(Math.round(value * 10) / 10)
                }
                Text { Layout.preferredWidth: 56; horizontalAlignment: Text.AlignRight; text: qsTr("%1 с").arg(secs.value.toFixed(1)); color: Theme.text; font.family: Theme.mono; font.pixelSize: 13 }
                Item { implicitWidth: 28 }
            }
            Rectangle { Layout.fillWidth: true; visible: !!hi.box; height: 1; color: Theme.line }

            PropRow {
                visible: !!hi.box
                Layout.fillWidth: true
                label: qsTr("Позиция X"); from: -0.2; to: 1.2
                value: hi.box ? hi.box.kx : 0.5
                onCommit: (v) => hi.key(hi.box, { x: v }, false)
                onKeyHere: hi.key(hi.box, {}, true)
            }
            PropRow {
                visible: !!hi.box
                Layout.fillWidth: true
                label: qsTr("Позиция Y"); from: -0.6; to: 0.6
                value: hi.box ? hi.box.ky : 0
                onCommit: (v) => hi.key(hi.box, { y: v }, false)
                onKeyHere: hi.key(hi.box, {}, true)
            }
            PropRow {
                visible: !!hi.box
                Layout.fillWidth: true
                label: qsTr("Масштаб"); from: 0.2; to: 3
                value: hi.box ? hi.box.zoom : 1
                onCommit: (v) => hi.key(hi.box, { zoom: v }, false)
                onKeyHere: hi.key(hi.box, {}, true)
            }
            PropRow {
                visible: !!hi.box
                Layout.fillWidth: true
                label: qsTr("Поворот"); from: -180; to: 180; stepSize: 1; factor: 1; unit: "°"
                value: hi.box ? hi.box.rotate : 0
                onCommit: (v) => hi.key(hi.box, { rotate: v }, false)
                onKeyHere: hi.key(hi.box, {}, true)
            }
            PropRow {
                visible: !!hi.box
                Layout.fillWidth: true
                label: qsTr("Прозрачность"); from: 0; to: 1
                value: hi.box ? hi.box.alpha : 1
                onCommit: (v) => hi.key(hi.box, { alpha: v }, false)
                onKeyHere: hi.key(hi.box, {}, true)
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !!hi.box
                spacing: 8
                DarkCheck {
                    text: qsTr("Кувырок (зеркально)")
                    checked: !!hi.box && !!hi.box.flip
                    onToggled: hi.key(hi.box, { flip: checked }, false)
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !!hi.box
                spacing: 8
                PillButton { accent: true; text: qsTr("◆ Ключевой кадр здесь"); onClicked: hi.key(hi.box, {}, true) }
                PillButton {
                    dark: true
                    text: qsTr("Сбросить")
                    onClicked: hi.key(hi.box, { y: 0, zoom: 1, rotate: 0, alpha: 1, flip: false }, false)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Обычный размер, без поворота и прозрачности — место остаётся")
                }
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 4
                wrapMode: Text.Wrap
                visible: !!hi.box
                text: qsTr("Мышь в превью: клик — выбрать героя. С ◆ — тащи куда угодно, колёсико меняет масштаб, и всё это плавная анимация. Без ◆ — тащи по местам Лета, колёсико — ближе/дальше.")
                color: Theme.faint; font.family: Theme.ui; font.pixelSize: 13
            }
        }
    }
}
