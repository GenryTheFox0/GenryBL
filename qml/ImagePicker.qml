import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// Pick one picture of the game: a character pose (face grid -> that character's poses),
// a background or a CG. Emits the Ren'Py image name: "sl smile pioneer", "bg ext_beach_day", "cg ...".
Popup {
    id: ip
    signal picked(string image)
    property int tab: 0                  // 0 characters, 1 backgrounds, 2 CG
    property string tag: ""              // character opened on tab 0
    property string tagName: ""
    // «гардероб мастерской»: "" = the game's own sprites, else a workshop outfit
    property string wOutfit: ""
    readonly property var wOutfits: (Engine.wardrobeState === 2 && tag ? Engine.wardrobeOutfits(tag).filter(o => String(Engine.setting("wardrobeAdult", true)) !== "false" || !o.adult) : [])
    onTagChanged: wOutfit = ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 120, 1180)
    height: Math.min(parent.height - 90, 800)
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    onOpened: { search.text = ""; search.forceActiveFocus() }

    function choose(img) { picked(img); close() }
    function matches(s) { return !search.text || s.toLowerCase().indexOf(search.text.toLowerCase()) >= 0 }

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.accent }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: ["Персонажи", "Фоны", "CG"]
                PillButton { text: modelData; accent: ip.tab === index; dark: ip.tab !== index; onClicked: { ip.tab = index; ip.tag = "" } }
            }
            TextField {
                id: search
                Layout.fillWidth: true
                implicitHeight: 32
                placeholderText: ip.tab === 0 && ip.tag ? "Эмоция или одежда…" : "Поиск…"
                placeholderTextColor: Theme.faint
                color: Theme.text
                font.family: Theme.ui; font.pixelSize: 14
                selectByMouse: true
                background: Rectangle { radius: 16; color: Theme.bg; border.color: search.activeFocus ? Theme.accent : Theme.line }
            }
            PillButton { dark: true; text: "✕"; onClicked: ip.close() }
        }
        RowLayout {
            visible: ip.tab === 0 && ip.tag !== ""
            spacing: 10
            PillButton { dark: true; text: "← Все персонажи"; onClicked: { ip.tag = ""; search.text = "" } }
            InkText { text: ip.tagName; size: 24; color: Theme.charColor(ip.tag) }
            Item { width: 18; height: 1 }
            Text { visible: ip.wOutfits.length > 0; text: "Гардероб мастерской:"; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14 }
            DarkCombo {
                visible: ip.wOutfits.length > 0
                width: 260
                model: ["одежда игры"].concat(ip.wOutfits.map(o => (o.adult ? "18+ " : "") + o.id + (o.dists.indexOf("") < 0 ? " (" + (o.dists[0] === "close" ? "близко" : "далеко") + ")" : "")))
                currentIndex: ip.wOutfit ? ip.wOutfits.findIndex(o => o.id === ip.wOutfit) + 1 : 0
                onActivated: (i) => ip.wOutfit = i > 0 ? ip.wOutfits[i - 1].id : ""
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            cacheBuffer: 800
            readonly property bool faces: ip.tab === 0
            cellWidth: faces ? Math.floor(width / Math.max(1, Math.floor(width / 150))) : Math.floor(width / Math.max(1, Math.floor(width / 270)))
            cellHeight: faces ? cellWidth + 34 : cellWidth * 9 / 16 + 34
            model: {
                if (ip.tab === 0 && !ip.tag) return Engine.cast.filter(c => ip.matches(c.name) || ip.matches(c.id))
                if (ip.tab === 0 && ip.wOutfit) {
                    const o = ip.wOutfits.find(x => x.id === ip.wOutfit)
                    const d = o && o.dists.indexOf("") < 0 ? o.dists[0] : ""
                    return Engine.wardrobeLooks(ip.tag, ip.wOutfit, d).filter(n => ip.matches(n))
                        .map(n => ({ id: n + (d ? " " + d : ""), image: ip.tag + " " + n + (d ? " " + d : "") }))
                }
                if (ip.tab === 0) return Engine.spriteNames(ip.tag).filter(n => ip.matches(n)).map(n => ({ id: n, image: ip.tag + " " + n }))
                return (ip.tab === 1 ? Engine.backgrounds : Engine.cgs).filter(b => ip.matches(b.id))
            }
            delegate: Item {
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                readonly property string thumb: {
                    if (ip.tab === 0 && !ip.tag) return "image://gb/face/" + encodeURIComponent(modelData.id + " " + modelData.pose)
                    if (ip.tab === 0) return "image://gb/face/" + encodeURIComponent(modelData.image)
                    return "image://gb/" + (ip.tab === 1 ? "bg/" : "cg/") + encodeURIComponent(modelData.id)
                }
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
                        height: grid.faces ? width : width * 9 / 16
                        source: parent.parent.thumb
                        sourceSize: grid.faces ? Qt.size(180, 180) : Qt.size(320, 180)
                        asynchronous: true
                        fillMode: grid.faces ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                    }
                    Rectangle {
                        visible: !!modelData.patch
                        anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 9
                        width: 36; height: 20; radius: 10
                        color: "#c0392b"
                        Text { anchors.centerIn: parent; text: "18+"; color: "white"; font.pixelSize: 11; font.bold: true }
                    }
                    Text {
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 6
                        x: 8; width: parent.width - 16
                        horizontalAlignment: Text.AlignHCenter
                        text: ip.tab === 0 && !ip.tag ? modelData.name : modelData.id
                        color: ip.tab === 0 && !ip.tag ? modelData.color : (area.containsMouse ? Theme.text : Theme.dim)
                        font.family: ip.tab === 0 && !ip.tag ? Theme.ui : Theme.mono
                        font.pixelSize: ip.tab === 0 && !ip.tag ? 15 : 12
                        font.bold: ip.tab === 0 && !ip.tag
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            Sfx.click()
                            if (ip.tab === 0 && !ip.tag) { ip.tag = modelData.id; ip.tagName = modelData.name; search.text = "" }
                            else if (ip.tab === 0) ip.choose(modelData.image)
                            else ip.choose((ip.tab === 1 ? "bg " : "cg ") + modelData.id)
                        }
                    }
                }
            }
        }
    }
}
