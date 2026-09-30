import QtQuick
import QtQuick.Particles
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
    // the game's skip: Ctrl held, or Tab / the right leaf (Skip()) until a choice
    property bool skipHold: false
    property bool skipOn: false
    readonly property bool skipping: skipHold || skipOn
    // the game's «история» (its left leaf): every line shown, and how many steps back to it
    property var history: []
    property bool historyOpen: false
    property string weatherKey: ""
    property var weatherModel: []
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
        skipHold = false
        skipOn = false
        history = []
        historyOpen = false
        musicKey = ""
        ambienceKey = ""
        inputs = []
        popupModel.clear()
        open()
        Music.fadeTo(0)                          // the constructor's own theme steps aside
        apply(Engine.cinemaStart(text, line))
    }
    // «Сломай мой мод» -> «▶ Как туда попасть»: the mod from its start along that route (the clicks and picks of a walk)
    function beginRoute(text, route, seed) {
        docked = false
        start(text, 1)
        const s = Engine.cinemaRoute(text, route, seed || 0)
        inputs = route.slice(0, s.used)
        quiet = true
        apply(s)
        quiet = false
    }
    function restart() { musicKey = ""; ambienceKey = ""; inputs = []; history = []; historyOpen = false; skipOn = false; popupModel.clear(); apply(Engine.cinemaStart(storyText, startLine)) }
    // «назад» (the wheel up, PageUp, a line of the history): the same walk with fewer steps - to that line, as the game rolls back
    function goBack(n) {
        historyOpen = false
        if (n < 0 || n >= inputs.length) return
        skipOn = false
        inputs = inputs.slice(0, n)
        const s = Engine.cinemaReplay(storyText, startLine, inputs)
        if (s.used < inputs.length) inputs = inputs.slice(0, s.used)
        quiet = true
        apply(s)
        quiet = false
    }
    function stepBack() {                        // the line shown before this one
        for (let i = history.length - 1; i >= 0; --i)
            if (history[i].n < inputs.length) { goBack(history[i].n); return }
    }
    function openHistory() {
        skipOn = false
        historyOpen = true
        Qt.callLater(() => hList.positionViewAtEnd())
    }
    function toggleSkip() {
        skipOn = !skipOn
        if (!skipOn) return
        if (typing) shown = stop.text.length
        readTimer.restart()
    }
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
        // the history: a walk back gives it whole; a step on adds the line now shown
        if (s.log !== undefined) history = s.log.slice()
        if ((s.kind === "say" || s.kind === "note") && s.text) history = history.concat([{ speaker: s.speaker, color: s.color, text: s.text, n: inputs.length }])
        if (s.kind === "choice" || s.kind === "end") skipOn = false          // the game's skip stops at a menu
        // the weather keeps falling across lines: a new particle system only when the weather itself changes
        if (s.weatherKey !== weatherKey) { weatherKey = s.weatherKey; weatherModel = s.weather || [] }
        // the lids: the game's own, over the command's seconds (a walk back / a skip puts them where they are at once)
        const shut = s.eyes === "closed" ? 1 : s.eyes === "sleepy" ? 0.5 : 0
        if (Math.abs(shut - lids.shut) > 0.001) {
            lidAnim.stop()
            if (quiet || skipping) lids.shut = shut
            else { lidAnim.from = lids.shut; lidAnim.to = shut; lidAnim.duration = Math.max(150, (s.eyesSeconds || 2) * 1000); lidAnim.start() }
        }
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
            if (s.moment === "blink") lidBlink.restart()
            if (s.moment === "pixels") blink.restart()
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
        if (historyOpen) { historyOpen = false; return }
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

            // ---- the weather, falling: the game's SnowBlossom layers (the same pictures, counts, speeds) as particles;
            // the game's rain falls behind its dialogue box
            Item {
                id: weatherLayer
                // the game's own 1920x1080 units, the whole layer scaled to the frame: the speeds and sizes are the game's
                width: 1920
                height: cv.stop.box ? 916 : 1080
                scale: cv.sc
                transformOrigin: Item.TopLeft
                clip: true
                Repeater {
                    model: cv.opened ? cv.weatherModel : []
                    Item {
                        id: wl
                        required property var modelData
                        anchors.fill: parent
                        readonly property real ys: (modelData.ys0 + modelData.ys1) / 2
                        readonly property real slow: Math.max(6, Math.min(Math.abs(modelData.ys0), Math.abs(modelData.ys1)))
                        readonly property real way: 1080 + 160
                        ParticleSystem { id: ps; anchors.fill: parent; running: cv.opened }
                        ImageParticle {
                            system: ps
                            source: wl.modelData.src
                            alpha: wl.modelData.alpha
                            alphaVariation: wl.modelData.anim === "twinkle" ? 0.5 : 0
                            rotation: wl.modelData.rotate
                            rotationVariation: wl.modelData.anim.indexOf("spin") === 0 ? 180 : 0
                            rotationVelocity: wl.modelData.anim === "spinfast" ? 160 : wl.modelData.anim === "spin" ? 60 : 0
                            rotationVelocityVariation: wl.modelData.anim.indexOf("spin") === 0 ? 40 : 0
                        }
                        Emitter {
                            system: ps
                            // born over the top edge (under the bottom one when they float up); the screen already full at once
                            x: -80
                            width: 1920 + 160
                            y: wl.ys >= 0 ? -80 : 1080 + 80
                            height: 1
                            lifeSpan: wl.way / wl.slow * 1000
                            emitRate: wl.modelData.count / Math.max(0.3, wl.way / Math.max(6, Math.abs(wl.ys)))
                            startTime: lifeSpan
                            size: wl.modelData.size
                            velocity: PointDirection {
                                x: (wl.modelData.xs0 + wl.modelData.xs1) / 2
                                xVariation: Math.abs(wl.modelData.xs1 - wl.modelData.xs0) / 2
                                y: wl.ys
                                yVariation: Math.abs(wl.modelData.ys1 - wl.modelData.ys0) / 2
                            }
                        }
                    }
                }
            }
            // ---- the lids: the game's own pictures (anim blink_up / blink_down) close and open, as «закрытьглаза» does
            Item {
                id: lids
                anchors.fill: parent
                property real shut: 0                    // 0 open, 0.5 sleepy, 1 closed
                visible: shut > 0.001
                Image { width: parent.width; height: parent.height; y: -parent.height * (1 - lids.shut); source: "image://gb/file/images/anim/blink_up.png" }
                Image { width: parent.width; height: parent.height; y: parent.height * (1 - lids.shut); source: "image://gb/file/images/anim/blink_down.png" }
                NumberAnimation { id: lidAnim; target: lids; property: "shut"; easing.type: Easing.InOutSine }
                SequentialAnimation {
                    id: lidBlink                         // «моргание»: the lids, not a black flash
                    NumberAnimation { target: lids; property: "shut"; to: 1; duration: 140; easing.type: Easing.InQuad }
                    NumberAnimation { target: lids; property: "shut"; to: 0; duration: 260; easing.type: Easing.OutQuad }
                }
            }

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
                onWheel: (w) => { if (w.angleDelta.y < 0) cv.advance(); else if (w.angleDelta.y > 0) cv.stepBack() }
            }

            // ---- the game's leaves on its dialogue box (Renderer: 38 / 1768, 949): the left one - what was said
            // (the game's «история»), the right one - skip (Skip(); while it skips the game shows «fast_forward»)
            readonly property string tod: cv.stop.time === "prologue" ? "prologue" : (cv.stop.time || "day")
            Item {
                anchors.fill: parent
                visible: !!cv.stop.box && !cv.historyOpen
                Image {
                    x: 38 * cv.sc; y: 949 * cv.sc
                    width: implicitWidth * cv.sc; height: implicitHeight * cv.sc
                    source: "image://gb/file/images/gui/dialogue_box/" + frame.tod + "/backward_hover.png"
                    visible: backArea.containsMouse
                }
                MouseArea {
                    id: backArea
                    x: 30 * cv.sc; y: 935 * cv.sc; width: 130 * cv.sc; height: 115 * cv.sc
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: cv.openHistory()
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("История: что говорили раньше")
                }
                Image {
                    x: 1768 * cv.sc; y: 949 * cv.sc
                    width: implicitWidth * cv.sc; height: implicitHeight * cv.sc
                    source: "image://gb/file/images/gui/dialogue_box/" + frame.tod + "/" + (cv.skipping ? "fast_forward" : "forward") + (fwdArea.containsMouse ? "_hover" : "_idle") + ".png"
                    visible: fwdArea.containsMouse || cv.skipping
                }
                MouseArea {
                    id: fwdArea
                    x: 1760 * cv.sc; y: 935 * cv.sc; width: 130 * cv.sc; height: 115 * cv.sc
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: cv.toggleSkip()
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Пропуск (Tab)")
                }
            }

            // ---- «история»: the game's text_history - its choice box, the names in their colours, a line takes you back there
            Item {
                id: historyView
                anchors.fill: parent
                visible: cv.historyOpen
                z: 20
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: cv.historyOpen = false
                    onWheel: (w) => { w.accepted = true }
                }
                // the game opens its history as a menu: the scene under it steps back into the dark
                Rectangle { anchors.fill: parent; color: "#b0000000" }
                BorderImage {
                    anchors.centerIn: parent
                    width: 1500 * cv.sc
                    height: 900 * cv.sc
                    border { left: 50; top: 50; right: 50; bottom: 50 }       // the game's Frame of it (Renderer: frame9, 50)
                    source: "image://gb/file/images/gui/choice/" + frame.tod + "/choice_box.png"
                    MouseArea { anchors.fill: parent }         // a click on the box itself does not close it
                    ListView {
                        id: hList
                        anchors.fill: parent
                        anchors.leftMargin: 75 * cv.sc
                        anchors.rightMargin: 60 * cv.sc
                        anchors.topMargin: 70 * cv.sc
                        anchors.bottomMargin: 70 * cv.sc
                        clip: true
                        spacing: 16 * cv.sc
                        model: cv.historyOpen ? cv.history : []
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Column {
                            id: hRow
                            required property var modelData
                            required property int index
                            width: hList.width - 20 * cv.sc
                            spacing: 2 * cv.sc
                            Text {
                                visible: !!hRow.modelData.speaker
                                text: hRow.modelData.speaker || ""
                                color: hRow.modelData.color || "#ffdd7d"
                                font.family: Engine.font()
                                font.pixelSize: Math.max(8, 29 * cv.sc)
                                style: Text.Outline; styleColor: "#80000000"
                            }
                            Text {
                                x: 100 * cv.sc
                                width: parent.width - x
                                wrapMode: Text.Wrap
                                text: hRow.modelData.text
                                color: lineArea.containsMouse ? "#40e138" : "#f3f0e6"
                                font.family: Engine.font()
                                font.pixelSize: Math.max(8, 28 * cv.sc)
                                MouseArea {
                                    id: lineArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: cv.goBack(hRow.modelData.n)
                                }
                            }
                        }
                    }
                }
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
                text: qsTr("строка ") + (cv.stop.line || "—") + qsTr("  ·  клик / пробел — дальше  ·  колёсико ↑ — назад  ·  A — авто  ·  Ctrl / Tab — пропуск  ·  Esc — выход")
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
            if (e.key === Qt.Key_Escape && cv.historyOpen) { cv.historyOpen = false; e.accepted = true }
            else if (e.key === Qt.Key_Space || e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Right) { cv.advance(); e.accepted = true }
            else if (e.key === Qt.Key_PageUp || e.key === Qt.Key_Left) { cv.stepBack(); e.accepted = true }
            else if (e.key === Qt.Key_Tab) { cv.toggleSkip(); e.accepted = true }
            else if (e.key === Qt.Key_A) { cv.auto = !cv.auto; if (cv.auto && !cv.typing) readTimer.restart(); e.accepted = true }
            else if (e.key === Qt.Key_Control) { cv.skipHold = true; if (cv.typing) cv.shown = cv.stop.text.length; readTimer.restart(); e.accepted = true }
            else if (e.key >= Qt.Key_1 && e.key <= Qt.Key_9 && cv.stop.kind === "choice") { cv.pick(e.key - Qt.Key_1); e.accepted = true }
            else if (e.key === Qt.Key_Escape) { cv.close(); e.accepted = true }
        }
        Keys.onReleased: (e) => { if (e.key === Qt.Key_Control) { cv.skipHold = false; e.accepted = true } }
    }
}
