import QtQuick
import QtQuick.Particles
import QtQuick.Shapes
import QtQuick.Effects
import GenryBL

// The title screen: the INFORMATION board of «Совёнок» - alive (LiveBoard.qml: wind in the leaves, light,
// mist, lamps, stars, fireflies, butterflies, birds) and different at every time of day, by the clock of
// this computer or by choice. Each sheet of the board opens a part of the constructor: hovered, it lifts
// off the board towards you - with a shadow by day and a lantern's warm light at night.
Item {
    id: menu
    width: 1920
    height: 1080
    signal pick(string what)
    property bool intro: true
    property var lastProject: null
    // --shot launcher <png> night:new  -> the night menu with «Новый мод» lifted (screenshots)
    readonly property var shotBits: shotPage === "launcher" && shotArg ? shotArg.split(":") : []
    property string hovered: shotBits.length > 1 ? shotBits[1] : ""
    property real mx: 0
    property real my: 0

    // ---- time of day: the clock of this computer, or the choice («menuTime»: auto | morning | day | evening | night)
    property string timeSetting: shotBits.length ? shotBits[0] : Engine.setting("menuTime", "auto")
    function clockTime() {
        const h = new Date().getHours()
        return h >= 5 && h < 10 ? "morning" : h >= 10 && h < 18 ? "day" : h >= 18 && h < 22 ? "evening" : "night"
    }
    property string clock: clockTime()
    Timer { interval: 30000; running: true; repeat: true; onTriggered: menu.clock = menu.clockTime() }
    readonly property string timeOfDay: {
        const t = timeSetting === "sunset" ? "evening" : timeSetting          // «sunset» = the V1 name of the evening
        return !t || t === "auto" ? clock : t
    }
    readonly property string dayPick: Math.random() < 0.5 ? "day" : "day2"   // two pictures of the day, one per launch
    readonly property string variant: timeOfDay === "day" ? dayPick : timeOfDay
    onTimeOfDayChanged: if (shotPage === "") Ambience.play(timeOfDay)

    // the sheets of each picture (1920x1080): [x0, y0, x1, y1]
    readonly property var layouts: ({
        day:     { new: [314,238,661,840], mods: [679,267,981,832], gallery: [995,303,1267,825], help: [384,88,1208,232], about: [56,488,176,640], settings: [1056,888,1344,1072], quit: [1512,712,1656,880] },
        day2:    { new: [334,238,683,870], mods: [701,251,1002,860], gallery: [1019,283,1297,849], help: [408,96,1232,232], about: [88,512,205,672], settings: [1069,909,1352,1072], quit: [1533,733,1680,896] },
        evening: { new: [341,250,686,840], mods: [705,280,998,830], gallery: [1012,311,1270,820], help: [408,88,1232,244], about: [96,480,200,648], settings: [1064,880,1328,1072], quit: [1496,704,1648,880] },
        morning: { new: [322,258,674,859], mods: [691,291,990,849], gallery: [1006,325,1274,842], help: [392,96,1216,252], about: [88,456,200,608], settings: [1064,899,1328,1072], quit: [1517,728,1664,896] },
        night:   { new: [330,244,681,829], mods: [698,274,990,821], gallery: [1005,311,1271,813], help: [400,88,1232,238], about: [96,488,200,640], settings: [1064,864,1328,1072], quit: [1488,696,1632,856] }
    })
    readonly property var titles: [
        ["new",      qsTr("Новый мод"),   qsTr("начать новую историю")],
        ["mods",     qsTr("Мои моды"),    qsTr("открыть, играть, делиться")],
        ["gallery",  qsTr("Галерея"),     qsTr("все персонажи, фоны и CG лагеря")],
        ["settings", qsTr("Инструменты"), qsTr("настройки конструктора")],
        ["quit",     qsTr("Выход"),       qsTr("до завтра, пионер")],
        ["help",     qsTr("Информация"),  qsTr("как устроен конструктор")],
        ["about",    qsTr("Сова"),        qsTr("кто всё это сделал")]
    ]
    readonly property var spots: {
        const lay = layouts[variant] || layouts.day
        return titles.map(t => { const r = lay[t[0]]; return { key: t[0], title: t[1], sub: t[2], x: r[0], y: r[1], w: r[2] - r[0], h: r[3] - r[1] } })
    }

    // eased mouse (-1..1) + a slow idle float, so the board lives even when the mouse rests
    property real ex: 0
    property real ey: 0
    // the fade in starts when the picture is there (not a white box first), or after 2.5 s whatever happens
    property bool shown: board.ready || !menu.intro
    Timer { interval: 2500; running: menu.intro && !menu.shown; onTriggered: menu.shown = true }
    Behavior on ex { SmoothedAnimation { velocity: 1.6 } }
    Behavior on ey { SmoothedAnimation { velocity: 1.6 } }
    onMxChanged: ex = mx
    onMyChanged: ey = my
    property real floatT: 0
    NumberAnimation on floatT { from: 0; to: Math.PI * 2; duration: 14000; loops: Animation.Infinite; running: true }

    // ---- the board: real perspective tilt after the mouse, parallax and the intro push-in
    Item {
        id: world
        width: 1920
        height: 1080
        transformOrigin: Item.Center
        // the tilt after the mouse pulls the far edge in by ~8 %: the board is that much bigger, no black edge ever
        property real push: menu.intro ? 1.18 : 1.11
        scale: push
        x: -menu.ex * 18 + Math.sin(menu.floatT) * 4
        y: -menu.ey * 11 + Math.cos(menu.floatT * 0.7) * 3
        transform: [
            Rotation { origin.x: 960; origin.y: 540; axis { x: 0; y: 1; z: 0 } angle: menu.ex * 3.2 + Math.sin(menu.floatT * 0.5) * 0.4 },
            Rotation { origin.x: 960; origin.y: 540; axis { x: 1; y: 0; z: 0 } angle: -menu.ey * 2.2 }
        ]
        NumberAnimation on push { running: menu.intro && menu.shown; from: 1.18; to: 1.11; duration: 3200; easing.type: Easing.OutCubic }

        LiveBoard {
            id: board
            variant: menu.variant
            tod: menu.timeOfDay
            ex: menu.ex
            ey: menu.ey
            windTest: menu.shotBits.length > 2 && menu.shotBits[2] === "wind"
        }

        // a sheet lifts off the board towards you: its own pixels, a shadow by day, a lantern's glow at night,
        // and a band of light that crosses it once
        component LiftCard: Item {
            id: card
            property var spot
            readonly property bool on: menu.hovered === spot.key
            readonly property bool flat: spot.key === "help"
            readonly property bool night: menu.timeOfDay === "night"
            x: spot.x; y: spot.y
            width: spot.w; height: spot.h
            z: on ? 3 : 1
            opacity: on ? 1 : 0
            visible: opacity > 0.01
            scale: on ? (flat ? 1.03 : spot.key === "about" ? 1.14 : 1.065) : 1.0
            rotation: on && !flat ? (spot.x % 2 ? 0.9 : -0.9) : 0
            Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            Behavior on scale { NumberAnimation { duration: 300; easing.type: Easing.OutBack } }
            Behavior on rotation { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
            ShaderEffectSource {
                id: cardSrc
                anchors.fill: parent
                sourceItem: board.currentComp
                sourceRect: Qt.rect(card.spot.x, card.spot.y, card.spot.w, card.spot.h)
                live: card.visible
                visible: false
                smooth: true
            }
            MultiEffect {
                source: cardSrc
                anchors.fill: cardSrc
                autoPaddingEnabled: true
                shadowEnabled: true
                shadowColor: card.night ? "#ffc766" : "#000000"
                shadowOpacity: card.night ? 0.9 : (card.flat ? 0.35 : 0.75)
                shadowBlur: 1.0
                shadowHorizontalOffset: card.night ? 0 : 14 + menu.ex * 10
                shadowVerticalOffset: card.night ? 0 : 22 + menu.ey * 10
                brightness: card.night ? 0.22 : menu.timeOfDay === "evening" ? 0.10 : 0.09
                saturation: 0.06
            }
            Item {
                anchors.fill: parent
                clip: true
                Rectangle {
                    width: 150
                    height: card.height * 1.8
                    y: -card.height * 0.4
                    rotation: 18
                    x: card.on ? card.width + 220 : -320
                    Behavior on x { enabled: card.on; NumberAnimation { duration: 1000; easing.type: Easing.OutCubic } }
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.5; color: card.night ? "#50ffe2a8" : "#60ffffff" }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                }
            }
        }
        Repeater {
            model: menu.spots
            LiftCard { spot: modelData }
        }

        // a light that slides over the board with the mouse: the sun on the paper by day, a lantern at night
        Shape {
            z: 4
            width: 1400; height: 1400
            x: 960 - 700 + menu.ex * 520
            y: 540 - 700 + menu.ey * 320
            opacity: menu.timeOfDay === "night" ? 0.16 : menu.timeOfDay === "evening" ? 0.10 : 0.12
            Behavior on opacity { NumberAnimation { duration: 1200 } }
            ShapePath {
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: 700; centerY: 700; centerRadius: menu.timeOfDay === "night" ? 420 : 700
                    focalX: 700; focalY: 700
                    GradientStop { position: 0.0; color: menu.timeOfDay === "night" ? "#ffd48a" : menu.timeOfDay === "evening" ? "#ffd08a" : "#fffbea" }
                    GradientStop { position: 1.0; color: "transparent" }
                }
                startX: 0; startY: 0
                PathLine { x: 1400; y: 0 }
                PathLine { x: 1400; y: 1400 }
                PathLine { x: 0; y: 1400 }
                PathLine { x: 0; y: 0 }
            }
        }

        // the first look: a sweep of light runs across the board as the camp wakes up
        Rectangle {
            id: sweep
            z: 5
            width: 520; height: 2600
            y: -760
            x: -900
            rotation: 24
            visible: menu.intro
            opacity: 0.55
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: menu.timeOfDay === "night" ? "#40ffe0a0" : "#70fff6dc" }
                GradientStop { position: 1.0; color: "transparent" }
            }
            SequentialAnimation {
                running: menu.intro
                PauseAnimation { duration: 700 }
                NumberAnimation { target: sweep; property: "x"; from: -900; to: 2500; duration: 2600; easing.type: Easing.InOutQuad }
            }
        }

        // hotspots
        Repeater {
            model: menu.spots
            MouseArea {
                x: modelData.x; y: modelData.y
                width: modelData.w; height: modelData.h
                z: 6
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: {
                    menu.hovered = modelData.key
                    if (modelData.key === "quit") Sfx.play("sound/sfx/menu_gate.ogg")
                    else if (modelData.key === "about") Sfx.play("sound/test.ogg")
                }
                onExited: if (menu.hovered === modelData.key) menu.hovered = ""
                onClicked: { Sfx.click(); menu.pick(modelData.key) }
            }
        }
    }

    // ---- the pioneer tag that tells what a sheet does
    Item {
        id: tag
        readonly property var spot: {
            for (const s of menu.spots) if (s.key === menu.hovered) return s
            return null
        }
        visible: opacity > 0.01
        opacity: spot ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        x: spot ? Math.min(1920 - 420, Math.max(20, spot.x + spot.w / 2 - 190)) : x
        y: spot ? (spot.key === "settings" || spot.key === "quit" ? spot.y - 116 : spot.key === "help" || spot.key === "about" ? spot.y + spot.h + 12 : spot.y + spot.h - 170) : y
        Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        width: 380
        height: 100
        Rectangle {
            anchors.fill: parent
            radius: 6
            color: "#f4ecd2"
            border.color: "#b9a57a"
            rotation: -1.5
            Rectangle { x: 16; y: -10; width: 22; height: 44; color: Theme.pioneer; rotation: 12; radius: 2 }   // the red tie clip
        }
        Column {
            x: 52; y: 14
            Text { text: tag.spot ? tag.spot.title : ""; color: "#8a1f1a"; font.family: Theme.riffic; font.pixelSize: 34; font.bold: true }
            Text { text: tag.spot ? tag.spot.sub : ""; color: Theme.ink; font.family: Theme.ui; font.pixelSize: 20 }
        }
    }

    // ---- continue: a note pinned to the fence
    Item {
        visible: menu.lastProject !== null
        x: 40; y: 880
        width: 400; height: 120
        rotation: 2
        scale: contArea.containsMouse ? 1.05 : 1
        Behavior on scale { NumberAnimation { duration: 160 } }
        Rectangle { anchors.fill: parent; radius: 4; color: "#fbf5e1"; border.color: "#c9b58a" }
        Rectangle { x: parent.width / 2 - 9; y: -8; width: 18; height: 18; radius: 9; color: Theme.pioneer }
        Column {
            x: 22; y: 18
            spacing: 2
            Text { text: qsTr("▶ Продолжить"); color: "#2f6b1c"; font.family: Theme.riffic; font.pixelSize: 30; font.bold: true }
            Text {
                width: 360
                text: menu.lastProject ? "«" + menu.lastProject.name + "»" : ""
                color: Theme.ink; font.family: Theme.ui; font.pixelSize: 22
                elide: Text.ElideRight
            }
        }
        MouseArea {
            id: contArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { Sfx.click(); menu.pick("continue") }
        }
    }

    Component.onCompleted: {
        Engine.checkUpdates(false)                             // once a session (Updater.cpp), quietly
        if (shotPage === "") Ambience.play(timeOfDay)       // the camp's own sounds under the music
    }

    // ---- the last run fell: the crash catcher's report, one click to send it
    Rectangle {
        visible: Engine.lastCrash !== "" && shotPage === ""      // not on the screenshots of the menu
        z: 20
        x: 40; y: 150
        width: 560; height: crashCol.implicitHeight + 36
        radius: 10
        color: "#f7e7e2"; border.color: "#b3261e"; border.width: 2
        Column {
            id: crashCol
            x: 18; y: 18
            width: parent.width - 36
            spacing: 10
            Text { text: qsTr("В прошлый раз GenryBL вылетел"); color: "#8a1f1a"; font.family: Theme.riffic; font.pixelSize: 26; font.bold: true }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Прости. Отчёт о вылете сохранён — там видно, где именно. Скопируй его и кинь в Discord или GitHub: ") +
                      qsTr("по нему вылет чинится наверняка, а не наугад.")
                color: Theme.ink; font.family: Theme.ui; font.pixelSize: 17
            }
            Flow {
                width: parent.width
                spacing: 8
                LinkChip { label: qsTr("Скопировать отчёт"); tint: "#b3261e"; action: function() { Engine.copyText(Engine.crashText()); Engine.toast(qsTr("Отчёт скопирован — вставь его в сообщение"), 0) } }
                LinkChip { label: qsTr("Открыть папку"); tint: "#7a6440"; action: function() { Engine.revealFile(Engine.lastCrash) } }
                LinkChip { label: "Discord"; url: "https://discord.gg/2Yy45gJap3"; tint: "#5865f2" }
                LinkChip { label: "GitHub"; url: "https://github.com/GenryTheFox0/GenryBL/issues/new"; tint: "#2d333b" }
                LinkChip { label: qsTr("Закрыть"); tint: "#8b8b8b"; action: function() { Engine.crashSeen() } }
            }
        }
    }

    // ---- support: a note pinned over the gate - donations, the community, reviews
    component LinkChip: Rectangle {
        id: chip
        property string label
        property string url
        property color tint
        property var action: null                   // a function instead of a link («Обновить»)
        height: 40
        width: chipText.implicitWidth + 28
        radius: 20
        color: chipArea.containsMouse ? Qt.lighter(tint, 1.15) : tint
        border.color: Qt.darker(tint, 1.35)
        scale: chipArea.containsMouse ? 1.06 : 1
        Behavior on scale { NumberAnimation { duration: 140 } }
        Text { id: chipText; anchors.centerIn: parent; text: chip.label; color: "white"; font.family: Theme.ui; font.pixelSize: 19; font.bold: true }
        MouseArea {
            id: chipArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { Sfx.click(); if (chip.action) chip.action(); else Qt.openUrlExternally(chip.url) }
        }
    }
    Item {
        x: 1486; y: 184
        width: 400; height: supportCol.implicitHeight + 38
        rotation: -1.5
        Rectangle { anchors.fill: parent; radius: 4; color: "#fbf5e1"; border.color: "#c9b58a" }
        Rectangle { x: parent.width / 2 - 9; y: -8; width: 18; height: 18; radius: 9; color: Theme.pioneer }
        Column {
            id: supportCol
            x: 22; y: 18
            width: parent.width - 44
            spacing: 9
            Text { text: qsTr("Поддержать GenryBL"); color: "#8a1f1a"; font.family: Theme.riffic; font.pixelSize: 28; font.bold: true }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Программа бесплатная. Нравится — закинь на донат, так она и дальше будет расти. Лучше всего — первые три.")
                color: Theme.ink; font.family: Theme.ui; font.pixelSize: 18
            }
            Flow {
                width: parent.width
                spacing: 8
                Repeater {
                    model: [["DonatePay", "https://donatepay.ru/don/1411886", "#2f9e44"],
                            ["DonationAlerts", "https://www.donationalerts.com/r/genrythefoxmax", "#f57507"],
                            ["Boosty", "https://boosty.to/genrythefox", "#f15f2c"],
                            ["Patreon", "https://www.patreon.com/cw/GenryTheFox", "#8b8b8b"]]
                    LinkChip { label: modelData[0]; url: modelData[1]; tint: modelData[2] }
                }
            }
            Rectangle { width: parent.width; height: 1; color: "#c9b58a" }
            Text { text: qsTr("Сообщество, отзывы, помощь"); color: "#5a3e1e"; font.family: Theme.ui; font.pixelSize: 19; font.bold: true }
            Flow {
                width: parent.width
                spacing: 8
                Repeater {
                    model: [["Discord", "https://discord.gg/2Yy45gJap3", "#5865f2"],
                            ["Telegram", "https://t.me/teamgenrythefox", "#229ed9"],
                            [qsTr("Мастерская Steam"), "https://steamcommunity.com/sharedfiles/filedetails/?id=3809725763", "#1b2838"],
                            [qsTr("GitHub · отзывы и баги"), "https://github.com/GenryTheFox0/GenryBL/issues", "#2d333b"]]
                    LinkChip { label: modelData[0]; url: modelData[1]; tint: modelData[2] }
                }
            }
            // «Обновить»: the newest GenryBL from GitHub or the Steam Workshop, installed over this one (Updater.cpp)
            Rectangle { width: parent.width; height: 1; color: "#c9b58a" }
            Item {
                width: parent.width; height: 40
                readonly property var u: Engine.updateInfo
                readonly property bool working: u.state === "checking" || u.state === "downloading" || u.state === "starting"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Обновления"); color: "#5a3e1e"; font.family: Theme.ui; font.pixelSize: 19; font.bold: true
                }
                LinkChip {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    label: parent.u.state === "available" ? qsTr("Обновить до ") + parent.u.version
                         : parent.u.state === "checking" ? qsTr("Проверяю…")
                         : parent.u.state === "downloading" ? qsTr("Качаю ") + Math.round(parent.u.progress * 100) + "%"
                         : parent.u.state === "starting" ? qsTr("Ставлю…") : qsTr("Проверить")
                    tint: parent.u.state === "available" ? "#2f9e44" : "#7a6440"
                    action: function() {
                        if (parent.u.state === "available") Engine.startUpdate()
                        else if (!parent.working) Engine.checkUpdates(true)
                    }
                }
            }
            Text {
                visible: text !== ""
                width: parent.width
                wrapMode: Text.Wrap
                text: Engine.updateInfo.text || ""
                color: Engine.updateInfo.state === "error" ? "#b3261e" : Theme.ink
                font.family: Theme.ui; font.pixelSize: 17
            }
            Rectangle {
                visible: Engine.updateInfo.state === "downloading"
                width: parent.width; height: 8; radius: 4; color: "#e3d6b2"
                Rectangle { width: parent.width * (Engine.updateInfo.progress || 0); height: parent.height; radius: 4; color: "#2f9e44" }
            }
        }
    }

    // ---- time of day: by the clock of this computer (auto) or chosen; under the 18+ plaque
    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 26
        anchors.topMargin: 86
        spacing: 10
        // the language: a flag, a click opens all 20
        Column {
            spacing: 2
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 64; height: 52; radius: 12
                color: "#80101a0d"
                border.color: "#f4ecd2"
                scale: langArea.containsMouse ? 1.1 : 1
                Behavior on scale { NumberAnimation { duration: 140 } }
                Image {
                    anchors.centerIn: parent
                    width: 48; height: 32; smooth: true; mipmap: true
                    source: "file:///" + Engine.appRoot + "/data/flags/" + Engine.language + ".png"
                }
                MouseArea { id: langArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { Sfx.click(); langPicker.open() } }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("язык")
                color: "#f4ecd2"; style: Text.Outline; styleColor: "#a0000000"
                font.family: Theme.ui; font.pixelSize: 15
            }
        }
        Item { width: 8; height: 1 }
        Repeater {
            model: [["auto", "⟳", qsTr("по часам")], ["morning", "☼", qsTr("утро")], ["day", "☀", qsTr("день")], ["evening", "◐", qsTr("вечер")], ["night", "☾", qsTr("ночь")]]
            Column {
                id: tChip
                spacing: 2
                readonly property bool chosen: modelData[0] === "auto" ? (menu.timeSetting === "auto" || !menu.timeSetting)
                                                                   : menu.timeSetting === modelData[0] || (modelData[0] === "evening" && menu.timeSetting === "sunset")
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 52; height: 52; radius: 26
                    color: tChip.chosen ? "#f4ecd2" : "#80101a0d"
                    border.color: "#f4ecd2"
                    scale: tArea.containsMouse ? 1.1 : 1
                    Behavior on scale { NumberAnimation { duration: 140 } }
                    Text { anchors.centerIn: parent; text: modelData[1]; font.pixelSize: 26; color: tChip.chosen ? Theme.ink : "#f4ecd2" }
                    MouseArea {
                        id: tArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { Sfx.click(); menu.timeSetting = modelData[0]; Engine.setSetting("menuTime", modelData[0]) }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: modelData[2]
                    color: "#f4ecd2"; style: Text.Outline; styleColor: "#a0000000"
                    font.family: Theme.ui; font.pixelSize: 15
                }
            }
        }
    }

    Text {
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 18
        text: "GenryBL " + Engine.version + qsTr(" · конструктор модов «Бесконечного лета» · GenryTheFox")
        color: "#e8f0dc"
        style: Text.Outline
        styleColor: "#80000000"
        font.family: Theme.ui
        font.pixelSize: 20
    }

    LanguagePicker { id: langPicker }

    HoverHandler {
        onPointChanged: {
            menu.mx = (point.position.x / menu.width - 0.5) * 2
            menu.my = (point.position.y / menu.height - 0.5) * 2
        }
    }

    Rectangle {
        id: fadeIn
        anchors.fill: parent
        color: "black"
        opacity: menu.intro ? 1 : 0
        NumberAnimation on opacity { running: menu.intro && menu.shown; from: 1; to: 0; duration: 1400; easing.type: Easing.InOutQuad }
        visible: opacity > 0.01
    }
}
