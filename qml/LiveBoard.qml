import QtQuick
import QtQuick.Particles
import QtQuick.Shapes
import GenryBL

// The living camp behind the INFORMATION board of the menu: one picture per time of day
// (data/menu/board_*.jpg), cross-faded; wind runs through its leaves, grass and flowers (a shader with
// a mask per picture - data/menu/wind_*.png - the board, its text, the fence and the statue stand still);
// the sun or the moon glows, the lamps flicker with moths around them, mist crawls in the morning,
// stars twinkle and fall at night, fireflies, dust in the sunbeams, butterflies, birds, falling leaves
// and a gust of wind now and then.
Item {
    id: board
    readonly property bool ready: imgA.status === Image.Ready
    width: 1920
    height: 1080
    property string variant: "day"          // the picture: morning | day | day2 | evening | night
    property string tod: "day"              // the time of day: morning | day | evening | night
    property real ex: 0                     // the mouse, -1..1 - the near layers move more (depth)
    property real ey: 0
    property bool windTest: false           // screenshots: a storm, to see the wind at a glance
    readonly property string currentUrl: url(variant)
    // not Russian: the pictures without their painted Russian words + BoardText writes them in the language
    readonly property bool localized: Engine.language !== "ru"
    // the picture with its words, as the wind shader sees it - the menu lifts its sheets out of this
    readonly property Item currentComp: mixv < 0.5 ? compA : compB

    function url(v) { return "file:///" + Engine.appRoot + "/data/menu/board_" + v + (localized ? "_clean" : "") + ".jpg" }
    function maskUrl(v) { return "file:///" + Engine.appRoot + "/data/menu/wind_" + v + ".png" }

    // per picture: where its sun (or moon) is and its lamps
    readonly property var light: ({
        morning: { sun: [1740, 408], lamps: [[1786, 494]] },
        day:     { sun: [1850, 250], lamps: [] },
        day2:    { sun: [1900, 60],  lamps: [] },
        evening: { sun: [1725, 360], lamps: [[1768, 464]] },
        night:   { sun: [1645, 104], lamps: [[320, 120], [1272, 232], [1760, 472], [1408, 568], [1664, 424]] }
    })
    readonly property var lit: light[variant] || light.day
    readonly property bool isNight: tod === "night"
    readonly property bool isEvening: tod === "evening"
    readonly property bool isMorning: tod === "morning"
    readonly property bool isDay: tod === "day"
    readonly property color sunColor: isNight ? "#c9dcff" : isEvening ? "#ffb04f" : isMorning ? "#ffc4a4" : "#fff1c2"

    // ---- the picture: two slots cross-faded inside the wind shader --------------------------------
    property string aVar: ""
    property string bVar: ""
    property real mixv: 0
    property real pendingTo: -1
    property bool inited: false
    Component.onCompleted: { aVar = variant; bVar = variant; mixv = 0; inited = true }
    onVariantChanged: {
        if (!inited) return
        if (mixv < 0.5) { bVar = variant; pendingTo = 1 } else { aVar = variant; pendingTo = 0 }
        tryFade()
    }
    function tryFade() {
        if (pendingTo < 0) return
        const img = pendingTo === 1 ? imgB : imgA
        const msk = pendingTo === 1 ? mB : mA
        if (img.status === Image.Ready && msk.status !== Image.Loading) { fade.to = pendingTo; fade.restart(); pendingTo = -1 }
    }
    NumberAnimation { id: fade; target: board; property: "mixv"; duration: 2400; easing.type: Easing.InOutQuad }

    // each slot = its picture + its words, rendered into a texture for the shader
    Item {
        id: compA
        width: 1920; height: 1080
        Image { id: imgA; anchors.fill: parent; source: board.aVar ? board.url(board.aVar) : ""; asynchronous: true; onStatusChanged: board.tryFade() }
        BoardText { variant: board.aVar || "day"; visible: board.localized }
    }
    Item {
        id: compB
        width: 1920; height: 1080
        Image { id: imgB; anchors.fill: parent; source: board.bVar ? board.url(board.bVar) : ""; asynchronous: true; onStatusChanged: board.tryFade() }
        BoardText { variant: board.bVar || "day"; visible: board.localized }
    }
    ShaderEffectSource { id: texA; sourceItem: compA; hideSource: true; visible: false; smooth: true }
    ShaderEffectSource { id: texB; sourceItem: compB; hideSource: true; visible: false; smooth: true }
    Image { id: mA; source: board.aVar ? board.maskUrl(board.aVar) : ""; visible: false; asynchronous: true; onStatusChanged: board.tryFade() }
    Image { id: mB; source: board.bVar ? board.maskUrl(board.bVar) : ""; visible: false; asynchronous: true; onStatusChanged: board.tryFade() }

    // a still picture under the shader: if a GPU cannot run the wind, the menu is still there
    Image { anchors.fill: parent; source: board.currentUrl; asynchronous: true }
    BoardText { variant: board.variant; visible: board.localized && !pic.visible }

    ShaderEffect {
        id: pic
        anchors.fill: parent
        visible: imgA.status === Image.Ready && mA.status === Image.Ready
        property variant srcA: texA
        property variant srcB: imgB.status === Image.Ready ? texB : texA
        property variant maskA: mA
        property variant maskB: mB.status === Image.Ready ? mB : mA
        property real time: 0
        property real strength: board.windTest ? 26 : board.isNight ? 2.2 : 3.6
        property real gust: 0
        property real mixv: board.mixv
        property point texel: Qt.point(1 / 1920, 1 / 1080)
        fragmentShader: "qrc:/shaders/menuwind.frag.qsb"
        NumberAnimation on time { from: 0; to: 3600; duration: 3600000; loops: Animation.Infinite }
    }

    // ---- gusts: every 14..34 s the wind bends the leaves, tears off a few and says «ффф» -------------
    SequentialAnimation {
        id: gustAnim
        NumberAnimation { target: pic; property: "gust"; to: 1; duration: 1400; easing.type: Easing.OutQuad }
        NumberAnimation { target: pic; property: "gust"; to: 0; duration: 2800; easing.type: Easing.InOutQuad }
    }
    Timer {
        interval: 9000; running: true; repeat: true
        onTriggered: {
            interval = 14000 + Math.random() * 20000
            gustAnim.restart()
            leafBurst.burst(board.isNight ? 4 : 9)
            Ambience.gust()
        }
    }

    // ---- light: the sun (the moon) glows and breathes --------------------------------------------
    property real pulse: 0
    SequentialAnimation on pulse {
        loops: Animation.Infinite
        NumberAnimation { from: 0; to: 1; duration: 4200; easing.type: Easing.InOutSine }
        NumberAnimation { from: 1; to: 0; duration: 4200; easing.type: Easing.InOutSine }
    }
    component Glow: Shape {
        id: g
        property real r: 300
        property color tint: "white"
        property real cx: 0
        property real cy: 0
        x: cx - r; y: cy - r
        width: 2 * r; height: 2 * r
        ShapePath {
            strokeWidth: -1
            fillGradient: RadialGradient {
                centerX: g.r; centerY: g.r; centerRadius: g.r
                focalX: g.r; focalY: g.r
                GradientStop { position: 0.0; color: g.tint }
                GradientStop { position: 0.35; color: Qt.rgba(g.tint.r, g.tint.g, g.tint.b, 0.35) }
                GradientStop { position: 1.0; color: "transparent" }
            }
            startX: 0; startY: 0
            PathLine { x: 2 * g.r; y: 0 }
            PathLine { x: 2 * g.r; y: 2 * g.r }
            PathLine { x: 0; y: 2 * g.r }
            PathLine { x: 0; y: 0 }
        }
    }
    Glow {
        cx: board.lit.sun[0]; cy: board.lit.sun[1]
        Behavior on cx { NumberAnimation { duration: 2400; easing.type: Easing.InOutQuad } }
        Behavior on cy { NumberAnimation { duration: 2400; easing.type: Easing.InOutQuad } }
        r: board.isNight ? 230 : 640
        tint: board.sunColor
        opacity: (board.isNight ? 0.22 : board.isEvening ? 0.30 : 0.22) + 0.08 * board.pulse
        Behavior on r { NumberAnimation { duration: 2400 } }
    }

    // god rays from the sun through the leaves, swaying slowly
    property real raySway: 0
    NumberAnimation on raySway { from: 0; to: Math.PI * 2; duration: 23000; loops: Animation.Infinite }
    Repeater {
        model: 7
        Rectangle {
            readonly property real base: 22 + index * 7.5
            x: board.lit.sun[0] - width / 2
            y: board.lit.sun[1]
            Behavior on x { NumberAnimation { duration: 2400; easing.type: Easing.InOutQuad } }
            Behavior on y { NumberAnimation { duration: 2400; easing.type: Easing.InOutQuad } }
            width: 70 + (index % 3) * 46
            height: 2300
            transformOrigin: Item.Top
            rotation: base + Math.sin(board.raySway + index * 1.3) * 2.2
            opacity: (board.isNight ? 0.0 : board.isEvening ? 0.13 : board.isMorning ? 0.12 : 0.09)
                     * (0.55 + 0.45 * Math.sin(board.raySway * 2 + index * 2.1))
            Behavior on opacity { NumberAnimation { duration: 1500 } }
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.08; color: board.sunColor }
                GradientStop { position: 0.55; color: Qt.rgba(board.sunColor.r, board.sunColor.g, board.sunColor.b, 0.35) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }

    // morning mist (and a thin haze in the evening / a blue one at night) crawling along the ground
    Repeater {
        model: 4
        Glow {
            id: fog
            r: 520
            tint: board.isNight ? "#8fa9d6" : board.isMorning ? "#ffe1e6" : "#ffd9b0"
            property real drift: 0
            cy: 700 + index * 95
            cx: ((index * 610 + drift) % 2600) - 340
            scale: 1.0
            transform: Scale { origin.x: fog.r; origin.y: fog.r; xScale: 1.9; yScale: 0.42 }
            opacity: board.isMorning ? 0.26 : board.isEvening ? 0.09 : board.isNight ? 0.07 : 0.0
            Behavior on opacity { NumberAnimation { duration: 2400 } }
            NumberAnimation on drift { from: 0; to: 2600; duration: 160000 + index * 23000; loops: Animation.Infinite }
        }
    }

    // ---- lamps: warm flickering light, moths around them at night --------------------------------
    Repeater {
        model: board.lit.lamps
        Item {
            id: lamp
            readonly property real lx: modelData[0]
            readonly property real ly: modelData[1]
            property real flick: 1
            opacity: board.isNight ? 1 : board.isEvening ? 0.75 : 0
            Behavior on opacity { NumberAnimation { duration: 2000 } }
            SequentialAnimation on flick {
                loops: Animation.Infinite
                NumberAnimation { to: 0.78; duration: 90 + Math.random() * 140 }
                NumberAnimation { to: 1.0; duration: 120 + Math.random() * 300 }
                PauseAnimation { duration: 600 + Math.random() * 2600 }
                NumberAnimation { to: 0.9; duration: 200 }
                NumberAnimation { to: 1.0; duration: 400 }
            }
            Glow { cx: lamp.lx; cy: lamp.ly; r: 260; tint: "#ffbf66"; opacity: 0.30 * lamp.flick }
            Glow { cx: lamp.lx; cy: lamp.ly; r: 70; tint: "#fff0c4"; opacity: 0.75 * lamp.flick }
            Repeater {
                model: board.isNight ? 3 : 0
                Item {
                    id: moth
                    x: lamp.lx; y: lamp.ly
                    property real a: index * 2.1
                    property real rr: 26 + index * 11
                    NumberAnimation on a { from: index * 2.1; to: index * 2.1 + Math.PI * 2; duration: 1800 + index * 900; loops: Animation.Infinite }
                    Rectangle {
                        width: 5; height: 4; radius: 2
                        color: "#fff6da"
                        x: Math.cos(moth.a) * moth.rr + Math.sin(moth.a * 3.3) * 6
                        y: Math.sin(moth.a * 1.3) * moth.rr * 0.6 + Math.cos(moth.a * 2.7) * 5
                        opacity: 0.85
                    }
                }
            }
        }
    }

    // ---- night sky: stars twinkle where the picture's sky is, and one falls now and then -----------
    Item {
        anchors.fill: parent
        opacity: board.isNight && board.variant === "night" ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 2000 } }
        Repeater {
            model: [[1744,98], [1553,132], [1622,289], [1550,37], [1538,328], [1442,118], [1838,84], [1239,50], [1907,232],
                    [1296,45], [1839,164], [1757,255], [1609,245], [1844,292], [1502,65], [1540,90], [1424,74], [1776,22],
                    [1515,181], [1910,302], [1189,105], [1218,82]]
            Rectangle {
                x: modelData[0]; y: modelData[1]
                width: index % 5 === 0 ? 4 : 3; height: width; radius: width / 2
                color: index % 4 === 0 ? "#dce8ff" : "#ffffff"
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    PauseAnimation { duration: (index * 397) % 2300 }
                    NumberAnimation { to: 0.15; duration: 900 + (index * 131) % 1400; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0.95; duration: 900 + (index * 173) % 1400; easing.type: Easing.InOutSine }
                }
            }
        }
        Rectangle {
            id: meteor
            width: 190; height: 2; radius: 1
            rotation: 152
            opacity: 0
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "white" }
                GradientStop { position: 1.0; color: "transparent" }
            }
            ParallelAnimation {
                id: fall
                NumberAnimation { target: meteor; property: "x"; to: meteor.x - 360; duration: 900; easing.type: Easing.InQuad }
                NumberAnimation { target: meteor; property: "y"; to: meteor.y + 190; duration: 900; easing.type: Easing.InQuad }
                SequentialAnimation {
                    NumberAnimation { target: meteor; property: "opacity"; to: 0.9; duration: 150 }
                    PauseAnimation { duration: 450 }
                    NumberAnimation { target: meteor; property: "opacity"; to: 0; duration: 300 }
                }
            }
            Timer {
                interval: 9000; repeat: true; running: board.isNight
                onTriggered: {
                    interval = 16000 + Math.random() * 26000
                    meteor.x = 1450 + Math.random() * 430
                    meteor.y = 10 + Math.random() * 110
                    fall.restart()
                }
            }
        }
    }

    // ---- the near layer: it moves more with the mouse than the picture (depth) --------------------
    Item {
        id: near
        width: 1920; height: 1080
        x: -board.ex * 26
        y: -board.ey * 16

        // dust in the sunbeams
        ParticleSystem { id: dust; running: !board.isNight }
        ItemParticle {
            system: dust
            delegate: Rectangle {
                width: 3 + Math.random() * 3; height: width; radius: width / 2
                color: board.sunColor
                opacity: 0.0
                SequentialAnimation on opacity {
                    running: true
                    NumberAnimation { to: 0.75; duration: 1400 + Math.random() * 1200 }
                    NumberAnimation { to: 0.1; duration: 1600 + Math.random() * 1800 }
                    NumberAnimation { to: 0.6; duration: 1500 }
                    NumberAnimation { to: 0.0; duration: 1800 }
                }
            }
        }
        Emitter {
            system: dust
            x: 1000; y: 40
            width: 900; height: 820
            emitRate: board.isEvening ? 7 : 5
            lifeSpan: 7000
            velocity: AngleDirection { angle: -80; angleVariation: 70; magnitude: 9; magnitudeVariation: 7 }
        }

        // falling leaves (ES's own leaf), more of them when the wind blows
        ParticleSystem { id: leaves }
        ImageParticle {
            system: leaves
            groups: ["leaf"]
            source: "image://gb/file/images/gui/settings/leaf.png"
            rotationVariation: 180
            rotationVelocity: 30
            rotationVelocityVariation: 70
            entryEffect: ImageParticle.Fade
            color: board.isNight ? "#5d7a5a" : board.isEvening ? "#e2a24a" : board.isMorning ? "#f3d3a8" : "#ffffff"
        }
        Emitter {
            system: leaves
            group: "leaf"
            x: 0; y: -40
            width: 1920; height: 10
            emitRate: board.isNight ? 0.5 : 1.2
            lifeSpan: 14000
            size: 34
            sizeVariation: 16
            velocity: AngleDirection { angle: 80; angleVariation: 25; magnitude: 70; magnitudeVariation: 30 }
        }
        Emitter {
            id: leafBurst
            system: leaves
            group: "leaf"
            enabled: false
            x: -60; y: 0
            width: 40; height: 700
            lifeSpan: 9000
            size: 32
            sizeVariation: 14
            velocity: AngleDirection { angle: 12; angleVariation: 22; magnitude: 330; magnitudeVariation: 120 }
            acceleration: AngleDirection { angle: 95; magnitude: 40 }
        }
        Wander { system: leaves; groups: ["leaf"]; xVariance: 90; pace: 60; affectedParameter: Wander.Velocity }

        // fireflies: a few in the evening, many at night
        ParticleSystem { id: flies; running: board.isNight || board.isEvening }
        ItemParticle {
            system: flies
            delegate: Item {
                width: 22; height: 22
                opacity: 0.9
                Rectangle { anchors.centerIn: parent; width: 22; height: 22; radius: 11; color: "#d9ff7a"; opacity: 0.22 }
                Rectangle { anchors.centerIn: parent; width: 6; height: 6; radius: 3; color: "#f4ffc4" }
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.1; duration: 700 + Math.random() * 900 }
                    NumberAnimation { to: 1.0; duration: 700 + Math.random() * 900 }
                }
            }
        }
        Emitter {
            system: flies
            x: 40; y: 560
            width: 1840; height: 500
            emitRate: board.isNight ? 5 : 1.4
            lifeSpan: 8000
            velocity: AngleDirection { angleVariation: 180; magnitude: 16; magnitudeVariation: 12 }
        }
        Wander { system: flies; xVariance: 40; yVariance: 30; pace: 30 }

        // butterflies over the flowers and the grass, by day
        component Butterfly: Item {
            id: bf
            property color wing: "#f2a33a"
            property rect zone: Qt.rect(0, 760, 700, 300)
            width: 36; height: 28
            opacity: board.isNight || board.isEvening ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: 1800 } }
            x: zone.x + Math.random() * zone.width
            y: zone.y + Math.random() * zone.height
            Behavior on x { NumberAnimation { duration: bf.dur; easing.type: Easing.InOutSine } }
            Behavior on y { NumberAnimation { duration: bf.dur; easing.type: Easing.InOutSine } }
            property int dur: 2200
            property real flap: 1
            SequentialAnimation on flap {
                loops: Animation.Infinite
                NumberAnimation { to: 0.2; duration: 110 }
                NumberAnimation { to: 1.0; duration: 130 }
            }
            Timer {
                interval: 1600; repeat: true; running: bf.opacity > 0
                onTriggered: {
                    interval = 1400 + Math.random() * 2400
                    bf.dur = interval + 300
                    const nx = bf.zone.x + Math.random() * bf.zone.width
                    const ny = bf.zone.y + Math.random() * bf.zone.height
                    bf.rotation = nx < bf.x ? -12 : 12
                    bf.x = nx; bf.y = ny + Math.sin(nx) * 30
                }
            }
            Behavior on rotation { NumberAnimation { duration: 600 } }
            Rectangle { x: 16; y: 6; width: 4; height: 17; radius: 2; color: "#3a2a1a" }
            Item {
                x: 18; y: 0; width: 18; height: 28
                transform: Scale { origin.x: 0; xScale: bf.flap }
                Rectangle { x: 0; y: 2; width: 17; height: 14; radius: 7; color: bf.wing; border.color: Qt.darker(bf.wing, 1.6); border.width: 1 }
                Rectangle { x: 0; y: 14; width: 12; height: 11; radius: 6; color: Qt.lighter(bf.wing, 1.15); border.color: Qt.darker(bf.wing, 1.6); border.width: 1 }
            }
            Item {
                x: 0; y: 0; width: 18; height: 28
                transform: Scale { origin.x: 18; xScale: bf.flap }
                Rectangle { x: 1; y: 2; width: 17; height: 14; radius: 7; color: bf.wing; border.color: Qt.darker(bf.wing, 1.6); border.width: 1 }
                Rectangle { x: 6; y: 14; width: 12; height: 11; radius: 6; color: Qt.lighter(bf.wing, 1.15); border.color: Qt.darker(bf.wing, 1.6); border.width: 1 }
            }
        }
        Butterfly { wing: "#f2a33a"; zone: Qt.rect(20, 780, 640, 260) }
        Butterfly { wing: "#fff2a8"; zone: Qt.rect(1080, 840, 520, 200) }
        Butterfly { wing: "#9fc8ff"; zone: Qt.rect(0, 360, 260, 520) }
    }

    // ---- birds: a small flock crosses the sky on the right now and then (not at night) ------------
    Item {
        id: flock
        width: 1920; height: 1080
        opacity: board.isNight ? 0 : 1
        property real fx: -200
        property real fy: 120
        property int dir: 1
        Repeater {
            model: 5
            Shape {
                id: bird
                readonly property real ox: [0, -38, -30, -70, -64][index]
                readonly property real oy: [0, -18, 20, -34, 30][index]
                x: flock.fx + ox * flock.dir
                y: flock.fy + oy + Math.sin(flock.fx / 60 + index) * 5
                width: 30; height: 14
                property real flap: 0
                SequentialAnimation on flap {
                    loops: Animation.Infinite
                    PauseAnimation { duration: index * 70 }
                    NumberAnimation { to: 1; duration: 190; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0; duration: 210; easing.type: Easing.InOutSine }
                }
                ShapePath {
                    strokeColor: board.isEvening ? "#2a1a14" : "#33302c"
                    strokeWidth: 2.4
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    startX: 0; startY: 2 + bird.flap * 8
                    PathQuad { x: 15; y: 8; controlX: 8; controlY: 2 + bird.flap * 6 }
                    PathQuad { x: 30; y: 2 + bird.flap * 8; controlX: 22; controlY: 2 + bird.flap * 6 }
                }
            }
        }
        NumberAnimation { id: fly; target: flock; property: "fx"; duration: 11000 }
        Timer {
            interval: 6000; repeat: true; running: !board.isNight
            onTriggered: {
                interval = 22000 + Math.random() * 26000
                flock.dir = Math.random() < 0.5 ? 1 : -1
                flock.fy = 40 + Math.random() * 200
                fly.from = flock.dir > 0 ? 1180 : 2020
                fly.to = flock.dir > 0 ? 2020 : 1180
                fly.duration = 9000 + Math.random() * 5000
                fly.restart()
            }
        }
    }
}
