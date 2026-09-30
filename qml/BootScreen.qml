import QtQuick
import QtQuick.Shapes
import GenryBL

// GenryBL's first frame: the dark the menu comes out of, the camp's owl in a turning gold ring, «Загрузка».
// Everything that moves here is an Animator: it runs on the render thread and keeps moving while the launcher is
// being built behind it (the GUI thread is busy then).
Rectangle {
    id: boot
    property bool done: false               // the launcher is there: dissolve into its intro
    color: "black"
    opacity: done ? 0 : 1
    visible: opacity > 0.01
    Behavior on opacity { NumberAnimation { duration: 520; easing.type: Easing.InOutQuad } }
    MouseArea { anchors.fill: parent; hoverEnabled: true }      // nothing under it reacts while it is there

    Item {
        id: content
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -20
        width: 360; height: 330
        opacity: 0
        scale: boot.done ? 1.06 : 1
        Behavior on scale { NumberAnimation { duration: 520; easing.type: Easing.InQuad } }
        OpacityAnimator { target: content; from: 0; to: 1; duration: 420; easing.type: Easing.OutCubic; running: true }

        // a warm glow breathing behind the owl
        Shape {
            id: glow
            x: (parent.width - width) / 2; y: 110 - height / 2
            width: 340; height: 340
            opacity: 0.6
            ShapePath {
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: 170; centerY: 170; centerRadius: 170; focalX: 170; focalY: 170
                    GradientStop { position: 0.0; color: "#40ffdd7d" }
                    GradientStop { position: 0.45; color: "#16ffb84d" }
                    GradientStop { position: 1.0; color: "#00ffdd7d" }
                }
                PathAngleArc { centerX: 170; centerY: 170; radiusX: 170; radiusY: 170; startAngle: 0; sweepAngle: 360 }
            }
            SequentialAnimation {
                running: boot.visible
                loops: Animation.Infinite
                OpacityAnimator { target: glow; from: 0.6; to: 1; duration: 1500; easing.type: Easing.InOutSine }
                OpacityAnimator { target: glow; from: 1; to: 0.6; duration: 1500; easing.type: Easing.InOutSine }
            }
        }

        // the ring: a faint track and a gold comet running round it
        Canvas {
            id: ring
            x: (parent.width - width) / 2; y: 110 - height / 2
            width: 196; height: 196
            onPaint: {
                const c = getContext("2d")
                c.reset()
                const cx = width / 2, cy = height / 2, r = width / 2 - 6
                c.lineWidth = 2
                c.strokeStyle = "rgba(255,221,125,0.14)"
                c.beginPath(); c.arc(cx, cy, r, 0, Math.PI * 2); c.stroke()
                c.lineWidth = 4.5
                const n = 48, span = Math.PI * 1.15, a0 = -Math.PI / 2
                for (let i = 0; i < n; ++i) {
                    const k = (i + 1) / n
                    c.strokeStyle = "rgba(255,221,125," + (k * k).toFixed(3) + ")"
                    c.beginPath(); c.arc(cx, cy, r, a0 + span * i / n, a0 + span * (i + 1) / n + 0.012); c.stroke()
                }
                const head = a0 + span
                c.fillStyle = "#fff3c4"
                c.beginPath(); c.arc(cx + r * Math.cos(head), cy + r * Math.sin(head), 4.2, 0, Math.PI * 2); c.fill()
            }
            RotationAnimator { target: ring; from: 0; to: 360; duration: 1250; loops: Animation.Infinite; running: boot.visible }
        }

        // the owl of the game's menu, bobbing a little
        Image {
            id: owl
            x: (parent.width - width) / 2 - 9; y: 110 - height / 2      // the owl sits right of its picture's middle
            width: 124; height: 124
            fillMode: Image.PreserveAspectFit
            smooth: true
            source: Engine.esArt ? "image://gb/file/images/gui/title_menu/owl_idle.png" : ""
            SequentialAnimation {
                running: boot.visible
                loops: Animation.Infinite
                YAnimator { target: owl; from: 48; to: 41; duration: 950; easing.type: Easing.InOutSine }
                YAnimator { target: owl; from: 41; to: 48; duration: 950; easing.type: Easing.InOutSine }
            }
        }

        // «Загрузка» and three dots running a wave
        Row {
            id: label
            anchors.horizontalCenter: parent.horizontalCenter
            y: 238
            spacing: 5
            Text {
                text: qsTr("Загрузка")
                color: Theme.gold
                font.family: Theme.ui
                font.pixelSize: 26
                font.letterSpacing: 2
                style: Text.Raised; styleColor: "#66000000"
            }
            Repeater {
                model: 3
                Rectangle {
                    id: dot
                    required property int index
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                    width: 6; height: 6; radius: 3
                    color: Theme.gold
                    opacity: 0.2
                    // one cycle for every dot (the waits are animators holding still): the wave never drifts
                    SequentialAnimation {
                        running: boot.visible
                        loops: Animation.Infinite
                        OpacityAnimator { target: dot; from: 0.2; to: 0.2; duration: 1 + dot.index * 170 }
                        OpacityAnimator { target: dot; from: 0.2; to: 1; duration: 340; easing.type: Easing.OutQuad }
                        OpacityAnimator { target: dot; from: 1; to: 0.2; duration: 340; easing.type: Easing.InQuad }
                        OpacityAnimator { target: dot; from: 0.2; to: 0.2; duration: 1 + (2 - dot.index) * 170 + 260 }
                    }
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 282
            text: "GenryBL " + Engine.version
            color: Theme.faint
            font.family: Theme.ui
            font.pixelSize: 14
            font.letterSpacing: 1
        }
    }
}
