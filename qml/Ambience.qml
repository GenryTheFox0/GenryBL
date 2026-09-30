pragma Singleton
import QtQuick
import QtMultimedia
import GenryBL

// Nature under the menu music: the camp by the time of day (ES's own ambiences), cross-faded when the
// time changes, plus rare sounds of its own - an owl at night, a rustle in the bushes, a gust of wind
// (the living menu, LiveBoard.qml, calls gust() when its leaves bend).
QtObject {
    id: amb
    property bool enabled: !Engine.shotMode && (Engine.setting("ambience", true) === true || Engine.setting("ambience", true) === "true")
    property real level: 0.6                       // under the music
    readonly property real target: enabled ? Music.volume * level : 0
    property string timeOfDay: ""
    property bool active: false
    property bool useA: true

    readonly property var loops: ({
        morning: "sound/ambiences/day_countryside_ambience.ogg",
        day:     "sound/ambiences/forest_day.ogg",
        evening: "sound/ambiences/forest_evening.ogg",
        night:   "sound/ambiences/forest_night.ogg"
    })
    readonly property var extras: ({
        morning: ["sound/sfx/bush_leaves.ogg"],
        day:     ["sound/sfx/bush_leaves.ogg"],
        evening: ["sound/sfx/bush_leaves.ogg", "sound/sfx/owl_far.ogg"],
        night:   ["sound/sfx/owl_far.ogg", "sound/sfx/owl_far.ogg", "sound/sfx/bush_leaves.ogg"]
    })

    function play(tod) {
        if (!tod) return
        const path = loops[tod] || loops.day
        timeOfDay = tod
        if (active && (useA ? pA : pB).source.toString() === Engine.audioUrl(path).toString()) return
        active = true
        const next = useA ? pB : pA
        const prev = useA ? pA : pB
        useA = !useA
        next.source = Engine.audioUrl(path)
        outFor(next).volume = 0
        if (enabled) next.play()
        fadeIn.target = outFor(next); fadeIn.to = target; fadeIn.restart()
        fadeOut.target = outFor(prev); fadeOut.to = 0; fadeOut.player = prev; fadeOut.restart()
    }
    function stop() {
        active = false
        fadeOut.target = outFor(pA); fadeOut.to = 0; fadeOut.player = pA; fadeOut.restart()
        fadeOut2.restart()
    }
    function setEnabled(on) {
        enabled = on
        Engine.setSetting("ambience", on)
        if (!on) { pA.pause(); pB.pause(); oOne.volume = 0 }
        else if (active) { const p = useA ? pA : pB; p.play(); outFor(p).volume = target }
    }
    function gust() {
        if (!enabled || !active) return
        one.source = Engine.audioUrl("sound/sfx/wind_gust.ogg")
        oOne.volume = target * 0.45
        one.play()
    }
    function outFor(p) { return p === pA ? oA : oB }

    // follow the volume slider of the settings
    onTargetChanged: if (active) outFor(useA ? pA : pB).volume = target

    property AudioOutput oA: AudioOutput { volume: 0 }
    property AudioOutput oB: AudioOutput { volume: 0 }
    property AudioOutput oOne: AudioOutput { volume: 0 }
    property MediaPlayer pA: MediaPlayer { audioOutput: amb.oA; loops: MediaPlayer.Infinite }
    property MediaPlayer pB: MediaPlayer { audioOutput: amb.oB; loops: MediaPlayer.Infinite }
    property MediaPlayer one: MediaPlayer { audioOutput: amb.oOne }

    property NumberAnimation fadeIn: NumberAnimation { property: "volume"; duration: 2600; easing.type: Easing.InOutQuad }
    property NumberAnimation fadeOut: NumberAnimation {
        property var player: null
        property: "volume"; duration: 2600; easing.type: Easing.InOutQuad
        onFinished: if (player && (!amb.active || player !== (amb.useA ? amb.pA : amb.pB))) player.stop()
    }
    property NumberAnimation fadeOut2: NumberAnimation {
        target: amb.oB; property: "volume"; to: 0; duration: 1200
        onFinished: if (!amb.active) { amb.pA.stop(); amb.pB.stop() }
    }

    // a rare sound of its own, every 20..50 s
    property Timer extra: Timer {
        interval: 26000; repeat: true; running: amb.active && amb.enabled
        onTriggered: {
            interval = 20000 + Math.random() * 30000
            const list = amb.extras[amb.timeOfDay] || []
            if (!list.length || amb.one.playbackState === MediaPlayer.PlayingState) return
            amb.one.source = Engine.audioUrl(list[Math.floor(Math.random() * list.length)])
            amb.oOne.volume = amb.target * (amb.timeOfDay === "night" ? 0.9 : 0.55)
            amb.one.play()
        }
    }
}
