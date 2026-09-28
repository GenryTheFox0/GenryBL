import QtQuick
import QtQuick.Particles
import QtQuick.Shapes
import GenryBL

// A summer evening drawn by code (no game files needed - the first start and the installer run
// before the game is found): the sky drifts from day to sunset and back, the sun glows, two rows
// of hills sway a little after the mouse, leaves fall.
Item {
    id: bd
    clip: true
    property real t: 0                    // 0 day .. 1 sunset
    property real mx: 0
    SequentialAnimation on t {
        loops: Animation.Infinite
        NumberAnimation { from: 0; to: 1; duration: 26000; easing.type: Easing.InOutSine }
        NumberAnimation { from: 1; to: 0; duration: 26000; easing.type: Easing.InOutSine }
    }
    function mix(a, b, k) { return Qt.rgba(a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, 1) }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: bd.mix(Qt.color("#3a7bd5"), Qt.color("#2a2350"), bd.t) }
            GradientStop { position: 0.55; color: bd.mix(Qt.color("#9fd3f7"), Qt.color("#f08a4b"), bd.t) }
            GradientStop { position: 1.0; color: bd.mix(Qt.color("#e9f7d9"), Qt.color("#ffcf7a"), bd.t) }
        }
    }
    // the sun
    Shape {
        id: sun
        anchors.fill: parent
        readonly property real cx: bd.width * 0.74 - bd.mx * 12
        readonly property real cy: bd.height * (0.2 + 0.28 * bd.t)
        ShapePath {
            strokeWidth: -1
            fillGradient: RadialGradient {
                centerX: sun.cx; centerY: sun.cy; centerRadius: bd.height * 0.36
                focalX: sun.cx; focalY: sun.cy
                GradientStop { position: 0.0; color: Qt.rgba(1, 0.97, 0.85, 0.95) }
                GradientStop { position: 0.12; color: Qt.rgba(1, 0.9, 0.6, 0.75) }
                GradientStop { position: 0.45; color: Qt.rgba(1, 0.75, 0.4, 0.18) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 0.7, 0.3, 0.0) }
            }
            startX: 0; startY: 0
            PathLine { x: bd.width; y: 0 }
            PathLine { x: bd.width; y: bd.height }
            PathLine { x: 0; y: bd.height }
            PathLine { x: 0; y: 0 }
        }
    }
    // hills: far and near, smooth curves
    Repeater {
        model: [[0.70, "#3f6b3a", "#4a3b3a", 0.35, 8], [0.80, "#264d2a", "#2b2230", 0.55, 20]]
        Shape {
            id: hill
            required property var modelData
            anchors.fill: parent
            readonly property real base: bd.height * modelData[0]
            readonly property real sway: bd.mx * modelData[4]
            readonly property real bump: bd.height * 0.06 * modelData[3]
            ShapePath {
                strokeWidth: -1
                fillColor: bd.mix(Qt.color(hill.modelData[1]), Qt.color(hill.modelData[2]), bd.t * 0.8)
                startX: -60 + hill.sway; startY: bd.height + 10
                PathLine { x: -60 + hill.sway; y: hill.base }
                PathCubic {
                    x: bd.width * 0.5 + hill.sway; y: hill.base - hill.bump
                    control1X: bd.width * 0.15; control1Y: hill.base - bd.height * 0.14
                    control2X: bd.width * 0.3; control2Y: hill.base + bd.height * 0.05
                }
                PathCubic {
                    x: bd.width + 60 + hill.sway; y: hill.base - bd.height * 0.03
                    control1X: bd.width * 0.7; control1Y: hill.base - bd.height * 0.16
                    control2X: bd.width * 0.85; control2Y: hill.base + bd.height * 0.04
                }
                PathLine { x: bd.width + 60; y: bd.height + 10 }
                PathLine { x: -60 + hill.sway; y: bd.height + 10 }
            }
        }
    }
    // falling leaves (the leaf picture is drawn by code: image://gb/leaf)
    ParticleSystem { id: sys }
    ImageParticle {
        system: sys
        source: "image://gb/leaf/0"
        colorVariation: 0.25
        rotationVariation: 180
        rotationVelocityVariation: 90
        entryEffect: ImageParticle.Fade
    }
    Emitter {
        system: sys
        width: bd.width
        y: -30
        emitRate: 5
        lifeSpan: 14000
        size: 26
        sizeVariation: 12
        velocity: AngleDirection { angle: 80; angleVariation: 25; magnitude: 70; magnitudeVariation: 30 }
    }
    Wander { system: sys; anchors.fill: parent; xVariance: 70; pace: 60 }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: (m) => bd.mx = (m.x / Math.max(1, width)) * 2 - 1
    }
    Behavior on mx { SmoothedAnimation { velocity: 1.2 } }
}
