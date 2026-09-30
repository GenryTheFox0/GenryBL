import QtQuick
import QtQuick.Controls
import QtMultimedia
import GenryBL

// «Кино-режим»: the mod plays right here, the way the game will run it (src/core/Cinema) - no
// minute of Everlasting Summer loading. The frame is THE renderer's; the words are typed over the
// game's own dialogue box; choices, calls and the map are clickable and lead down their branch;
// music, ambience, sounds and voice play. Click / Space / Enter = on, A = auto, Ctrl = skip, Esc = out.
Popup {
    id: cv
    parent: Overlay.overlay
    // «живое кино»: docked over the editor's preview, the editor stays in hand; every edit re-walks the story to the
    // same moment (Engine.cinemaReplay) - the change is on screen without a restart, no game to load
    property bool docked: false
    property Item dockTo: null
    readonly property point dockAt: docked && dockTo && dockTo.width > 0 && dockTo.height > 0 && parent ? dockTo.mapToItem(parent, 0, 0) : Qt.point(0, 0)
    x: docked && dockTo ? dockAt.x : 0
    y: docked && dockTo ? dockAt.y : 0
    width: docked && dockTo ? dockTo.width : (parent ? parent.width : 1600)
    height: docked && dockTo ? dockTo.height : (parent ? parent.height : 900)
    modal: !docked
    dim: !docked
    focus: !docked
    padding: 0
    closePolicy: docked ? Popup.NoAutoClose : Popup.CloseOnEscape
    signal gotoLine(int line)
    property var inputs: []                     // every click / pick since the start: what a re-walk repeats
    property bool quiet: false                  // a re-walk: no typing again, no sounds and popups again

    property var stop: ({})
    property string storyText: ""
    property int startLine: 1
    property bool auto: false
    property bool skipping: false
    property int shown: 0                       // characters typed so far
    property bool useA: true
    property string musicKey: ""
    property string ambienceKey: ""
    property real remain: 0                     // a timed choice / call: seconds left
    property int soundTurn: 0
    readonly property real sc: frame.width / 1920
    readonly property bool typing: !!stop.typed && shown < (stop.text || "").length
    readonly property bool waitsClick: stop.kind === "say" || stop.kind === "note"

    function begin(text, line) {
        docked = false
        start(text, line)
    }
    function beginDocked(text, line, item) {
        dockTo = item
        docked = true
        start(text, line)
    }
    function start(text, line) {
        storyText = text
        startLine = line
        auto = false
        skipping = false
        musicKey = ""
        ambienceKey = ""
        inputs = []
        popupModel.clear()
        open()
        Music.fadeTo(0)                          // the constructor's own theme steps aside
        apply(Engine.cinemaStart(text, line))
    }
    function restart() { musicKey = ""; ambienceKey = ""; inputs = []; popupModel.clear(); apply(Engine.cinemaStart(storyText, startLine)) }
    // one step on, remembered for the re-walk
    function next(arg) {
        inputs = inputs.concat([arg])
        apply(Engine.cinemaNext(arg))
    }
    // the story was edited: the same route through the new text, to the same step
    function reload(text) {
        if (!opened) return
        storyText = text
        const s = Engine.cinemaReplay(text, startLine, inputs)
        if (s.used < inputs.length) inputs = inputs.slice(0, s.used)
        quiet = true
        apply(s)
        quiet = false
    }

    function apply(s) {
        stop = s
        autoTimer.stop(); choiceTimer.stop(); readTimer.stop()
        // the frame: a quick crossfade like ES's dissolve
        if (useA) { imgB.source = s.frame; fadeToB.restart() } else { imgA.source = s.frame; fadeToA.restart() }
        useA = !useA
        shown = s.typed && !quiet ? 0 : (s.text || "").length
        if (s.typed && !quiet) typer.restart()
        // audio
        if (s.musicKey !== musicKey) {
            musicKey = s.musicKey
            if (s.music) { musicPlayer.source = s.music; musicPlayer.play() } else musicPlayer.stop()
        }
        if (s.ambienceKey !== ambienceKey) {
            ambienceKey = s.ambienceKey
            if (s.ambience) { ambiencePlayer.source = s.ambience; ambiencePlayer.play() } else ambiencePlayer.stop()
        }
        if (!quiet) {
            for (const u of (s.sounds || [])) {
                const p = [sfx0, sfx1, sfx2][soundTurn++ % 3]
                p.stop(); p.source = u; p.play()
            }
            for (const p of (s.popups || [])) popupModel.append({ title: p.title, text: p.text, born: Date.now() })
            if (s.moment === "shake") shake.restart()
            if (s.moment === "flash") flash.restart()
            if (s.moment === "blink" || s.moment === "pixels") blink.restart()
        }
        // what makes it go on by itself
        if (s.kind === "card" || s.kind === "timed") { autoTimer.interval = skipping ? 60 : Math.max(200, s.seconds * 1000); autoTimer.start() }
        if (s.kind === "choice" && s.seconds > 0) { remain = s.seconds; choiceTimer.start() }
        if (s.kind === "video") {
            if (s.video && !skipping) { videoPlayer.source = s.video; videoPlayer.play() }
            else { autoTimer.interval = 700; autoTimer.start() }
        }
        if (s.kind !== "choice" && (auto || skipping) && waitsClick && !s.typed) readTimer.restart()
        if (!docked) keys.forceActiveFocus()        // docked: the keyboard stays with the editor
    }
    function advance() {
        if (!stop.kind || stop.kind === "end" || stop.kind === "choice") return
        if (typing) { shown = stop.text.length; return }
        videoPlayer.stop()
        next(-1)
    }
    function pick(i) {
        if (stop.kind !== "choice" || i >= (stop.options || []).length) return
        if (stop.optionHints && stop.optionHints[i]) return          // locked: «нужно …»
        Sfx.click()
        next(i)
    }
    onClosed: {
        musicPlayer.stop(); ambiencePlayer.stop(); videoPlayer.stop(); sfx0.stop(); sfx1.stop(); sfx2.stop()
        autoTimer.stop(); choiceTimer.stop(); readTimer.stop(); typer.stop()
        Music.fadeTo(1)
    }

    background: Rectangle { color: "black" }

    // ---------------------------------------------------------------- audio
    MediaPlayer { id: musicPlayer; loops: MediaPlayer.Infinite; audioOutput: AudioOutput { volume: Music.volume } }
    MediaPlayer { id: ambiencePlayer; loops: MediaPlayer.Infinite; audioOutput: AudioOutput { volume: Music.volume * 0.8 } }
    MediaPlayer { id: sfx0; audioOutput: AudioOutput { volume: Music.volume } }
    MediaPlayer { id: sfx1; audioOutput: AudioOutput { volume: Music.volume } }
    MediaPlayer { id: sfx2; audioOutput: AudioOutput { volume: Music.volume } }
    MediaPlayer {
        id: videoPlayer
        videoOutput: videoOut
        audioOutput: AudioOutput { volume: Music.volume }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.EndOfMedia && cv.stop.kind === "video") cv.advance()
        onErrorOccurred: if (cv.stop.kind === "video") { autoTimer.interval = 400; autoTimer.start() }
    }

    // ---------------------------------------------------------------- timers
    Timer {                                      // the typewriter (ES: about 40 characters a second)
        id: typer
        interval: 22
        repeat: true
        onTriggered: {
            if (cv.shown >= (cv.stop.text || "").length) {
                stop()
                if (cv.auto || cv.skipping) readTimer.restart()
                return
            }
            cv.shown = Math.min((cv.stop.text || "").length, cv.shown + (cv.skipping ? 40 : 1))
        }
    }
    Timer {                                      // auto mode: time to read, then on
        id: readTimer
        interval: cv.skipping ? 40 : 900 + 28 * (cv.stop.text || "").length
        onTriggered: if ((cv.auto || cv.skipping) && cv.waitsClick) cv.advance()
    }
    Timer { id: autoTimer; onTriggered: cv.advance() }
    Timer {
        id: choiceTimer
        interval: 50
        repeat: true
        onTriggered: {
            cv.remain -= 0.05
            if (cv.remain <= 0) { stop(); cv.next(-1) }   // time is up
        }
    }

    // ---------------------------------------------------------------- the picture
    Item {
        id: stage
        anchors.fill: parent
        anchors.topMargin: 44
        Item {
            id: frame
            width: Math.min(stage.width, stage.height * 16 / 9)
            height: width * 9 / 16
            anchors.centerIn: parent
            clip: true
            Image { id: imgA; anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false; asynchronous: false; opacity: 1 }
            Image { id: imgB; anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false; asynchronous: false; opacity: 0 }
            NumberAnimation { id: fadeToB; target: imgB; property: "opacity"; from: 0; to: 1; duration: cv.skipping ? 0 : 260 }
            NumberAnimation { id: fadeToA; target: imgB; property: "opacity"; from: 1; to: 0; duration: cv.skipping ? 0 : 260 }
            VideoOutput { id: videoOut; anchors.fill: parent; visible: cv.stop.kind === "video" && videoPlayer.playbackState === MediaPlayer.PlayingState }

            // the words, typed over the game's own dialogue box (Renderer: calibri 28, x 194, y 964, width 1541)
            FontMetrics { id: fm; font.family: Engine.font(); font.pixelSize: 28 }
            Repeater {
                model: [[2, 2, "black"], [0, 0, ""]]
                Text {
                    required property var modelData
                    visible: !!cv.stop.typed
                    x: (194 + modelData[0]) * cv.sc
                    y: (964 + modelData[1]) * cv.sc
                    width: 1541 * cv.sc
                    wrapMode: Text.Wrap
                    font.family: Engine.font()
                    font.pixelSize: Math.max(8, 28 * cv.sc)
                    lineHeightMode: Text.FixedHeight
                    lineHeight: (fm.height + 2) * cv.sc
                    color: modelData[2] || cv.stop.whatColor || "#ffdd7d"
                    text: (cv.stop.text || "").substring(0, cv.shown)
                }
            }
            Rectangle { id: flashRect; anchors.fill: parent; color: "white"; opacity: 0 }
            Rectangle { id: blinkRect; anchors.fill: parent; color: "black"; opacity: 0 }
            SequentialAnimation {
                id: shake
                loops: 3
                NumberAnimation { target: frame; property: "anchors.horizontalCenterOffset"; to: 14 * cv.sc; duration: 40 }
                NumberAnimation { target: frame; property: "anchors.horizontalCenterOffset"; to: -14 * cv.sc; duration: 60 }
                NumberAnimation { target: frame; property: "anchors.horizontalCenterOffset"; to: 0; duration: 40 }
            }
            SequentialAnimation {
                id: flash
                NumberAnimation { target: flashRect; property: "opacity"; to: 0.95; duration: 60 }
                NumberAnimation { target: flashRect; property: "opacity"; to: 0; duration: 520 }
            }
            SequentialAnimation {
                id: blink
                NumberAnimation { target: blinkRect; property: "opacity"; to: 1; duration: 120 }
                NumberAnimation { target: blinkRect; property: "opacity"; to: 0; duration: 260 }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: cv.stop.kind === "choice" ? Qt.ArrowCursor : Qt.PointingHandCursor
                onClicked: (m) => { if (m.button === Qt.LeftButton) cv.advance() }
                onWheel: (w) => { if (w.angleDelta.y < 0) cv.advance() }
            }

            // ---- choices: the game's look, clickable; a timed choice shows its fuse
            Column {
                visible: cv.stop.kind === "choice"
                anchors.centerIn: parent
                anchors.verticalCenterOffset: cv.stop.seconds > 0 ? 40 * cv.sc : 0
                spacing: 18 * cv.sc
                Rectangle {
                    visible: cv.stop.seconds > 0
                    width: 900 * cv.sc
                    height: 10 * cv.sc
                    radius: height / 2
                    color: "#55000000"
                    Rectangle {
                        width: parent.width * Math.max(0, cv.remain / Math.max(0.1, cv.stop.seconds || 1))
                        height: parent.height
                        radius: height / 2
                        color: cv.remain < 3 ? "#ff5c5c" : "#ffdd7d"
                    }
                }
                Repeater {
                    model: cv.stop.kind === "choice" ? cv.stop.options : []
                    Rectangle {
                        required property var modelData
                        required property int index
                        readonly property bool ok: !cv.stop.optionOk || cv.stop.optionOk[index] !== false
                        readonly property string lock: cv.stop.optionHints ? (cv.stop.optionHints[index] || "") : ""
                        width: 900 * cv.sc
                        height: (lock ? 88 : 66) * cv.sc
                        radius: 12 * cv.sc
                        color: lock ? "#a0101418" : optArea.containsMouse ? "#e6182a3a" : "#c80e1822"
                        border.color: !lock && optArea.containsMouse ? "#ffdd7d" : "#66ffffff"
                        border.width: Math.max(1, 2 * cv.sc)
                        opacity: ok && !lock ? 1 : 0.55
                        Column {
                            anchors.centerIn: parent
                            width: parent.width - 30 * cv.sc
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                text: (index + 1) + ".  " + modelData + (parent.parent.ok ? "" : qsTr("   · сцены нет"))
                                color: optArea.containsMouse && !parent.parent.lock ? "#ffdd7d" : "#eef6ff"
                                font.family: Engine.font()
                                font.pixelSize: Math.max(9, 30 * cv.sc)
                            }
                            Text {
                                visible: !!parent.parent.lock
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: "🔒 " + parent.parent.lock
                                color: "#c8c8c8"
                                font.family: Engine.font()
                                font.pixelSize: Math.max(8, 20 * cv.sc)
                            }
                        }
                        MouseArea { id: optArea; anchors.fill: parent; hoverEnabled: true; cursorShape: parent.lock ? Qt.ForbiddenCursor : Qt.PointingHandCursor; onClicked: cv.pick(index) }
                    }
                }
            }

            // ---- popups that showed up on the way (notify, «запомнит», points, items, achievements)
            Column {
                anchors.top: parent.top; anchors.right: parent.right
                anchors.margins: 24 * cv.sc
                spacing: 10 * cv.sc
                Repeater {
                    model: ListModel { id: popupModel }
                    Rectangle {
                        required property string title
                        required property string text
                        width: 460 * cv.sc
                        height: col.implicitHeight + 24 * cv.sc
                        radius: 12 * cv.sc
                        color: "#dd143247"
                        border.color: "#ffdd7d"
                        Column {
                            id: col
                            x: 16 * cv.sc; y: 12 * cv.sc
                            width: parent.width - 32 * cv.sc
                            Text { text: parent.parent.title; color: "#ffdd7d"; font.family: Engine.font(); font.pixelSize: Math.max(8, 22 * cv.sc); font.bold: true }
                            Text { text: parent.parent.text; width: parent.width; wrapMode: Text.Wrap; color: "#eef6ff"; font.family: Engine.font(); font.pixelSize: Math.max(8, 24 * cv.sc) }
                        }
                    }
                }
            }
            Timer {
                running: popupModel.count > 0
                interval: 250
                repeat: true
                onTriggered: { while (popupModel.count && Date.now() - popupModel.get(0).born > 3200) popupModel.remove(0) }
            }

            // ---- the end
            Rectangle {
                visible: cv.stop.kind === "end"
                anchors.fill: parent
                color: "#b0000000"
                Column {
                    anchors.centerIn: parent
                    spacing: 18
                    InkText { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Конец"); size: 64; color: Theme.gold }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(frame.width - 80, 900)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: cv.stop.note || ""
                        color: "#eef6ff"; font.family: Theme.ui; font.pixelSize: 18
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 10
                        PillButton { accent: true; text: qsTr("↺ Сначала"); onClicked: cv.restart() }
                        PillButton { dark: true; text: qsTr("К строке ") + (cv.stop.line || 1); onClicked: { cv.gotoLine(cv.stop.line || 1); cv.close() } }
                        PillButton { dark: true; text: qsTr("Закрыть"); onClicked: cv.close() }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- the bar
    Rectangle {
        width: parent.width
        height: 44
        color: "#e0101418"
        Row {
            anchors.verticalCenter: parent.verticalCenter
            x: 14
            spacing: 8
            InkText { text: cv.docked ? qsTr("Живое кино") : qsTr("Кино"); size: 22; color: Theme.gold; anchors.verticalCenter: parent.verticalCenter }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: !cv.docked
                text: qsTr("строка ") + (cv.stop.line || "—") + qsTr("  ·  клик / пробел — дальше  ·  A — авто  ·  Ctrl — промотать  ·  Esc — выход")
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: cv.docked
                text: qsTr("стр. ") + (cv.stop.line || "—")
                color: Theme.dim; font.family: Theme.mono; font.pixelSize: 13
            }
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 12
            spacing: 6
            PillButton { visible: !cv.docked; dark: !cv.auto; accent: cv.auto; text: cv.auto ? qsTr("Авто: вкл") : qsTr("Авто"); onClicked: { cv.auto = !cv.auto; if (cv.auto && !cv.typing) readTimer.restart(); keys.forceActiveFocus() } }
            PillButton { dark: true; text: cv.docked ? "↺" : qsTr("↺ Сначала"); onClicked: cv.restart(); ToolTip.visible: cv.docked && hovered; ToolTip.text: qsTr("Сначала") }
            PillButton {
                visible: cv.docked
                dark: true
                text: qsTr("к строке")
                onClicked: cv.gotoLine(cv.stop.line || 1)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Курсор редактора — на строку, которую сейчас показывает кино")
            }
            PillButton {
                visible: cv.docked
                dark: true
                text: "⛶"
                onClicked: { cv.docked = false; cv.forceActiveFocus(); keys.forceActiveFocus() }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("На весь экран")
            }
            PillButton { visible: !cv.docked; dark: true; text: qsTr("К строке в редакторе"); onClicked: { cv.gotoLine(cv.stop.line || 1); cv.close() } }
            PillButton { dark: true; text: "✕"; onClicked: cv.close() }
        }
    }

    // ---------------------------------------------------------------- keys
    Item {
        id: keys
        focus: true
        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_Space || e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Right) { cv.advance(); e.accepted = true }
            else if (e.key === Qt.Key_A) { cv.auto = !cv.auto; if (cv.auto && !cv.typing) readTimer.restart(); e.accepted = true }
            else if (e.key === Qt.Key_Control) { cv.skipping = true; if (cv.typing) cv.shown = cv.stop.text.length; readTimer.restart(); e.accepted = true }
            else if (e.key >= Qt.Key_1 && e.key <= Qt.Key_9 && cv.stop.kind === "choice") { cv.pick(e.key - Qt.Key_1); e.accepted = true }
            else if (e.key === Qt.Key_Escape) { cv.close(); e.accepted = true }
        }
        Keys.onReleased: (e) => { if (e.key === Qt.Key_Control) { cv.skipping = false; e.accepted = true } }
    }
}
