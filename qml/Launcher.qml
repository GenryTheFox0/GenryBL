import QtQuick
import QtQuick.Controls
import GenryBL

// The launcher IS Everlasting Summer's menu: the INFORMATION board, the pioneer album
// for the mods, the preferences paper, the photo club - with the game's music.
Item {
    id: launcher
    signal openEditor(string id)
    signal playProject(string id)
    property bool intro: true
    property string screen: "menu"          // menu | mods | gallery | settings
    property string dialog: ""              // "" | new | help | about
    property string startScreen: ""

    function lastProject() {
        const id = Engine.setting("lastProject", "")
        for (const p of Engine.projects) if (p.id === id) return p
        return null
    }
    function clockTime() {
        const h = new Date().getHours()
        return h >= 7 && h < 18 ? "day" : (h >= 18 && h < 21 ? "sunset" : "night")
    }
    function open(id) { Music.stopTheme(); launcher.openEditor(id) }

    Component.onCompleted: {
        Music.playTheme("sound/music/blow_with_the_fires.ogg", 0)
        if (startScreen) screen = startScreen
    }

    Rectangle { anchors.fill: parent; color: "black" }

    Item {
        id: stage
        width: 1920; height: 1080
        readonly property real s: Math.min(launcher.width / 1920, launcher.height / 1080)
        scale: s
        transformOrigin: Item.TopLeft
        x: (launcher.width - 1920 * s) / 2
        y: (launcher.height - 1080 * s) / 2

        component Screen: Item {
            property bool shown
            width: 1920; height: 1080
            opacity: shown ? 1 : 0
            visible: opacity > 0.01
            enabled: shown
            Behavior on opacity { NumberAnimation { duration: 450; easing.type: Easing.InOutQuad } }
        }

        Screen {
            shown: launcher.screen === "menu"
            MainMenuScreen {
                intro: launcher.intro
                lastProject: launcher.lastProject()
                timeOfDay: {
                    const t = Engine.setting("menuTime", "auto")
                    return t === "auto" || !t ? launcher.clockTime() : t
                }
                onPick: (what) => {
                    if (what === "quit") Qt.quit()
                    else if (what === "continue") { const p = launcher.lastProject(); if (p) launcher.open(p.id) }
                    else if (what === "new" || what === "help" || what === "about") launcher.dialog = what
                    else launcher.screen = what
                }
            }
        }
        Screen {
            shown: launcher.screen === "mods"
            AlbumScreen {
                onBack: launcher.screen = "menu"
                onOpen: (id) => launcher.open(id)
                onPlay: (id) => launcher.playProject(id)
                onCreate: launcher.dialog = "new"
            }
        }
        Screen {
            shown: launcher.screen === "gallery"
            GalleryScreen { onBack: launcher.screen = "menu" }
        }
        Screen {
            shown: launcher.screen === "settings"
            SettingsScreen { onBack: launcher.screen = "menu" }
        }

        // ---- the plaques: the constructor is 18+; a developer build is marked
        Row {
            x: 1920 - width - 34; y: 28
            spacing: 10
            Rectangle {
                visible: Engine.editionBadge !== ""
                height: 42; width: devText.implicitWidth + 30; radius: 21
                color: "#dd12304a"; border.color: "#ffdd7d"; border.width: 2
                Text { id: devText; anchors.centerIn: parent; text: Engine.editionBadge; color: "#ffdd7d"; font.family: Theme.ui; font.pixelSize: 20; font.bold: true }
            }
            Rectangle {
                height: 42; width: adultText.implicitWidth + 30; radius: 21
                color: "#c0392b"; border.color: "#7d1d14"; border.width: 2
                Text { id: adultText; anchors.centerIn: parent; text: "18+ · только для взрослых"; color: "white"; font.family: Theme.ui; font.pixelSize: 20; font.bold: true }
            }
        }

        // ---- dialogs on ES's confirm plate (images/gui/o_rly/base.png)
        Item {
            width: 1920; height: 1080
            opacity: launcher.dialog !== "" ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 260 } }
            MouseArea { anchors.fill: parent; onClicked: if (launcher.dialog !== "new") launcher.dialog = "" }
            Image {
                anchors.fill: parent
                visible: launcher.dialog === "new"
                source: "image://gb/file/images/gui/o_rly/base.png"
            }
            Rectangle { anchors.fill: parent; color: "#aa000000"; visible: launcher.dialog !== "new" }

            // new mod
            Item {
                visible: launcher.dialog === "new"
                x: 520; y: 410; width: 880; height: 180
                Text { x: 20; y: 8; text: "Как назовём мод?"; color: "#5a3e1e"; font.family: Theme.riffic; font.pixelSize: 44; font.bold: true }
                TextField {
                    id: nameField
                    x: 20; y: 78; width: 560; height: 58
                    placeholderText: "Например: Последний день смены"
                    font.family: Theme.ui; font.pixelSize: 28
                    color: Theme.ink
                    selectByMouse: true
                    background: Rectangle { radius: 6; color: "#fffdf4"; border.color: nameField.activeFocus ? "#7fb845" : "#b9a57a"; border.width: 2 }
                    onAccepted: createBtn.clicked()
                }
                PillButton {
                    id: createBtn
                    x: 600; y: 82; height: 50; width: 150
                    text: "Создать"
                    accent: true
                    font.pixelSize: 22
                    onClicked: { const id = Engine.createProject(nameField.text); nameField.text = ""; launcher.dialog = ""; launcher.open(id) }
                }
                PillButton { x: 760; y: 82; height: 50; width: 100; text: "Отмена"; onClicked: launcher.dialog = "" }
                onVisibleChanged: if (visible) nameField.forceActiveFocus()
            }

            // help / about on the album paper
            Rectangle {
                visible: launcher.dialog === "help" || launcher.dialog === "about"
                anchors.centerIn: parent
                width: 1100; height: launcher.dialog === "help" ? 760 : 540
                radius: 10
                color: "#f7f0da"
                border.color: "#b9a57a"; border.width: 3
                Column {
                    x: 50; y: 36; width: parent.width - 100
                    spacing: 14
                    Text {
                        text: launcher.dialog === "help" ? "Как это работает" : "GenryBL " + Engine.version
                        color: "#8a1f1a"; font.family: Theme.riffic; font.pixelSize: 46; font.bold: true
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        color: Theme.ink; font.family: Theme.ui; font.pixelSize: 25
                        text: launcher.dialog === "help"
                              ? "• Пишешь историю простыми командами: «фон ext_square_day», «показать dv smile pioneer left», «Алиса: Привет!».\n" +
                                "• Справа сразу видно, что увидит игрок — тот же кадр, что нарисует игра, из её же архива.\n" +
                                "• Наведи мышь на эмоцию, фон или команду слева — кадр примерит её до вставки.\n" +
                                "• Подсказки при наборе: после «показать » — персонажи, потом эмоции, одежда, позиции.\n" +
                                "• Ошибки подчёркиваются до запуска: несуществующая эмоция, фон, трек, сцена.\n" +
                                "• «Играть» (F5) собирает мод в game/mods и открывает БЛ окном прямо в сцене под курсором, со своими сохранениями.\n" +
                                "• «Проверить движком» — родной lint Ren'Py самой игры."
                              : "Конструктор модов «Бесконечного лета» на C++ и Qt.\nСделан Генри и Шрамом.\n\n" +
                                "18+ патч внутри — «Deleted hentai scenes» из Мастерской Steam, автор — Лена. Спасибо ей.\n\nСова всё видит."
                    }
                    Row {
                        spacing: 10
                        PillButton { text: "Понятно"; accent: true; onClicked: launcher.dialog = "" }
                        PillButton {
                            visible: launcher.dialog === "about"
                            text: "Патч в Мастерской"
                            onClicked: Qt.openUrlExternally("https://steamcommunity.com/sharedfiles/filedetails/?id=1118110148")
                        }
                        PillButton {
                            visible: launcher.dialog === "about"
                            text: "Автор патча"
                            onClicked: Qt.openUrlExternally("https://steamcommunity.com/id/Lena_sova")
                        }
                        PillButton {
                            visible: launcher.dialog === "about"
                            text: "Отзывы и баги"
                            onClicked: Qt.openUrlExternally("https://github.com/GenryTheFox0/GenryBL/issues")
                        }
                    }
                }
            }
        }
    }

    Keys.onEscapePressed: { if (dialog !== "") dialog = ""; else if (screen !== "menu") screen = "menu" }
    focus: true
}
