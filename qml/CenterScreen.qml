import QtQuick
import QtQuick.Controls
import GenryBL

// «Бесконечное лето» - the Center: the game itself, every mod it loads (the Steam Workshop and game/mods), the Steam
// Workshop live (trend, new, top, a search - «Подписаться» in one click), «Мастерская GenryBL» (the catalog on GitHub),
// and Discord. Looks like the game's own screens - the camp at night, paper, tape, a marker - but everything is one
// click away.
Item {
    id: center
    width: 1920
    height: 1080
    signal back()
    property int tab: 0                      // 0 in the game, 1 Steam Workshop, 2 GenryBL
    property var mods: []                    // Engine.scanGameMods
    property bool scanning: false
    property var steam: ({})                 // id -> Workshop details (titles, covers, subscribers)
    property var browse: []
    property int browsePage: 1
    property bool browseMore: false
    property string browseSort: "trend"
    property string browseError: ""
    property bool browsing: false
    property var catalog: []
    property string catalogError: ""
    property bool catalogLoading: false
    property string busyId: ""               // a subscribe / an install on its way
    property string query: ""

    function refresh() {
        scanning = true
        Engine.scanGameMods()
    }
    function openTab(i) {
        tab = i
        if (i === 1 && !browse.length && !browsing) loadBrowse(1)
        if (i === 2 && !catalog.length && !catalogLoading) { catalogLoading = true; Engine.loadCatalog() }
    }
    function loadBrowse(page) {
        browsing = true
        browseError = ""
        browsePage = page
        if (page === 1) browse = []
        Engine.steamBrowse(browseSort, tab === 1 ? query : "", page)
    }
    onVisibleChanged: if (visible) { refresh(); Engine.presence("center"); if (shotPage === "center" && shotArg) openTab(Number(shotArg)) }

    readonly property var installedWs: {
        const s = {}
        for (const m of mods) if (m.source === "workshop") s[m.id] = true
        return s
    }
    readonly property var shownMods: {
        const q = query.trim().toLowerCase()
        return mods.filter(m => !q || (titleOf(m) + " " + m.id).toLowerCase().indexOf(q) >= 0)
    }
    function titleOf(m) { return m.source === "workshop" && steam[m.id] && steam[m.id].title ? steam[m.id].title : m.title }
    function coverOf(m) {
        if (m.source === "workshop" && steam[m.id] && steam[m.id].preview) return steam[m.id].preview
        return m.preview ? "file:///" + m.preview : ""
    }
    function mb(n) { return n >= 1073741824 ? (n / 1073741824).toFixed(1) + qsTr(" ГБ") : Math.max(1, Math.round(n / 1048576)) + qsTr(" МБ") }
    function count(n) { return n >= 1000 ? (n / 1000).toFixed(n >= 10000 ? 0 : 1) + qsTr(" тыс.") : String(n || 0) }

    Connections {
        target: Engine
        function onGameModsReady(list) {
            center.mods = list
            center.scanning = false
            Engine.steamDetails(list.filter(m => m.source === "workshop").map(m => m.id))
        }
        function onSteamDetailsReady(items) {
            const s = Object.assign({}, center.steam)
            for (const k in items) s[k] = items[k]
            center.steam = s
        }
        function onSteamBrowseReady(items, page, more, error) {
            center.browsing = false
            center.browseError = error
            center.browseMore = more
            center.browse = page === 1 ? items : center.browse.concat(items)
        }
        function onSteamSubscribed(id, on, ok, message) {
            center.busyId = ""
            Engine.toast(message, ok ? 0 : 2)
            if (ok) { refreshLater.restart(); if (center.catalog.length) catalogLater.restart() }
        }
        function onCatalogReady(list, error) { center.catalog = list; center.catalogError = error; center.catalogLoading = false }
        function onCatalogInstalled(id, ok, message) {
            center.busyId = ""
            Engine.toast(message, ok ? 0 : 2)
            if (ok) { center.catalogLoading = true; Engine.loadCatalog(); center.refresh() }
        }
    }
    Timer { id: refreshLater; interval: 4000; onTriggered: center.refresh() }
    Timer { id: catalogLater; interval: 6000; onTriggered: Engine.loadCatalog() }

    // ---- the camp at night, the title on a piece of tape
    Image { anchors.fill: parent; source: "image://gb/bg/" + encodeURIComponent("ext_camp_entrance_night"); sourceSize: Qt.size(1920, 1080); asynchronous: true }
    Rectangle { anchors.fill: parent; color: "#b00a0710" }
    Rectangle { x: 0; y: 0; width: 700; height: parent.height; gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: "#e0070509" } GradientStop { position: 1; color: "#00070509" } } }

    Text {
        x: 90; y: 70
        text: qsTr("Бесконечное лето")
        color: "#f1e7c8"; style: Text.Raised; styleColor: "#60000000"
        font.family: Theme.riffic; font.pixelSize: 62; font.bold: true
    }
    Rectangle {                              // the tape: «ЦЕНТР · GenryBL»
        x: 96; y: 150; width: tapeText.implicitWidth + 34; height: 40
        rotation: -3
        color: Theme.pioneer
        Text { id: tapeText; anchors.centerIn: parent; text: qsTr("ЦЕНТР МОДОВ · GenryBL"); color: "#fff4e0"; font.family: Theme.riffic; font.pixelSize: 22; font.bold: true; font.letterSpacing: 2 }
    }

    // ---- the left side: play, Discord
    Column {
        x: 90; y: 250
        width: 520
        spacing: 26
        // the big one
        AbstractButton {
            id: playBtn
            width: 520; height: 118
            hoverEnabled: true
            onClicked: { Sfx.play("gui/sfx/select.ogg"); Engine.playGame() }
            scale: pressed ? 0.97 : hovered ? 1.02 : 1
            Behavior on scale { NumberAnimation { duration: 120 } }
            background: Rectangle {
                radius: 18
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#b5e27a" }
                    GradientStop { position: 1; color: "#5f9a2e" }
                }
                border.color: "#ffdd7d"; border.width: playBtn.hovered ? 3 : 0
            }
            contentItem: Column {
                leftPadding: 30; topPadding: 16
                spacing: 2
                Text { text: qsTr("▶  Играть"); color: "#16240c"; font.family: Theme.riffic; font.pixelSize: 46; font.bold: true }
                Text { text: qsTr("через Steam — время в игре и достижения считаются"); color: "#23380f"; font.family: Theme.ui; font.pixelSize: 17 }
            }
        }
        // Discord - a note pinned on
        Rectangle {
            width: 520; height: discCol.implicitHeight + 40
            rotation: 1.2
            radius: 4
            color: Theme.paper
            Rectangle { x: 200; y: -12; width: 120; height: 26; rotation: -2; color: "#99f1e7c8" }   // a bit of tape
            Column {
                id: discCol
                x: 24; y: 22
                width: parent.width - 48
                spacing: 12
                Row {
                    spacing: 12
                    Text { text: "Discord"; color: Theme.ink; font.family: Theme.riffic; font.pixelSize: 34; font.bold: true }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14; height: 14; radius: 7
                        color: Engine.discordState === "ready" ? "#43b581" : Engine.discordState === "waiting" ? "#faa61a" : "#9a8f7a"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Engine.discordState === "ready" ? qsTr("на связи") : Engine.discordState === "waiting" ? qsTr("Discord не запущен")
                            : Engine.discordState === "error" ? qsTr("Discord не принял") : qsTr("выключено")
                        color: "#6d5a3c"; font.family: Theme.ui; font.pixelSize: 18
                    }
                }
                component Tick: Item {
                    property string text
                    property string sub
                    property bool checked
                    signal toggled(bool on)
                    width: discCol.width; height: tickCol.implicitHeight + 6
                    Rectangle {
                        y: 3; width: 30; height: 30; radius: 6
                        color: parent.checked ? "#7fb845" : "#fffaf0"
                        border.color: "#8a7a55"; border.width: 2
                        Text { anchors.centerIn: parent; text: "✔"; color: "white"; font.pixelSize: 19; visible: parent.parent.checked }
                    }
                    Column {
                        id: tickCol
                        x: 44; width: parent.width - 44
                        Text { width: parent.width; wrapMode: Text.Wrap; text: parent.parent.text; color: Theme.ink; font.family: Theme.ui; font.pixelSize: 21 }
                        Text { width: parent.width; wrapMode: Text.Wrap; visible: !!parent.parent.sub; text: parent.parent.sub; color: "#7d6a4c"; font.family: Theme.ui; font.pixelSize: 15 }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); parent.toggled(!parent.checked) } }
                }
                Tick {
                    text: qsTr("Показывать, что я делаю в GenryBL")
                    sub: qsTr("«Пишет мод «…» · сцена «пляж»», «Смотрит свой мод в кино»")
                    checked: Engine.discordOn
                    onToggled: (on) => Engine.setDiscordOn(on)
                }
                Tick {
                    text: qsTr("Скрывать название мода")
                    sub: qsTr("если мод пока секретный")
                    checked: Engine.discordHide
                    onToggled: (on) => Engine.setDiscordHide(on)
                }
                Tick {
                    text: qsTr("Статус в самой игре")
                    sub: qsTr("«День 3 · вечер · Путь: Славя» или «Мод «…»» — у всех, кто видит тебя в Discord")
                    checked: Engine.gamePresence
                    onToggled: (on) => Engine.setGamePresence(on)
                }
            }
        }
        Text {
            width: 520
            wrapMode: Text.Wrap
            text: center.scanning ? qsTr("Считаю моды…")
                : qsTr("В игре модов: ") + center.mods.length + qsTr("  ·  из Мастерской: ") + center.mods.filter(m => m.source === "workshop").length +
                  qsTr("  ·  в game/mods: ") + center.mods.filter(m => m.source === "local").length
            color: "#cbbd99"; font.family: Theme.ui; font.pixelSize: 20
        }
    }

    // ---- the right side: tabs, search, the cards
    Row {
        x: 700; y: 84
        spacing: 34
        Repeater {
            model: [qsTr("В игре"), qsTr("Мастерская Steam"), qsTr("Мастерская GenryBL")]
            Text {
                text: modelData
                color: center.tab === index ? "#9bd35a" : (tabArea.containsMouse ? "#ffffff" : "#f1e7c8")
                font.family: Theme.riffic; font.pixelSize: 44; font.bold: true
                Rectangle {                      // the marker stroke under the open one
                    visible: center.tab === index
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.bottom
                    anchors.topMargin: -4; height: 6; radius: 3; rotation: -1
                    color: "#9bd35a"
                }
                MouseArea { id: tabArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); center.openTab(index) } }
            }
        }
    }
    Row {
        x: 700; y: 160
        spacing: 10
        TextField {
            id: search
            width: center.tab === 1 ? 420 : 560
            height: 44
            placeholderText: center.tab === 1 ? qsTr("Искать в Мастерской Steam…") : qsTr("Найти мод…")
            placeholderTextColor: "#8f8672"
            color: Theme.text
            font.family: Theme.ui; font.pixelSize: 19
            selectByMouse: true
            background: Rectangle { radius: 22; color: "#cc120d14"; border.color: search.activeFocus ? Theme.accent : "#665c4a" }
            leftPadding: 18
            onTextEdited: { center.query = text; if (center.tab === 1) searchTimer.restart() }
            Timer { id: searchTimer; interval: 600; onTriggered: center.loadBrowse(1) }
        }
        Repeater {                                // the Workshop's order
            model: center.tab === 1 ? [["trend", qsTr("В тренде")], ["new", qsTr("Новое")], ["top", qsTr("Лучшее")], ["updated", qsTr("Обновлённое")]] : []
            PillButton {
                anchors.verticalCenter: parent.verticalCenter
                text: modelData[1]
                accent: center.browseSort === modelData[0] && !center.query
                dark: !accent
                onClicked: { center.browseSort = modelData[0]; search.text = ""; center.query = ""; center.loadBrowse(1) }
            }
        }
        PillButton {
            visible: center.tab === 0
            anchors.verticalCenter: parent.verticalCenter
            dark: true
            text: qsTr("↻ Обновить")
            onClicked: center.refresh()
        }
        PillButton {
            visible: center.tab === 0
            anchors.verticalCenter: parent.verticalCenter
            dark: true
            text: qsTr("Папка game/mods")
            onClicked: Engine.openFolder(Engine.esRoot + "/game/mods")
        }
    }

    // a card: cover, title, what it is, its buttons
    component Card: Rectangle {
        id: card
        property string cover
        property string title
        property string meta
        property string badge
        property color badgeColor: Theme.accent
        property string warn
        default property alias buttons: btnRow.data
        width: 344; height: 318
        radius: 10
        color: cardArea.containsMouse ? "#f21e2b24" : "#e617211c"
        border.color: cardArea.containsMouse ? Theme.gold : "#40ffffff"
        MouseArea { id: cardArea; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        Rectangle {
            x: 8; y: 8; width: parent.width - 16; height: 176; radius: 6; clip: true; color: "#0c0a0e"
            Image { anchors.fill: parent; source: card.cover; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 660 }
            Text {
                anchors.centerIn: parent
                visible: !card.cover
                text: card.title.substring(0, 2).toUpperCase()
                color: "#40f1e7c8"; font.family: Theme.riffic; font.pixelSize: 80; font.bold: true
            }
            Rectangle {                        // the badge: Мастерская / game/mods / GenryBL
                visible: !!card.badge
                x: 8; y: 8; width: badgeText.implicitWidth + 16; height: 24; radius: 4
                rotation: -2
                color: card.badgeColor
                Text { id: badgeText; anchors.centerIn: parent; text: card.badge; color: "#14100c"; font.family: Theme.ui; font.pixelSize: 13; font.bold: true }
            }
            Rectangle {                        // the warning tape
                visible: !!card.warn
                anchors.bottom: parent.bottom; anchors.bottomMargin: 10
                x: -6; width: parent.width + 12; height: 26
                rotation: -2
                color: "#e8d8332f"
                Text { anchors.centerIn: parent; width: parent.width - 20; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; text: card.warn; color: "white"; font.family: Theme.ui; font.pixelSize: 14; font.bold: true }
            }
        }
        Text {
            x: 12; y: 192; width: parent.width - 24
            text: card.title
            elide: Text.ElideRight; maximumLineCount: 1
            color: Theme.text; font.family: Theme.ui; font.pixelSize: 19; font.bold: true
        }
        Text {
            x: 12; y: 220; width: parent.width - 24
            text: card.meta
            elide: Text.ElideRight
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14
        }
        Row {
            id: btnRow
            x: 10; y: 262
            spacing: 8
        }
    }

    // ---- «В игре»
    GridView {
        id: grid
        visible: center.tab === 0
        x: 700; y: 228
        width: 1150; height: 790
        clip: true
        cellWidth: 360; cellHeight: 332
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        model: center.shownMods
        delegate: Card {
            required property var modelData
            cover: center.coverOf(modelData)
            title: center.titleOf(modelData)
            meta: (modelData.source === "workshop" ? qsTr("Мастерская · ") : "game/mods · ") + center.mb(modelData.size) +
                  (modelData.entries.length > 1 ? qsTr(" · входов: ") + modelData.entries.length : modelData.entries.length ? "" : qsTr(" · без своего входа")) +
                  (modelData.source === "workshop" && center.steam[modelData.id] ? "  ·  ♥ " + center.count(center.steam[modelData.id].subs) : "")
            badge: modelData.genrybl ? "GenryBL" : modelData.source === "workshop" ? qsTr("Мастерская") : "game/mods"
            badgeColor: modelData.genrybl ? Theme.accent : modelData.source === "workshop" ? "#8fc7ff" : Theme.gold
            warn: modelData.twins.length ? qsTr("ДУБЛЬ — игра возьмёт только один") : ""
            PillButton {
                visible: modelData.entries.length > 0
                accent: true
                text: qsTr("▶ Играть")
                onClicked: {
                    if (modelData.entries.length > 1) { entryMenu.mod = modelData; entryMenu.popup() }
                    else Engine.playGameMod(modelData.entries[0].label)
                }
            }
            PillButton { dark: true; text: "📁"; onClicked: Engine.openFolder(modelData.dir) }
            PillButton {
                visible: modelData.source === "workshop"
                dark: true
                text: "Steam"
                onClicked: Qt.openUrlExternally("https://steamcommunity.com/sharedfiles/filedetails/?id=" + modelData.id)
            }
            PillButton {
                visible: modelData.source === "workshop"
                dark: true
                enabled: center.busyId === "" && !Engine.steamSubscribing()
                text: center.busyId === modelData.id ? "…" : qsTr("Отписаться")
                onClicked: { center.busyId = modelData.id; Engine.steamSubscribe(modelData.id, false) }
            }
        }
    }
    Menu {
        id: entryMenu
        property var mod: ({ entries: [] })
        Repeater {
            model: entryMenu.mod.entries
            MenuItem { text: modelData.title; onTriggered: Engine.playGameMod(modelData.label) }
        }
    }
    Text {
        visible: center.tab === 0 && !center.scanning && center.shownMods.length === 0
        x: 700; y: 300; width: 1100; wrapMode: Text.Wrap
        text: center.query ? qsTr("Ничего не нашлось") : qsTr("В игре пока нет модов — загляни в Мастерскую Steam или в Мастерскую GenryBL")
        color: "#cbbd99"; font.family: Theme.ui; font.pixelSize: 24
    }

    // ---- «Мастерская Steam»
    GridView {
        visible: center.tab === 1
        x: 700; y: 228
        width: 1150; height: 790
        clip: true
        cellWidth: 360; cellHeight: 332
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        model: center.browse
        footer: Item {
            width: 1100; height: 90
            PillButton {
                anchors.centerIn: parent
                visible: center.browseMore && !center.browsing
                dark: true
                text: qsTr("Ещё")
                onClicked: center.loadBrowse(center.browsePage + 1)
            }
            BusyIndicator { anchors.centerIn: parent; running: center.browsing; visible: running }
        }
        delegate: Card {
            required property var modelData
            readonly property var d: center.steam[modelData.id] || ({})
            readonly property bool have: !!center.installedWs[modelData.id]
            cover: d.preview || ""
            title: d.title || qsTr("Загружаю…")
            meta: d.title ? "♥ " + center.count(d.subs) + qsTr(" подписчиков") + (d.size ? "  ·  " + center.mb(d.size) : "") : ""
            badge: have ? qsTr("В ИГРЕ ✓") : ""
            badgeColor: Theme.accent
            PillButton {
                accent: !have
                dark: have
                enabled: center.busyId === "" && !Engine.steamSubscribing()
                text: center.busyId === modelData.id ? "…" : have ? qsTr("Отписаться") : qsTr("＋ Подписаться")
                onClicked: { center.busyId = modelData.id; Engine.steamSubscribe(modelData.id, !have) }
            }
            PillButton {
                dark: true
                text: qsTr("Страница")
                onClicked: Qt.openUrlExternally("https://steamcommunity.com/sharedfiles/filedetails/?id=" + modelData.id)
            }
        }
    }
    Text {
        visible: center.tab === 1 && !center.browsing && (center.browseError !== "" || center.browse.length === 0)
        x: 700; y: 300; width: 1100; wrapMode: Text.Wrap
        text: center.browseError || qsTr("Ничего не нашлось")
        color: "#cbbd99"; font.family: Theme.ui; font.pixelSize: 24
    }

    // ---- «Мастерская GenryBL»
    GridView {
        visible: center.tab === 2
        x: 700; y: 228
        width: 1150; height: 790
        clip: true
        cellWidth: 360; cellHeight: 332
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        model: center.catalog.filter(m => !center.query || (m.title + " " + (m.author || "")).toLowerCase().indexOf(center.query.toLowerCase()) >= 0)
        delegate: Card {
            required property var modelData
            cover: modelData.cover || ""
            title: modelData.title || ""
            meta: (modelData.author ? modelData.author + "  ·  " : "") + (modelData.version ? "v" + modelData.version : "") +
                  (modelData.size ? "  ·  " + center.mb(modelData.size) : "")
            badge: modelData.installed ? qsTr("УСТАНОВЛЕН ✓") : ""
            PillButton {
                accent: !modelData.installed
                dark: !!modelData.installed
                enabled: center.busyId === "" && !Engine.steamSubscribing()
                text: center.busyId === (modelData.workshop || modelData.id) ? "…"
                    : modelData.workshop ? (modelData.installed ? qsTr("Отписаться") : qsTr("＋ Подписаться"))
                    : modelData.installed ? qsTr("Обновить") : qsTr("＋ Установить")
                onClicked: {
                    if (modelData.workshop) { center.busyId = modelData.workshop; Engine.steamSubscribe(modelData.workshop, !modelData.installed) }
                    else { center.busyId = modelData.id; Engine.installCatalogMod(modelData) }
                }
            }
            PillButton {
                visible: !!modelData.page || !!modelData.workshop
                dark: true
                text: qsTr("Подробнее")
                onClicked: Qt.openUrlExternally(modelData.page || "https://steamcommunity.com/sharedfiles/filedetails/?id=" + modelData.workshop)
            }
        }
    }
    Column {
        visible: center.tab === 2 && !center.catalogLoading && center.catalog.length === 0
        x: 700; y: 290
        spacing: 16
        Text {
            width: 1100; wrapMode: Text.Wrap
            text: center.catalogError || qsTr("Мастерская GenryBL только открылась — первые моды появятся здесь. Сделал мод в GenryBL? Предложи его — после проверки он встанет сюда для всех.")
            color: "#f1e7c8"; font.family: Theme.ui; font.pixelSize: 24
        }
        PillButton {
            accent: true
            text: qsTr("Предложить свой мод")
            onClicked: Qt.openUrlExternally(Engine.catalogSuggestUrl(Engine.setting("lastProject", "")))
        }
    }
    PillButton {
        visible: center.tab === 2 && center.catalog.length > 0
        x: 1590; y: 164
        dark: true
        text: qsTr("Предложить свой мод")
        onClicked: Qt.openUrlExternally(Engine.catalogSuggestUrl(Engine.setting("lastProject", "")))
    }
    BusyIndicator { x: 1240; y: 500; running: (center.tab === 0 && center.scanning) || (center.tab === 2 && center.catalogLoading); visible: running }

    // ES-style «Назад»
    Text {
        x: 90; y: 980
        text: qsTr("‹ Назад")
        color: backArea.containsMouse ? "#9bd35a" : "#f1e7c8"
        font.family: Theme.riffic; font.pixelSize: 36; font.bold: true
        MouseArea { id: backArea; anchors.fill: parent; anchors.margins: -10; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); center.back() } }
    }
}
