import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GenryBL

// «Экспорт мода»: the mod as other people get it. Every way first builds it and lets the game itself check it
// (Ren'Py lint) - a broken mod never leaves; then either the archive for players or the folder for the Workshop,
// both in Documents\GenryBL, shown selected in Explorer. The Workshop folder then goes up to Steam from right here
// («⇪ Выложить в Steam», tools/gb_workshop.exe): title, words, who sees it, the upload's progress, the page.
Popup {
    id: ex
    property string projectId: ""
    property string storyText: ""
    property string state_: "choose"            // choose | working | done | failed | publish | uploading | published | upfailed
    property string message: ""
    property string path: ""
    property string kind: ""
    property var ws: ({})                       // Engine.workshopInfo
    property string upStage: ""
    property real upShare: 0
    property string upItem: ""
    property bool upLegal: false
    property bool autoPublish: false             // from the launcher: export, then straight to the form
    property var tags: []
    readonly property bool isNew: !ws.item
    readonly property bool updating: !!ws.item || /\d{6,}/.test(wsItem.text)
    // the Workshop's own tags (as Steam has them) and how they read here
    readonly property var tagGroups: [
        { title: qsTr("Персонажи"), tags: [["Alisa", qsTr("Алиса")], ["Lena", qsTr("Лена")], ["Slavya", qsTr("Славя")], ["Ulyana", qsTr("Ульяна")],
                                           ["Yulya", qsTr("Юля")], ["Miku", qsTr("Мику")], ["Zhenya", qsTr("Женя")], ["Olga Dmitrievna", qsTr("Ольга Дмитриевна")],
                                           ["Semyon", qsTr("Семён")], ["Electronik", qsTr("Электроник")], ["Shurik", qsTr("Шурик")], ["Masha", qsTr("Маша")],
                                           ["Viola", qsTr("Виола")], ["Pioneer", qsTr("Пионер")], ["New character", qsTr("Новый персонаж")]] },
        { title: qsTr("Жанры"), tags: [["Romance", qsTr("Романтика")], ["Slice of life", qsTr("Повседневность")], ["Comedy", qsTr("Комедия")],
                                       ["Drama", qsTr("Драма")], ["Mystery", qsTr("Загадка")], ["Mystic", qsTr("Мистика")], ["Horror", qsTr("Ужасы")],
                                       ["Thriller", qsTr("Триллер")], ["Adventure", qsTr("Приключения")], ["Action", qsTr("Экшен")], ["Sci-fi", qsTr("Фантастика")],
                                       ["Trash", qsTr("Треш")]] },
        { title: qsTr("Тип мода"), tags: [["Linear", qsTr("Линейный")], ["Variative", qsTr("Вариативный")], ["Technical", qsTr("Технический")]] }
    ]
    function toggleTag(t) {
        const i = tags.indexOf(t)
        tags = i >= 0 ? tags.filter(x => x !== t) : tags.concat([t])
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 980)
    height: Math.min(parent.height - 50, state_ === "publish" ? 900 : 620)
    modal: true
    padding: 0
    closePolicy: state_ === "working" || state_ === "uploading" ? Popup.NoAutoClose : Popup.CloseOnEscape

    function openFor(id, text) {
        autoPublish = false
        projectId = id
        storyText = text
        state_ = "choose"
        message = ""
        path = ""
        open()
    }
    function run(kind) {
        Sfx.click()
        ex.kind = kind
        state_ = "working"
        Engine.exportMod(projectId, storyText, kind)
    }
    function startPublish() {
        ws = Engine.workshopInfo(projectId, storyText)
        wsTitle.text = ws.title || ""
        wsDesc.text = ws.desc || ""
        wsNote.text = ws.item ? "" : qsTr("Первая версия")
        wsVis.currentIndex = 0
        wsItem.text = ws.item ? "" : (ws.escuItem || "")
        tags = ws.tags || []
        state_ = "publish"
    }
    function publish() {
        Sfx.click()
        // new: public / friends / only me / by link; an update: the first choice leaves it as it is
        const vis = ex.updating ? wsVis.currentIndex - 1 : wsVis.currentIndex
        upStage = "start"
        upShare = 0
        upLegal = false
        state_ = "uploading"
        Engine.publishWorkshop(projectId, storyText, wsItem.text.trim(), wsTitle.text.trim(), wsDesc.text, vis, wsNote.text, tags)
    }
    function stageText(st) {
        return st === "created" ? qsTr("Предмет в Мастерской создан, готовлю загрузку…")
             : st === "config" || st === "content" ? qsTr("Steam готовит файлы мода…")
             : st === "upload" ? qsTr("Загружаю мод в Steam…")
             : st === "preview" ? qsTr("Загружаю обложку…")
             : st === "commit" ? qsTr("Steam сохраняет мод…")
             : qsTr("Подключаюсь к Steam…")
    }
    Connections {
        target: Engine
        function onExportFinished(ok, message, path) {
            if (!ex.opened) return
            ex.message = message
            ex.path = path
            ex.state_ = ok ? "done" : "failed"
            if (ok && ex.autoPublish && ex.kind === "workshop") ex.startPublish()
        }
        function onWorkshopProgress(stage, share) { ex.upStage = stage; ex.upShare = share }
        function onWorkshopFinished(ok, item, message, legal) {
            if (!ex.opened) return
            ex.upItem = item
            ex.upLegal = legal
            ex.message = message
            ex.state_ = ok ? "published" : "upfailed"
        }
    }

    background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.line }

    component Way: Rectangle {
        id: way
        property string title
        property string body
        property string kind
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: 12
        color: wayArea.containsMouse ? Theme.panel3 : Theme.bg2
        border.color: wayArea.containsMouse ? Theme.gold : Theme.line
        border.width: wayArea.containsMouse ? 2 : 1
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12
            Text { text: way.title; color: Theme.gold; font.family: Theme.riffic; font.pixelSize: 26; font.bold: true }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: way.body
                color: Theme.text; font.family: Theme.ui; font.pixelSize: 17; lineHeight: 1.15
            }
            Item { Layout.fillHeight: true }
            PillButton { accent: true; text: qsTr("Собрать"); onClicked: ex.run(way.kind) }
        }
        MouseArea { id: wayArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ex.run(way.kind); z: -1 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            InkText { text: qsTr("Экспорт мода"); size: 32; color: Theme.gold }
            Item { Layout.fillWidth: true }
            PillButton { dark: true; text: "✕"; enabled: ex.state_ !== "working" && ex.state_ !== "uploading"; onClicked: ex.close() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: ex.state_ === "choose"
            text: qsTr("Сначала мод соберётся и его проверит сама игра (1–2 минуты) — сломанный мод никому не уйдёт. ") +
                  qsTr("Заодно проверю, что каждая картинка, звук и шрифт лежат внутри мода: у другого человека нет твоих файлов.")
            color: Theme.dim; font.family: Theme.ui; font.pixelSize: 16
        }

        // ---- choose
        RowLayout {
            visible: ex.state_ === "choose"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16
            Way {
                title: qsTr("Архив для игроков")
                kind: "zip"
                body: qsTr("ZIP: папка мода и «КАК_УСТАНОВИТЬ.txt». Человек распаковал её в game\\mods — и играет.\n\n") +
                      qsTr("Кидай куда хочешь: ВК, Телеграм, Дискорд, диск. Русские имена файлов не побьются.")
            }
            Way {
                title: qsTr("Папка для Мастерской")
                kind: "workshop"
                body: qsTr("Папка mods\\<мод> с .rpyc и обложка preview.jpg из первого кадра мода. ") +
                      qsTr("Потом одна кнопка «⇪ Выложить в Steam» — и мод в Мастерской, без загрузчика игры.")
            }
            Way {
                title: qsTr("Андроид")
                kind: "android"
                body: qsTr("ZIP для мобильного «Бесконечного лета»: картинки и разметка уменьшены под экран телефона (1280×720), ") +
                      qsTr("имена файлов латиницей, внутри «КАК_УСТАНОВИТЬ_ANDROID.txt» — куда положить папку mods.")
            }
        }

        // ---- working / done / failed
        ColumnLayout {
            visible: ex.state_ === "working" || ex.state_ === "done" || ex.state_ === "failed"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14
            Item { Layout.fillHeight: true }
            BusyIndicator { visible: ex.state_ === "working"; running: visible; Layout.alignment: Qt.AlignHCenter }
            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: ex.state_ !== "working"
                text: ex.state_ === "done" ? "✓" : "✖"
                color: ex.state_ === "done" ? Theme.good : Theme.bad
                font.pixelSize: 64
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: ex.state_ === "working" ? (Engine.busyText || qsTr("Собираю…")) : ex.message
                color: ex.state_ === "failed" ? Theme.bad : Theme.text
                font.family: Theme.ui; font.pixelSize: 18
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                visible: ex.state_ === "done"
                wrapMode: Text.WrapAnywhere
                text: ex.path
                color: Theme.dim; font.family: Theme.mono; font.pixelSize: 13
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 10
                visible: ex.state_ !== "working"
                PillButton {
                    visible: ex.state_ === "done" && ex.kind === "workshop"
                    accent: true
                    text: qsTr("⇪ Выложить в Steam")
                    onClicked: ex.startPublish()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Прямо отсюда в Мастерскую Steam: название, описание, обложка — без загрузчика игры")
                }
                PillButton { visible: ex.state_ === "done"; accent: ex.kind !== "workshop"; dark: ex.kind === "workshop"; text: qsTr("Показать в папке"); onClicked: Engine.revealFile(ex.path) }
                PillButton { dark: true; text: ex.state_ === "failed" ? qsTr("Назад") : qsTr("Ещё экспорт"); onClicked: ex.state_ = "choose" }
                PillButton { dark: true; text: qsTr("Закрыть"); onClicked: ex.close() }
            }
        }

        // ---- «⇪ Выложить в Steam»: the form
        ColumnLayout {
            visible: ex.state_ === "publish"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                Rectangle {
                    Layout.preferredWidth: 128; Layout.preferredHeight: 128
                    radius: 8; clip: true; color: "#111"
                    Image { anchors.fill: parent; source: ex.ws.preview ? "file:///" + ex.ws.preview : ""; fillMode: Image.PreserveAspectCrop; cache: false }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        text: !ex.updating ? qsTr("Новый мод в Мастерской") : qsTr("Обновить мод в Мастерской (предмет ") + (ex.ws.item || (wsItem.text.match(/\d{6,}/) || [""])[0]) + ")"
                        color: Theme.gold; font.family: Theme.ui; font.pixelSize: 17; font.bold: true
                    }
                    TextField {
                        id: wsTitle
                        Layout.fillWidth: true
                        maximumLength: 128
                        placeholderText: qsTr("Название в Мастерской")
                        color: Theme.text; font.family: Theme.ui; font.pixelSize: 16
                        selectByMouse: true
                        background: Rectangle { radius: 8; color: Theme.bg; border.color: wsTitle.activeFocus ? Theme.accent : Theme.line }
                    }
                    RowLayout {
                        spacing: 10
                        Text { text: qsTr("Кто видит:"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 15 }
                        DarkCombo {
                            id: wsVis
                            Layout.preferredWidth: 240
                            model: (ex.updating ? [qsTr("Не менять")] : []).concat([qsTr("Все"), qsTr("Только друзья"), qsTr("Только я"), qsTr("По ссылке")])
                        }
                    }
                }
            }
            // the mod was put up before (by the game's uploader, by hand): its link, and it is updated instead of a double
            RowLayout {
                visible: ex.isNew
                Layout.fillWidth: true
                spacing: 10
                TextField {
                    id: wsItem
                    Layout.fillWidth: true
                    placeholderText: qsTr("Мод уже в Мастерской? Вставь ссылку на него — обновлю, а не создам второй")
                    color: Theme.text; font.family: Theme.mono; font.pixelSize: 14
                    selectByMouse: true
                    background: Rectangle { radius: 8; color: Theme.bg; border.color: wsItem.activeFocus ? Theme.accent : Theme.line }
                }
                Text {
                    visible: !!ex.ws.escuItem && wsItem.text === ex.ws.escuItem
                    text: qsTr("нашёл: выкладывал загрузчиком игры")
                    color: Theme.good; font.family: Theme.ui; font.pixelSize: 13
                }
            }
            // the Workshop's tags: what the story shows is on already
            Repeater {
                model: ex.tagGroups
                Flow {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: modelData.title + ":"; width: 110; color: Theme.dim; font.family: Theme.ui; font.pixelSize: 13; topPadding: 4 }
                    Repeater {
                        model: modelData.tags
                        Rectangle {
                            required property var modelData
                            readonly property bool on: ex.tags.indexOf(modelData[0]) >= 0
                            width: chipText.implicitWidth + 18; height: 24; radius: 12
                            color: on ? Theme.accent : chipArea.containsMouse ? Theme.panel3 : Theme.bg
                            border.color: on ? Theme.accent : Theme.line
                            Text { id: chipText; anchors.centerIn: parent; text: modelData[1]; color: parent.on ? "#16240c" : Theme.text; font.family: Theme.ui; font.pixelSize: 13; font.bold: parent.on }
                            MouseArea { id: chipArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ex.toggleTag(modelData[0]) }
                        }
                    }
                }
            }
            Text { text: qsTr("Описание"); color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14 }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    id: wsDesc
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    color: Theme.text; font.family: Theme.ui; font.pixelSize: 15
                    background: Rectangle { radius: 8; color: Theme.bg; border.color: wsDesc.activeFocus ? Theme.accent : Theme.line }
                }
            }
            TextField {
                id: wsNote
                Layout.fillWidth: true
                placeholderText: qsTr("Что нового в этой версии (видно во вкладке «Изменения»)")
                color: Theme.text; font.family: Theme.ui; font.pixelSize: 15
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.bg; border.color: wsNote.activeFocus ? Theme.accent : Theme.line }
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: !ex.ws.steam ? qsTr("В папке игры нет её Steam-библиотеки — проверь файлы игры в Steam.")
                        : qsTr("Steam должен быть запущен. Пока идёт загрузка, Steam покажет, что ты «играешь» в БЛ, — так и задумано.")
                    color: ex.ws.steam ? Theme.dim : Theme.bad; font.family: Theme.ui; font.pixelSize: 13
                }
                PillButton { dark: true; text: qsTr("Назад"); onClicked: ex.state_ = "done" }
                PillButton { accent: true; enabled: !!ex.ws.steam && wsTitle.text.trim().length > 0; text: qsTr("⇪ Выложить"); onClicked: ex.publish() }
            }
        }

        // ---- the upload and its end
        ColumnLayout {
            visible: ex.state_ === "uploading" || ex.state_ === "published" || ex.state_ === "upfailed"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14
            Item { Layout.fillHeight: true }
            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: ex.state_ !== "uploading"
                text: ex.state_ === "published" ? "✓" : "✖"
                color: ex.state_ === "published" ? Theme.good : Theme.bad
                font.pixelSize: 64
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: ex.state_ === "uploading" ? ex.stageText(ex.upStage) : ex.message
                color: ex.state_ === "upfailed" ? Theme.bad : Theme.text
                font.family: Theme.ui; font.pixelSize: 18
            }
            ProgressBar {
                visible: ex.state_ === "uploading"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 480
                from: 0; to: 1
                value: ex.upShare
                indeterminate: ex.upStage !== "upload"
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: ex.upLegal && ex.state_ !== "uploading"
                text: qsTr("Steam просит принять соглашение Мастерской — пока не примешь, мод видишь только ты.")
                color: Theme.warn; font.family: Theme.ui; font.pixelSize: 15
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 10
                visible: ex.state_ !== "uploading"
                PillButton {
                    visible: !!ex.upItem
                    accent: true
                    text: qsTr("Открыть страницу в Steam")
                    onClicked: Qt.openUrlExternally("https://steamcommunity.com/sharedfiles/filedetails/?id=" + ex.upItem)
                }
                PillButton {
                    visible: ex.upLegal
                    dark: true
                    text: qsTr("Соглашение Мастерской")
                    onClicked: Qt.openUrlExternally("https://steamcommunity.com/sharedfiles/workshoplegalagreement")
                }
                PillButton { visible: ex.state_ === "upfailed"; dark: true; text: qsTr("Ещё раз"); onClicked: ex.startPublish() }
                PillButton { dark: true; text: qsTr("Закрыть"); onClicked: ex.close() }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: qsTr("Куда кладу: ") + Engine.exportDir()
                color: Theme.dim; font.family: Theme.ui; font.pixelSize: 14
            }
            PillButton { dark: true; text: qsTr("Открыть папку"); onClicked: Engine.openFolder(Engine.exportDir()) }
        }
    }
}
