pragma Singleton
import QtQuick
import QtMultimedia
import GenryBL

// Background music (optional loop point: "<loop 22.073>file.ogg") and a preview
// channel for the asset browser that ducks the menu theme while a track is auditioned.
QtObject {
    id: music
    property bool enabled: Engine.setting("music", true) === true || Engine.setting("music", true) === "true"
    property real volume: Number(Engine.setting("volume", 0.7))
    property string current: ""
    property int loopMs: 0
    property string previewing: ""

    function playTheme(path, loopAt) {
        if (current === path) { if (enabled) { bg.play(); fadeTo(1) } return }   // resume, don't restart
        current = path
        loopMs = Math.round((loopAt || 0) * 1000)
        bg.source = Engine.audioUrl(path)
        bgOut.volume = 0
        if (enabled) { bg.play(); fadeTo(1) }
    }
    function stopTheme() { fadeTo(0) }
    function fadeTo(v) {
        fade.stop()
        fade.to = v * volume * (enabled ? 1 : 0)
        fade.start()
    }
    function setEnabled(on) {
        enabled = on
        Engine.setSetting("music", on)
        if (on && current) { bg.play(); fadeTo(1) } else fadeTo(0)
    }
    function setVolume(v) { volume = v; Engine.setSetting("volume", v); bgOut.volume = enabled ? v : 0 }

    function preview(path) {
        if (previewing === path) { stopPreview(); return }
        previewing = path
        pv.source = Engine.audioUrl(path)
        pv.play()
        fadeTo(0.15)
    }
    function stopPreview() {
        previewing = ""
        pv.stop()
        if (current) fadeTo(1)
    }

    property AudioOutput bgOut: AudioOutput { volume: 0 }
    property MediaPlayer bg: MediaPlayer {
        audioOutput: music.bgOut
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.EndOfMedia) { position = music.loopMs; play() }
    }
    property NumberAnimation fade: NumberAnimation {
        target: music.bgOut; property: "volume"; duration: 900; easing.type: Easing.InOutQuad
        onFinished: if (music.bgOut.volume === 0 && music.previewing === "") music.bg.pause()
    }
    property MediaPlayer pv: MediaPlayer {
        audioOutput: AudioOutput { volume: music.volume }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.EndOfMedia) music.stopPreview()
    }
}
