pragma Singleton
import QtQuick
import QtMultimedia
import GenryBL

// UI sounds straight from Everlasting Summer's archive.
QtObject {
    id: sfx
    property bool enabled: Engine.setting("sfx", true) === true || Engine.setting("sfx", true) === "true"
    property real volume: 0.6
    property int next: 0
    readonly property var cache: ({})

    function url(path) {
        if (!cache[path]) cache[path] = Engine.audioUrl(path)
        return cache[path]
    }
    function play(path) {
        if (!enabled) return
        const u = url(path)
        if (!u) return
        const p = [p0, p1, p2][next]
        next = (next + 1) % 3
        p.stop()
        p.source = u
        p.play()
    }
    function click() { play("sound/sfx/click_1.ogg") }
    function setEnabled(on) { enabled = on; Engine.setSetting("sfx", on) }

    property MediaPlayer p0: MediaPlayer { audioOutput: AudioOutput { volume: sfx.volume } }
    property MediaPlayer p1: MediaPlayer { audioOutput: AudioOutput { volume: sfx.volume } }
    property MediaPlayer p2: MediaPlayer { audioOutput: AudioOutput { volume: sfx.volume } }
}
