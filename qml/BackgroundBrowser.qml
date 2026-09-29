import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import GenryBL

// Every background and CG of the camp (+ the project's own). Hover = the frame changes.
Item {
    id: bb
    signal insert(string cmd)
    signal hoverCommand(string cmd)
    property bool cg: false
    property bool adult: false             // the «18+» folder: the Workshop hentai patch, everything of it
    property bool blur: String(Engine.setting("blur18", true)) !== "false"
    property string trans: "fade"
    property string filter: ""
    function cmdFor(id) { return (bb.cg || bb.adult ? "цг " : "фон ") + id + (trans ? " " + trans : "") }
    // the folder: the patch's CGs and old cards, then the game's «body» sprites (old bodies with the patch)
    readonly property var adultModel: Engine.patchImages().map(p => ({ id: p.id, kind: p.card ? "card" : "cg", have: p.have }))
        .concat(Engine.patchSprites().map(p => ({ id: p.image, kind: "sprite", have: true })))

    Column {
        id: head
        x: 10; y: 10
        width: parent.width - 20
        spacing: 8
        Row {
            spacing: 6
            PillButton { text: "Фоны"; accent: !bb.cg && !bb.adult; dark: !accent; onClicked: { bb.cg = false; bb.adult = false } }
            PillButton { text: "CG"; accent: bb.cg && !bb.adult; dark: !accent; onClicked: { bb.cg = true; bb.adult = false } }
            AbstractButton {
                implicitWidth: 52; implicitHeight: 30
                onClicked: { Sfx.click(); bb.adult = true }
                background: Rectangle { radius: 15; color: bb.adult ? "#e74c3c" : "#8e2a20"; border.color: "#5a130c" }
                contentItem: Text { text: "18+"; color: "white"; font.family: Theme.ui; font.pixelSize: 15; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                ToolTip.visible: hovered
                ToolTip.text: "Официальный вырезанный 18+ контент БЛ (хентай-патч Мастерской)"
            }
            DarkCombo {
                width: 130
                model: ["fade", "dissolve", "без перехода", "fade2", "fade3", "dspr", "pixellate"]
                onActivated: (i) => bb.trans = i === 2 ? "" : model[i]
            }
            PillButton { accent: true; text: "＋ Свой"; onClicked: imgDialog.open() }
        }
        // the «18+» folder: what it is, whether the player has it
        Rectangle {
            visible: bb.adult
            width: parent.width
            height: adultInfo.implicitHeight + 20
            radius: 10
            color: "#33c0392b"
            border.color: "#c0392b"
            Column {
                id: adultInfo
                x: 10; y: 10
                width: parent.width - 20
                spacing: 6
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "18+ · официальный контент БЛ, вырезанный из Steam — патч «Deleted hentai scenes» (автор — Лена, Мастерская Steam, id 1118110148): его CG, старые карточки дней и старые тела героинь."
                    color: Theme.text; font.family: Theme.ui; font.pixelSize: 13
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: !Engine.patchInstalled ? "⚠ Патча нет ни в GenryBL, ни в подписках — превью пустые."
                        : Engine.patchBundled ? "✓ Патч встроен в GenryBL — подписываться не надо. Картинки, что ты покажешь, лягут прямо в мод: игроки увидят их и без патча."
                                              : "✓ Патч у тебя подписан. Картинки, что ты покажешь, лягут прямо в мод: игроки увидят их и без патча."
                    color: Engine.patchInstalled ? Theme.good : Theme.warn; font.family: Theme.ui; font.pixelSize: 13
                }
                Row {
                    spacing: 6
                    PillButton { dark: true; text: bb.blur ? "Размытие: вкл" : "Размытие: выкл"; onClicked: { bb.blur = !bb.blur; Engine.setSetting("blur18", bb.blur) } }
                    PillButton { dark: true; text: "Патч в Мастерской"; onClicked: Qt.openUrlExternally("https://steamcommunity.com/sharedfiles/filedetails/?id=1118110148") }
                    PillButton { dark: true; text: "Автор патча"; onClicked: Qt.openUrlExternally("https://steamcommunity.com/id/Lena_sova") }
                }
            }
        }
        TextField {
            id: search
            width: parent.width
            placeholderText: "Поиск: square, beach, night, dv…"
            placeholderTextColor: Theme.faint
            color: Theme.text
            font.family: Theme.ui; font.pixelSize: 14
            selectByMouse: true
            background: Rectangle { radius: 8; color: Theme.bg; border.color: search.activeFocus ? Theme.accent : Theme.line }
        }
    }

    GridView {
        id: grid
        anchors.top: head.bottom; anchors.topMargin: 8
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.leftMargin: 6
        clip: true
        cellWidth: Math.floor((width - 6) / 2)
        cellHeight: cellWidth * 9 / 16 + 28
        model: (bb.adult ? bb.adultModel : bb.cg ? Engine.cgs : Engine.backgrounds).filter(b => !search.text || (b.id + " " + (b.title || "")).toLowerCase().indexOf(search.text.toLowerCase()) >= 0)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        cacheBuffer: 600
        delegate: Item {
            width: grid.cellWidth
            height: grid.cellHeight
            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                radius: 8
                color: Theme.bg
                border.color: area.containsMouse ? Theme.accent : Theme.line
                border.width: area.containsMouse ? 2 : 1
                clip: true
                Image {
                    x: 4; y: 4
                    width: parent.width - 8
                    height: width * 9 / 16
                    // «18+»: blurred until the mouse is on it (safe to open on a stream), sharp with the blur off
                    readonly property bool sharp: !bb.blur || area.containsMouse
                    source: bb.adult
                            ? (modelData.kind === "sprite"
                               ? (sharp ? "image://gb/thumb/" + encodeURIComponent(modelData.id) : "image://gb/blur/" + encodeURIComponent("sprite:" + modelData.id))
                               : (sharp ? "image://gb/cg/" : "image://gb/blur/") + encodeURIComponent(modelData.id))
                            : "image://gb/" + (bb.cg ? "cg/" : "bg/") + encodeURIComponent(modelData.id)
                    sourceSize: Qt.size(320, 180)
                    asynchronous: true
                    fillMode: bb.adult && modelData.kind !== "cg" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                }
                Rectangle {                     // the «18+» folder: every tile says so
                    visible: bb.adult
                    anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 9
                    width: badge.implicitWidth + 12; height: 20; radius: 10
                    color: "#c0392b"
                    Text {
                        id: badge
                        anchors.centerIn: parent
                        text: "18+"
                        color: "white"
                        font.pixelSize: 11; font.bold: true
                    }
                }
                Text {
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 5
                    x: 8; width: parent.width - 16
                    text: bb.adult && !modelData.have ? modelData.id + " · нужен патч" : (modelData.title || modelData.id) + (modelData.custom ? " ★" : "")
                    color: area.containsMouse ? Theme.text : Theme.dim
                    font.family: modelData.title ? Theme.ui : Theme.mono; font.pixelSize: 12
                    elide: Text.ElideRight
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    readonly property string cmd: bb.adult && modelData.kind === "sprite" ? "показать " + modelData.id + " center dissolve" : bb.cmdFor(modelData.id)
                    onEntered: bb.hoverCommand(cmd)
                    onExited: bb.hoverCommand("")
                    onClicked: { Sfx.click(); bb.insert(cmd) }
                }
            }
        }
    }

    FileDialog {
        id: imgDialog
        title: "Свой фон / CG (лучше 1920×1080)"
        nameFilters: ["Картинки (*.png *.jpg *.jpeg *.webp)"]
        onAccepted: {
            const f = decodeURIComponent(String(selectedFile)).split("/").pop().replace(/\.[^.]+$/, "")
            const n = Engine.importImage(selectedFile, bb.cg ? "cg" : "bg", f)
            if (n) bb.insert((bb.cg ? "цг " : "фон ") + n.substring(3) + " fade")
        }
    }
}
