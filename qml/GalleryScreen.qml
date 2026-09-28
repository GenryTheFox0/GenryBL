import QtQuick
import QtQuick.Controls
import GenryBL

// "Галерея" = the camp's photo club: every pioneer (faces of every emotion), every
// background and CG of the game, straight from archive.rpa through THE renderer.
Item {
    id: gal
    width: 1920
    height: 1080
    signal back()
    property int tab: 0
    property string who: "dv"
    property string big: ""

    Image { anchors.fill: parent; source: "image://gb/file/images/gui/save_load/load_bg.jpg"; asynchronous: true }

    Row {
        x: 470; y: 180
        spacing: 26
        Repeater {
            model: ["Пионеры", "Фоны", "CG"]
            Text {
                text: modelData
                color: gal.tab === index ? "#9bd35a" : (tArea.containsMouse ? "#ffffff" : "#f1e7c8")
                font.family: Theme.riffic; font.pixelSize: 50; font.bold: true
                MouseArea { id: tArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); gal.tab = index } }
            }
        }
    }

    // pioneers
    Item {
        visible: gal.tab === 0
        x: 450; y: 270; width: 1140; height: 660
        ListView {
            id: castList
            width: parent.width; height: 118
            orientation: ListView.Horizontal
            spacing: 10
            clip: true
            model: Engine.cast
            delegate: Rectangle {
                width: 104; height: 112; radius: 8
                color: gal.who === modelData.id ? "#40ffffff" : "#20000000"
                border.color: gal.who === modelData.id ? modelData.color : "transparent"
                border.width: 2
                Image {
                    anchors.fill: parent; anchors.margins: 4; anchors.bottomMargin: 24
                    source: "image://gb/face/" + encodeURIComponent(modelData.id + " " + modelData.pose)
                    sourceSize: Qt.size(96, 96); asynchronous: true; fillMode: Image.PreserveAspectFit
                }
                Text { anchors.bottom: parent.bottom; anchors.bottomMargin: 3; anchors.horizontalCenter: parent.horizontalCenter; text: modelData.name; color: modelData.color; font.family: Theme.ui; font.pixelSize: 15; font.bold: true }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); gal.who = modelData.id } }
            }
        }
        GridView {
            y: 130
            width: parent.width; height: parent.height - 130
            clip: true
            cellWidth: 142; cellHeight: 168
            model: Engine.spriteNames(gal.who)
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                width: 142; height: 168
                Rectangle { anchors.fill: parent; anchors.margins: 4; radius: 8; color: fArea.containsMouse ? "#40ffffff" : "#18000000" }
                Image {
                    x: 10; y: 8; width: 122; height: 122
                    source: "image://gb/face/" + encodeURIComponent(gal.who + " " + modelData)
                    sourceSize: Qt.size(122, 122); asynchronous: true
                }
                Text { anchors.horizontalCenter: parent.horizontalCenter; y: 134; width: 134; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: modelData; color: "#f1e7c8"; font.family: Theme.mono; font.pixelSize: 14 }
                MouseArea { id: fArea; anchors.fill: parent; hoverEnabled: true; onEntered: gal.big = "sprite/" + gal.who + " " + modelData; onExited: gal.big = "" }
            }
        }
    }

    // backgrounds & CG
    GridView {
        visible: gal.tab > 0
        x: 450; y: 270; width: 1140; height: 660
        clip: true
        cellWidth: 285; cellHeight: 186
        model: gal.tab === 0 ? [] : gal.tab === 1 ? Engine.backgrounds : Engine.cgs
        ScrollBar.vertical: ScrollBar {}
        delegate: Item {
            width: 285; height: 186
            Image {
                x: 6; y: 6; width: 273; height: 154
                source: "image://gb/" + (gal.tab === 1 ? "bg/" : "cg/") + encodeURIComponent(modelData.id)
                sourceSize: Qt.size(273, 154); asynchronous: true; fillMode: Image.PreserveAspectCrop
                scale: bArea.containsMouse ? 1.04 : 1
                Behavior on scale { NumberAnimation { duration: 180 } }
            }
            Text { x: 8; y: 162; width: 270; elide: Text.ElideRight; text: modelData.id; color: "#f1e7c8"; font.family: Theme.mono; font.pixelSize: 14 }
            MouseArea { id: bArea; anchors.fill: parent; hoverEnabled: true; onEntered: gal.big = (gal.tab === 1 ? "bg/" : "cg/") + modelData.id; onExited: gal.big = "" }
        }
    }

    // the big look
    Rectangle {
        visible: gal.big !== ""
        x: gal.big.indexOf("sprite/") === 0 ? 1390 : 1180
        y: 120
        width: gal.big.indexOf("sprite/") === 0 ? 480 : 700
        height: gal.big.indexOf("sprite/") === 0 ? 900 : 394
        color: "#000000"; border.color: "#f1e7c8"; border.width: 3
        z: 5
        Image {
            anchors.fill: parent; anchors.margins: 3
            source: gal.big ? "image://gb/" + encodeURIComponent(gal.big) : ""
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            sourceSize: Qt.size(width, height)
        }
    }

    Text {
        x: 470; y: 960
        text: "‹ Назад"
        color: backArea.containsMouse ? "#9bd35a" : "#f1e7c8"
        font.family: Theme.riffic; font.pixelSize: 36; font.bold: true
        MouseArea { id: backArea; anchors.fill: parent; anchors.margins: -10; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); gal.back() } }
    }
}
