import QtQuick

// Double-buffered async image: the old frame stays until the new one is decoded,
// then they crossfade. No black flicker while the renderer works.
Item {
    id: si
    property url source
    property size sourceSize
    property int fillMode: Image.PreserveAspectFit
    property int fade: 140
    property bool showA: true
    property url pending
    readonly property bool loading: (showA ? b : a).status === Image.Loading

    onSourceChanged: {
        pending = source
        if (showA) b.source = source
        else a.source = source
    }

    Image {
        id: a
        anchors.fill: parent
        asynchronous: true
        smooth: true
        mipmap: true
        fillMode: si.fillMode
        sourceSize: si.sourceSize
        opacity: si.showA ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: si.fade } }
        onStatusChanged: if (status === Image.Ready && !si.showA && source === si.pending) si.showA = true
    }
    Image {
        id: b
        anchors.fill: parent
        asynchronous: true
        smooth: true
        mipmap: true
        fillMode: si.fillMode
        sourceSize: si.sourceSize
        opacity: si.showA ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: si.fade } }
        onStatusChanged: if (status === Image.Ready && si.showA && source === si.pending) si.showA = false
    }
}
