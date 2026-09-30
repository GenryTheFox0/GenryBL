import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import GenryBL

// The cast of Everlasting Summer: cards that flip through the pioneer's real emotions on
// hover, every sprite (emotion x outfit x distance) as a face, hover = see it on stage.
Item {
    id: cb
    signal insert(string cmd)
    signal hoverCommand(string cmd)
    property string tag: "dv"
    property string outfit: ""
    property string dist: ""               // "" | close | far
    property string pos: "center"
    property string trans: "dissolve"
    // Engine.cast / wardrobeState are read for real (a bare "(a, b)" read is compiled away and never re-evaluates)
    property int rev: 0                    // bumped on every deletion / face fix: the lists and faces reload
    Connections {
        target: Engine
        function onWardrobeChanged() { cb.rev++ }
    }
    readonly property var names: Engine.cast.length >= 0 && cb.rev >= 0 ? Engine.spriteNames(tag) : []
    readonly property var outfits: Engine.cast.length >= 0 && cb.rev >= 0 ? Engine.spriteOutfits(tag) : []
    readonly property var hidden: cb.rev >= 0 ? Engine.hiddenSprites(tag) : []
    // «гардероб мастерской»: workshop outfits on the game's bodies (18+ ones can be hidden)
    property bool wardrobeOpen: false
    property bool showAdult: String(Engine.setting("wardrobeAdult", true)) !== "false"
    readonly property var wOutfits: Engine.wardrobeState === 2 && cb.rev >= 0 ? Engine.wardrobeOutfits(tag) : []
    readonly property var wShown: wOutfits.filter(o => showAdult || !o.adult)
    readonly property bool wardrobeOutfit: wOutfits.findIndex(o => o.id === outfit) >= 0
    readonly property var shown: {
        if (cb.rev < 0) return []
        if (wardrobeOutfit) return Engine.wardrobeLooks(tag, outfit, dist)
        const es = names.filter(n => !outfit || n.split(" ").slice(1).join(" ") === outfit)
        // the workshop's extra faces with the game's own outfit ("evil_smile pioneer")
        const extra = outfit && Engine.wardrobeState === 2 ? Engine.wardrobeLooks(tag, outfit, dist).filter(n => es.indexOf(n) < 0) : []
        return es.concat(extra)
    }
    readonly property string displayName: {
        for (const c of Engine.cast) if (c.id === tag) return c.name
        return tag
    }
    function cmdFor(name) {
        return "показать " + tag + (name ? " " + name : "") + (dist ? " " + dist : "") + " " + pos + (trans ? " " + trans : "")
    }
    onTagChanged: {
        outfit = outfits.indexOf("pioneer") >= 0 ? "pioneer" : ""
        dist = ""
    }
    Component.onCompleted: outfit = outfits.indexOf("pioneer") >= 0 ? "pioneer" : ""

    Column {
        id: head
        x: 10; y: 10
        width: parent.width - 20
        spacing: 8

        // ---- cast cards
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: Engine.cast
                delegate: Rectangle {
                    id: card
                    width: (head.width - 6 * 5) / 6
                    height: width * 1.25
                    radius: 8
                    color: cb.tag === modelData.id ? Qt.rgba(1, 1, 1, 0.08) : Theme.bg
                    border.color: cb.tag === modelData.id || cardArea.containsMouse ? modelData.color : Theme.line
                    border.width: cb.tag === modelData.id ? 2 : 1
                    clip: true
                    property string face: modelData.pose
                    property var pool: []
                    SmoothImage {
                        anchors.fill: parent
                        anchors.bottomMargin: 18
                        source: "image://gb/face/" + encodeURIComponent(modelData.id + (card.face ? " " + card.face : ""))
                        sourceSize: Qt.size(110, 110)
                        fade: 90
                    }
                    Text {
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.name
                        color: modelData.color
                        font.family: Theme.ui; font.pixelSize: 12; font.bold: true
                        elide: Text.ElideRight
                        width: parent.width - 4
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Timer {
                        running: cardArea.containsMouse && !modelData.custom
                        interval: 480; repeat: true; triggeredOnStart: true
                        onTriggered: {
                            if (!card.pool.length) {
                                const all = Engine.spriteNames(modelData.id).filter(n => n.indexOf("pioneer") >= 0 || n.split(" ").length === 1)
                                for (let i = all.length - 1; i > 0; --i) { const j = Math.floor(Math.random() * (i + 1)); [all[i], all[j]] = [all[j], all[i]] }
                                card.pool = all
                            }
                            card.face = card.pool.pop() || modelData.pose
                        }
                        onRunningChanged: if (!running) card.face = modelData.pose
                    }
                    MouseArea {
                        id: cardArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { Sfx.click(); cb.tag = modelData.id }
                    }
                }
            }
        }

        // ---- outfits & distance
        Flow {
            width: parent.width
            spacing: 5
            PillButton { dark: cb.outfit !== ""; accent: cb.outfit === ""; text: qsTr("все"); onClicked: cb.outfit = "" }
            Repeater {
                model: cb.outfits
                PillButton {
                    dark: cb.outfit !== modelData; accent: cb.outfit === modelData; text: modelData; onClicked: cb.outfit = modelData
                    MouseArea { anchors.fill: parent; acceptedButtons: Qt.RightButton; onClicked: cb.askOutfit(modelData) }
                }
            }
        }
        Row {
            spacing: 5
            visible: Engine.wardrobeState !== 0
            PillButton {
                dark: !cb.wardrobeOpen && !cb.wardrobeOutfit
                accent: cb.wardrobeOpen || cb.wardrobeOutfit
                enabled: Engine.wardrobeState === 2 && cb.wOutfits.length > 0
                text: Engine.wardrobeState !== 2 ? qsTr("Гардероб мастерской грузится…")
                    : cb.wOutfits.length ? qsTr("Гардероб мастерской · ") + cb.wShown.length + (cb.wardrobeOpen ? " ▴" : " ▾") : qsTr("В мастерской нет одежды")
                onClicked: cb.wardrobeOpen = !cb.wardrobeOpen
            }
            PillButton {
                visible: cb.hidden.length > 0
                dark: true
                text: qsTr("Удалённые · ") + cb.hidden.length
                onClicked: restoreMenu.popup()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Что ты удалил у этого персонажа — клик по строке возвращает")
            }
            PillButton {
                visible: cb.wardrobeOpen && cb.wOutfits.some(o => o.adult)
                dark: true
                text: cb.showAdult ? qsTr("18+ показаны") : qsTr("18+ скрыты")
                onClicked: {
                    cb.showAdult = !cb.showAdult
                    Engine.setSetting("wardrobeAdult", cb.showAdult)
                    if (!cb.showAdult && cb.wOutfits.some(o => o.id === cb.outfit && o.adult)) cb.outfit = ""
                }
            }
        }
        Flickable {
            visible: cb.wardrobeOpen
            width: parent.width
            height: Math.min(wflow.implicitHeight, 150)
            contentHeight: wflow.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Flow {
                id: wflow
                width: parent.width - 14
                spacing: 5
                Repeater {
                    model: cb.wShown
                    PillButton {
                        required property var modelData
                        dark: cb.outfit !== modelData.id
                        accent: cb.outfit === modelData.id
                        // ◎ = drawn only up close / far away
                        text: (modelData.adult ? "18+ " : "") + modelData.id + (modelData.dists.indexOf("") < 0 ? " ◎" : "")
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("из «") + modelData.title + "»" + (modelData.body ? qsTr(" · своё тело") : "") +
                                      (modelData.dists.indexOf("") < 0 ? qsTr(" · только ") + (modelData.dists[0] === "close" ? qsTr("близко") : qsTr("далеко")) : "")
                        onClicked: {
                            cb.outfit = modelData.id
                            if (modelData.dists.indexOf(cb.dist) < 0) cb.dist = modelData.dists[0]
                        }
                        MouseArea { anchors.fill: parent; acceptedButtons: Qt.RightButton; onClicked: cb.askOutfit(modelData.id) }
                    }
                }
            }
        }
        Row {
            spacing: 5
            Repeater {
                model: [["", qsTr("обычно")], ["close", qsTr("близко")], ["far", qsTr("далеко")]]
                PillButton { dark: cb.dist !== modelData[0]; accent: cb.dist === modelData[0]; text: modelData[1]; onClicked: cb.dist = modelData[0] }
            }
        }
        Row {
            spacing: 5
            Repeater {
                model: [["fleft", qsTr("край ◂")], ["left", qsTr("слева")], ["center", qsTr("центр")], ["right", qsTr("справа")], ["fright", qsTr("▸ край")]]
                PillButton { dark: cb.pos !== modelData[0]; accent: cb.pos === modelData[0]; text: modelData[1]; onClicked: cb.pos = modelData[0] }
            }
        }
        Row {
            spacing: 6
            DarkCombo {
                width: 150
                model: ["dissolve", qsTr("без перехода"), "fade", "dspr", "moveinleft", "moveinright", "hpunch"]
                onActivated: (i) => cb.trans = i === 1 ? "" : model[i]
            }
            PillButton { dark: true; text: qsTr("Реплика"); onClicked: cb.insert(cb.displayName + ": ") }
            PillButton { dark: true; text: qsTr("Убрать"); onClicked: cb.insert("убрать " + cb.tag + " dissolve") }
            PillButton { accent: true; text: qsTr("＋ Свой"); onClicked: pngDialog.open() }
        }
        Text {
            text: cb.displayName + " · " + cb.shown.length + qsTr(" спрайтов · наведи — увидишь на сцене")
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13
        }
    }

    GridView {
        id: grid
        anchors.top: head.bottom; anchors.topMargin: 6
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.leftMargin: 6
        clip: true
        cellWidth: Math.floor((width - 6) / Math.max(1, Math.floor((width - 6) / 104)))
        cellHeight: 128
        model: cb.shown
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        cacheBuffer: 800
        delegate: Item {
            width: grid.cellWidth
            height: grid.cellHeight
            Rectangle {
                anchors.fill: parent
                anchors.margins: 3
                radius: 8
                color: em.containsMouse ? Theme.panel3 : Theme.bg
                border.color: em.containsMouse ? Theme.charColor(cb.tag) : "transparent"
                Image {
                    anchors.fill: parent
                    anchors.bottomMargin: 20
                    source: "image://gb/face/" + encodeURIComponent(cb.tag + (modelData ? " " + modelData : "") + (cb.dist ? " " + cb.dist : "")) + "#" + cb.rev
                    sourceSize: Qt.size(116, 116)
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                }
                Text {
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 3
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 6
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: modelData.split(" ")[0]
                    color: em.containsMouse ? Theme.text : Theme.dim
                    font.family: Theme.mono; font.pixelSize: 12
                }
                MouseArea {
                    id: em
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onEntered: cb.hoverCommand(cb.cmdFor(modelData))
                    onExited: cb.hoverCommand("")
                    onClicked: (m) => {
                        if (m.button === Qt.RightButton) { tileMenu.name = modelData; tileMenu.popup(); return }
                        Sfx.click(); cb.insert(cb.cmdFor(modelData))
                    }
                }
                // a sprite the wardrobe puts together: its face can be moved by hand
                readonly property bool wardrobeSprite: Engine.wardrobeState === 2 && cb.rev >= 0 &&
                                                       Engine.wardrobeKnows(cb.tag + " " + modelData + (cb.dist ? " " + cb.dist : ""))
                Row {
                    anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 6
                    spacing: 4
                    visible: em.containsMouse || fixArea.containsMouse || delArea.containsMouse
                    Rectangle {
                        visible: parent.parent.wardrobeSprite
                        width: 24; height: 24; radius: 12
                        color: fixArea.containsMouse ? Theme.accent : "#cc1b2330"
                        Text { anchors.centerIn: parent; text: "✎"; color: "white"; font.pixelSize: 13 }
                        MouseArea { id: fixArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: faceFix.openFor(cb.tag, modelData, cb.dist) }
                        ToolTip.visible: fixArea.containsMouse
                        ToolTip.text: qsTr("Поправить лицо вручную")
                    }
                    Rectangle {
                        width: 24; height: 24; radius: 12
                        color: delArea.containsMouse ? "#c0392b" : "#cc1b2330"
                        Text { anchors.centerIn: parent; text: "✕"; color: "white"; font.pixelSize: 12; font.bold: true }
                        MouseArea { id: delArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Engine.hideSprite(cb.tag, modelData) }
                        ToolTip.visible: delArea.containsMouse
                        ToolTip.text: qsTr("Удалить этот спрайт навсегда (правый клик — ещё варианты)")
                    }
                }
            }
        }
    }

    function fixFace(t, n, d) { faceFix.openFor(t, n, d) }
    function askOutfit(o) { outfitMenu.outfit = o; outfitMenu.popup() }
    Menu {
        id: outfitMenu
        property string outfit
        MenuItem { text: qsTr("Удалить наряд «") + outfitMenu.outfit + qsTr("» целиком"); onTriggered: { Engine.hideOutfit(cb.tag, outfitMenu.outfit); if (cb.outfit === outfitMenu.outfit) cb.outfit = "" } }
    }
    Menu {
        id: tileMenu
        property string name
        readonly property string emo: name.split(" ")[0]
        readonly property string outf: name.split(" ").slice(1).join(" ")
        MenuItem {
            text: qsTr("✎ Поправить лицо…")
            enabled: Engine.wardrobeState === 2 && Engine.wardrobeKnows(cb.tag + " " + tileMenu.name + (cb.dist ? " " + cb.dist : ""))
            onTriggered: faceFix.openFor(cb.tag, tileMenu.name, cb.dist)
        }
        MenuItem { text: qsTr("✕ Удалить спрайт «") + tileMenu.name + "»"; onTriggered: Engine.hideSprite(cb.tag, tileMenu.name) }
        MenuItem { text: qsTr("Удалить эмоцию «") + tileMenu.emo + qsTr("» у всех нарядов"); onTriggered: Engine.hideEmotion(cb.tag, tileMenu.emo) }
        MenuItem { text: qsTr("Удалить наряд «") + tileMenu.outf + qsTr("» целиком"); enabled: tileMenu.outf !== ""; onTriggered: { Engine.hideOutfit(cb.tag, tileMenu.outf); if (cb.outfit === tileMenu.outf) cb.outfit = "" } }
    }
    Menu {
        id: restoreMenu
        Instantiator {
            model: cb.hidden
            delegate: MenuItem {
                required property var modelData
                text: "↺ " + modelData.label
                onTriggered: Engine.unhideSprite(modelData.key)
            }
            onObjectAdded: (i, o) => restoreMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => restoreMenu.removeItem(o)
        }
    }
    FaceFixDialog { id: faceFix }
    FileDialog {
        id: pngDialog
        title: qsTr("Свой спрайт (PNG с прозрачностью, лучше 900×1080)")
        nameFilters: [qsTr("Картинки (*.png *.webp)")]
        onAccepted: { nameField.text = ""; nameDialog.file = selectedFile; nameDialog.open() }
    }
    Dialog {
        id: nameDialog
        property url file
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Как зовут и какая эмоция?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Column {
            spacing: 8
            Label { width: 380; wrapMode: Text.Wrap; text: qsTr("Например «вожатый smile» → команда «показать vozhatyy smile center».\nПервое слово — персонаж, дальше — эмоция/одежда.") }
            TextField { id: nameField; width: 380; placeholderText: qsTr("Имя эмоция"); selectByMouse: true; onAccepted: nameDialog.accept() }
        }
        onAccepted: {
            const n = Engine.importImage(file, "sprite", nameField.text)
            if (n) cb.insert("показать " + n + " " + cb.pos + " dissolve")
        }
    }
}
