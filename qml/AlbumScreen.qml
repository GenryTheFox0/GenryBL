import QtQuick
import QtQuick.Controls
import GenryBL

// "Мои моды" = the ES save/load album (images/gui/save_load/load_bg.jpg): every mod is
// a photo in the game's own thumbnail frame, the cover is the mod's first real frame.
Item {
    id: album
    width: 1920
    height: 1080
    signal back()
    signal open(string id)
    signal play(string id)
    signal create()
    signal publish(string id)

    Image { anchors.fill: parent; source: "image://gb/file/images/gui/save_load/load_bg.jpg"; asynchronous: true }
    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.0 }

    Text {
        x: 470; y: 180
        text: qsTr("Мои моды")
        color: "#f1e7c8"
        style: Text.Raised
        styleColor: "#40000000"
        font.family: Theme.riffic; font.pixelSize: 64; font.bold: true
    }
    Text {
        x: 480; y: 262
        text: Engine.projects.length ? qsTr("Альбом лагеря: ") + Engine.projects.length + qsTr(" шт. Двойной клик — открыть.") : qsTr("Альбом пока пуст — начни с первого мода.")
        color: "#d9ccaa"; font.family: Theme.ui; font.pixelSize: 24
    }

    GridView {
        id: grid
        x: 450; y: 318
        width: 1130; height: 610
        clip: true
        cellWidth: 376
        cellHeight: 300
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        model: [{ create: true }].concat(Engine.projects)
        delegate: Item {
            width: 376; height: 300
            readonly property bool hot: area.containsMouse
            Item {
                id: photo
                x: 20; y: hot ? 4 : 12
                width: 336; height: 196
                rotation: modelData.create ? 0 : ((index * 37) % 5 - 2) * 0.8
                Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                Rectangle { anchors.fill: parent; anchors.margins: 6; color: "#20160e" }
                Image {
                    anchors.fill: parent
                    anchors.margins: 6
                    visible: !modelData.create
                    source: modelData.create ? "" : Engine.coverUrl(modelData.id)
                    sourceSize.width: 520
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 260 } }
                }
                Column {
                    visible: !!modelData.create
                    anchors.centerIn: parent
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: "＋"; color: "#9bd35a"; font.pixelSize: 64 }
                    Text { text: qsTr("Новый мод"); color: "#f1e7c8"; font.family: Theme.riffic; font.pixelSize: 28; font.bold: true }
                }
                Image {
                    anchors.fill: parent
                    source: hot ? "image://gb/file/images/gui/save_load/thumbnail_hover.png" : "image://gb/file/images/gui/save_load/thumbnail_idle.png"
                    opacity: hot ? 0.55 : 1
                }
            }
            Text {
                x: 24; y: 218
                width: 330
                visible: !modelData.create
                text: modelData.name || ""
                color: "#f1e7c8"; font.family: Theme.riffic; font.pixelSize: 24; font.bold: true
                elide: Text.ElideRight
            }
            Text {
                x: 24; y: 250
                visible: !modelData.create
                text: modelData.create ? "" : (qsTr("сцен ") + modelData.scenes + qsTr(" · строк ") + modelData.lines + " · " + modelData.modified)
                color: "#cbbd99"; font.family: Theme.ui; font.pixelSize: 17
            }
            Row {
                x: 30; y: 22
                spacing: 6
                visible: hot && !modelData.create
                PillButton { text: qsTr("Открыть"); onClicked: album.open(modelData.id) }
                PillButton { text: qsTr("▶ Играть"); accent: true; onClicked: album.play(modelData.id) }
            }
            PillButton {
                x: 20 + 336 - width - 10; y: 22
                visible: hot && !modelData.create
                text: "⋯"
                onClicked: { menu.project = modelData; menu.popup() }
            }
            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                z: -1
                cursorShape: Qt.PointingHandCursor
                onClicked: (m) => {
                    if (modelData.create) { Sfx.click(); album.create(); return }
                    if (m.button === Qt.RightButton) { menu.project = modelData; menu.popup() }
                }
                onDoubleClicked: if (!modelData.create) { Sfx.click(); album.open(modelData.id) }
            }
        }
    }

    Menu {
        id: menu
        property var project
        MenuItem { text: qsTr("Открыть"); onTriggered: album.open(menu.project.id) }
        MenuItem { text: qsTr("Переименовать"); onTriggered: { renameField.text = menu.project.name; renameDialog.open() } }
        MenuItem { text: qsTr("Сделать копию"); onTriggered: Engine.duplicateProject(menu.project.id) }
        MenuItem { text: qsTr("Открыть папку"); onTriggered: Engine.openFolder(Engine.projectDir(menu.project.id)) }
        MenuItem { text: qsTr("⇪ В Мастерскую Steam"); enabled: !Engine.busy && !Engine.uploading; onTriggered: album.publish(menu.project.id) }
        MenuSeparator {}
        MenuItem { text: qsTr("В корзину"); onTriggered: trashDialog.open() }
    }
    Dialog {
        id: renameDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Новое название")
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: renameField; width: 420; selectByMouse: true; onAccepted: renameDialog.accept() }
        onAccepted: Engine.renameProject(menu.project.id, renameField.text)
    }
    Dialog {
        id: trashDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Убрать мод в корзину?")
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: menu.project ? "«" + menu.project.name + qsTr("» переедет в projects/_trash — вернуть можно руками.") : "" }
        onAccepted: Engine.trashProject(menu.project.id)
    }

    // ES-style "Назад"
    Text {
        id: backText
        x: 470; y: 960
        text: qsTr("‹ Назад")
        color: backArea.containsMouse ? "#9bd35a" : "#f1e7c8"
        font.family: Theme.riffic; font.pixelSize: 36; font.bold: true
        MouseArea { id: backArea; anchors.fill: parent; anchors.margins: -10; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { Sfx.click(); album.back() } }
    }
    Text {
        x: 1180; y: 968
        text: qsTr("Папка модов")
        color: folderArea.containsMouse ? "#9bd35a" : "#cbbd99"
        font.family: Theme.ui; font.pixelSize: 24
        MouseArea { id: folderArea; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Engine.openFolder(Engine.appRoot + "/projects") }
    }
}
