import QtQuick
import QtQuick.Particles
import QtQuick.Shapes
import GenryBL

// Everlasting Summer's own title screen (screens.rpy: screen main_menu) rebuilt from
// its art: the INFORMATION board is an imagemap, each sheet lights up from
// mainmenu_hover.jpg exactly where the game's hotspots are - and each one opens a
// part of the constructor.
Item {
    id: menu
    width: 1920
    height: 1080
    signal pick(string what)
    property string timeOfDay: "day"       // day | sunset | night
    property bool intro: true
    property var lastProject: null
    property string hovered: ""
    property real mx: 0
    property real my: 0

    readonly property var spots: [
        { key: "new",      x: 439,  y: 265, w: 318, h: 621, title: "Новый мод",   sub: "начать новую историю" },
        { key: "mods",     x: 787,  y: 261, w: 270, h: 537, title: "Мои моды",    sub: "открыть, играть, делиться" },
        { key: "gallery",  x: 1083, y: 258, w: 229, h: 538, title: "Галерея",     sub: "все персонажи, фоны и CG лагеря" },
        { key: "settings", x: 1067, y: 748, w: 252, h: 312, title: "Инструменты", sub: "настройки конструктора" },
        { key: "quit",     x: 1459, y: 532, w: 149, h: 295, title: "Выход",       sub: "до завтра, пионер" },
        { key: "help",     x: 494,  y: 125, w: 768, h: 86,  title: "Информация",  sub: "как устроен конструктор" }
    ]

    // eased mouse (-1..1) + a slow idle float, so the board lives even when the mouse rests
    property real ex: 0
    property real ey: 0
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
        property real push: menu.intro ? 1.12 : 1.05
        scale: push
        x: -menu.ex * 18 + Math.sin(menu.floatT) * 4
        y: -menu.ey * 11 + Math.cos(menu.floatT * 0.7) * 3
        transform: [
            Rotation { origin.x: 960; origin.y: 540; axis { x: 0; y: 1; z: 0 } angle: menu.ex * 3.2 + Math.sin(menu.floatT * 0.5) * 0.4 },
            Rotation { origin.x: 960; origin.y: 540; axis { x: 1; y: 0; z: 0 } angle: -menu.ey * 2.2 }
        ]
        NumberAnimation on push { running: menu.intro; from: 1.12; to: 1.05; duration: 3200; easing.type: Easing.OutCubic }

        Image {
            source: "image://gb/file/images/gui/title_menu/mainmenu_ground.jpg"
            asynchronous: true
            smooth: true
        }

        // imagemap hover: the lit version of each sheet, only inside its hotspot - and the sheet
        // lifts off the board towards you (a soft shadow under it, a touch of tilt)
        Repeater {
            model: menu.spots
            Item {
                readonly property bool on: menu.hovered === modelData.key
                x: modelData.x; y: modelData.y
                width: modelData.w; height: modelData.h
                z: on ? 3 : 1
                opacity: on ? 1 : 0
                scale: on && modelData.key !== "help" ? 1.045 : 1.0
                rotation: on && modelData.key !== "help" ? (modelData.x % 2 ? 0.8 : -0.8) : 0
                Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }
                Behavior on rotation { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                Rectangle {
                    x: 10 + menu.ex * 6; y: 14 + menu.ey * 6
                    width: parent.width; height: parent.height
                    radius: 6
                    color: "#000000"
                    opacity: 0.28
                    visible: modelData.key !== "help"
                }
                Item {
                    anchors.fill: parent
                    clip: true
                    Image {
                        x: -modelData.x; y: -modelData.y
                        source: "image://gb/file/images/gui/title_menu/mainmenu_hover.jpg"
                        asynchronous: true
                        smooth: true
                    }
                }
            }
        }

        // a light that slides over the board with the mouse (the sun on the paper)
        Shape {
            z: 4
            width: 1400; height: 1400
            x: 960 - 700 + menu.ex * 520
            y: 540 - 700 + menu.ey * 320
            opacity: menu.timeOfDay === "night" ? 0.05 : 0.13
            Behavior on opacity { NumberAnimation { duration: 1200 } }
            ShapePath {
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: 700; centerY: 700; centerRadius: 700
                    focalX: 700; focalY: 700
                    GradientStop { position: 0.0; color: menu.timeOfDay === "sunset" ? "#ffd08a" : "#fffbea" }
                    GradientStop { position: 1.0; color: "transparent" }
                }
                startX: 0; startY: 0
                PathLine { x: 1400; y: 0 }
                PathLine { x: 1400; y: 1400 }
                PathLine { x: 0; y: 1400 }
                PathLine { x: 0; y: 0 }
            }
        }

        // the owl (ES shows it after an ending; here it always watches)
        Image {
            id: owl
            x: 135; y: 606
            source: owlArea.containsMouse ? "image://gb/file/images/gui/title_menu/owl_hover.png" : "image://gb/file/images/gui/title_menu/owl_idle.png"
            MouseArea {
                id: owlArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: Sfx.play("sound/test.ogg")
                onClicked: menu.pick("about")
            }
        }

        // sun rays through the leaves
        Repeater {
            model: 5
            Rectangle {
                readonly property real phase: index * 1.7
                x: 1250 + index * 110
                y: -260
                width: 90 + index * 26
                height: 1500
                rotation: 28 + index * 3
                transformOrigin: Item.Top
                opacity: menu.timeOfDay === "night" ? 0 : (0.10 + 0.07 * Math.sin(rayClock.t + phase))
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.35; color: menu.timeOfDay === "sunset" ? "#ffb35c" : "#fff4c2" }
                    GradientStop { position: 1.0; color: "transparent" }
                }
                Behavior on opacity { NumberAnimation { duration: 900 } }
            }
        }

        // time of day grading
        Rectangle {
            anchors.fill: parent
            color: "#ff7a2e"
            opacity: menu.timeOfDay === "sunset" ? 0.22 : 0
            Behavior on opacity { NumberAnimation { duration: 1200 } }
        }
        Rectangle {
            anchors.fill: parent
            color: "#07102e"
            opacity: menu.timeOfDay === "night" ? 0.58 : 0
            Behavior on opacity { NumberAnimation { duration: 1200 } }
        }

        // hotspots
        Repeater {
            model: menu.spots
            MouseArea {
                x: modelData.x; y: modelData.y
                width: modelData.w; height: modelData.h
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: {
                    menu.hovered = modelData.key
                    if (modelData.key === "quit") Sfx.play("sound/sfx/menu_gate.ogg")
                }
                onExited: if (menu.hovered === modelData.key) menu.hovered = ""
                onClicked: { Sfx.click(); menu.pick(modelData.key) }
            }
        }
    }

    QtObject {
        id: rayClock
        property real t: 0
        property NumberAnimation a: NumberAnimation { target: rayClock; property: "t"; from: 0; to: Math.PI * 2; duration: 9000; loops: Animation.Infinite; running: true }
    }

    // ---- falling leaves (ES's own leaf) / fireflies at night
    ParticleSystem { id: leaves }
    ImageParticle {
        system: leaves
        groups: ["leaf"]
        x: -menu.ex * 44          // nearer than the board: they move more
        y: -menu.ey * 26
        source: "image://gb/file/images/gui/settings/leaf.png"
        rotationVariation: 180
        rotationVelocity: 30
        rotationVelocityVariation: 60
        entryEffect: ImageParticle.Fade
        color: menu.timeOfDay === "night" ? "#5d7a5a" : menu.timeOfDay === "sunset" ? "#e2b04a" : "#ffffff"
    }
    Emitter {
        system: leaves
        group: "leaf"
        x: 0; y: -40
        width: 1920; height: 10
        emitRate: 1.3
        lifeSpan: 14000
        size: 34
        sizeVariation: 16
        velocity: AngleDirection { angle: 80; angleVariation: 25; magnitude: 70; magnitudeVariation: 30 }
    }
    Wander { system: leaves; groups: ["leaf"]; xVariance: 90; pace: 60; affectedParameter: Wander.Velocity }

    ParticleSystem { id: flies; running: menu.timeOfDay === "night" }
    ItemParticle {
        system: flies
        delegate: Rectangle {
            width: 7; height: 7; radius: 4
            color: "#e8ff8a"
            opacity: 0.85
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { to: 0.15; duration: 700 + Math.random() * 900 }
                NumberAnimation { to: 0.9; duration: 700 + Math.random() * 900 }
            }
        }
    }
    Emitter {
        system: flies
        x: 100; y: 600
        width: 1720; height: 460
        emitRate: 3
        lifeSpan: 7000
        velocity: AngleDirection { angleVariation: 180; magnitude: 14; magnitudeVariation: 10 }
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
        y: spot ? (spot.key === "settings" ? spot.y - 116 : spot.key === "help" ? spot.y + spot.h + 12 : spot.y + spot.h - 30) : y
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
            Text { text: "▶ Продолжить"; color: "#2f6b1c"; font.family: Theme.riffic; font.pixelSize: 30; font.bold: true }
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

    // ---- support: a note pinned over the gate - donations, the community, reviews
    component LinkChip: Rectangle {
        id: chip
        property string label
        property string url
        property color tint
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
            onClicked: { Sfx.click(); Qt.openUrlExternally(chip.url) }
        }
    }
    Item {
        x: 1486; y: 160
        width: 400; height: supportCol.implicitHeight + 38
        rotation: -1.5
        Rectangle { anchors.fill: parent; radius: 4; color: "#fbf5e1"; border.color: "#c9b58a" }
        Rectangle { x: parent.width / 2 - 9; y: -8; width: 18; height: 18; radius: 9; color: Theme.pioneer }
        Column {
            id: supportCol
            x: 22; y: 18
            width: parent.width - 44
            spacing: 9
            Text { text: "Поддержать GenryBL"; color: "#8a1f1a"; font.family: Theme.riffic; font.pixelSize: 28; font.bold: true }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "Программа бесплатная. Нравится — закинь на донат, так она и дальше будет расти. Лучше всего — первые три."
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
            Text { text: "Сообщество, отзывы, помощь"; color: "#5a3e1e"; font.family: Theme.ui; font.pixelSize: 19; font.bold: true }
            Flow {
                width: parent.width
                spacing: 8
                Repeater {
                    model: [["Discord", "https://discord.gg/2Yy45gJap3", "#5865f2"],
                            ["Telegram", "https://t.me/teamgenrythefox", "#229ed9"],
                            ["GitHub · отзывы и баги", "https://github.com/GenryTheFox0/GenryBL/issues", "#2d333b"]]
                    LinkChip { label: modelData[0]; url: modelData[1]; tint: modelData[2] }
                }
            }
        }
    }

    // ---- time of day switch (auto by the clock, click to change), under the 18+ plaque
    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 26
        anchors.topMargin: 86
        spacing: 10
        Repeater {
            model: [["day", "☀"], ["sunset", "◐"], ["night", "☾"]]
            Rectangle {
                width: 52; height: 52; radius: 26
                color: menu.timeOfDay === modelData[0] ? "#f4ecd2" : "#80101a0d"
                border.color: "#f4ecd2"
                Text { anchors.centerIn: parent; text: modelData[1]; font.pixelSize: 26; color: menu.timeOfDay === modelData[0] ? Theme.ink : "#f4ecd2" }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); menu.timeOfDay = modelData[0]; Engine.setSetting("menuTime", modelData[0]) } }
            }
        }
    }

    Text {
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 18
        text: "GenryBL " + Engine.version + " · конструктор модов «Бесконечного лета» · GenryTheFox"
        color: "#e8f0dc"
        style: Text.Outline
        styleColor: "#80000000"
        font.family: Theme.ui
        font.pixelSize: 20
    }

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
        NumberAnimation on opacity { running: menu.intro; from: 1; to: 0; duration: 1400; easing.type: Easing.InOutQuad }
        visible: opacity > 0.01
    }
}
