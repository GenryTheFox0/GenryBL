import QtQuick
import GenryBL

// The words of the menu board in the chosen language. The pictures have Russian lettering painted in; for
// every other language LiveBoard shows board_<v>_clean.jpg (tools/make_menu_clean.py took the letters off)
// and this layer writes them again: gold letters on the plank, red titles and ink on the sheets, the plate of
// the stool, «Always ready! / Exit» on the gate sign. Where every text stood in each picture and the colour its
// letters had there (the light of that time of day) - measured by the same tool (work/menu_text_boxes.json).
Item {
    id: bt
    width: 1920
    height: 1080
    property string variant: "day"
    readonly property var at: place[variant] || place.day

    function caps(s) {
        // Turkish has its own dotted capital I
        return Engine.language === "tr" ? s.replace(/i/g, "İ").toUpperCase() : s.toUpperCase()
    }

    // <place> written by tools/make_board_text.py
    readonly property var place: ({
        day: {
            help: [798, 181.2, 854, 100, 6.71, "#f2b351"],
            new: { t: [338, 312, 524, 344], tc: "#820802", b: [347, 391, 618, 652], bc: "#45321a" },
            mods: { t: [700, 335, 858, 367], tc: "#840b04", b: [713, 414, 942, 670], bc: "#534129" },
            gallery: { t: [1014, 361, 1137, 393], tc: "#7b110b", b: [1027, 436, 1241, 653], bc: "#59452e" },
            plate: [1159, 909, 1336, 945, "#e7a585"],
            frame: [1537, 768, 1633, 796, "#f1beaf"],
            panel: [1538, 821, 1633, 850, "#703134"]
        },
        day2: {
            help: [807, 193.2, 872, 108, 6.64, "#f4c459"],
            new: { t: [358, 336, 546, 368], tc: "#800a05", b: [367, 410, 644, 682], bc: "#3d2d16" },
            mods: { t: [722, 352, 880, 384], tc: "#841209", b: [735, 417, 966, 699], bc: "#4b3c24" },
            gallery: { t: [1038, 319, 1164, 351], tc: "#891e13", b: [1050, 441, 1261, 681], bc: "#4d3b26" },
            plate: [1171, 929, 1344, 964, "#eea984"],
            frame: [1558, 788, 1653, 820, "#fec4aa"],
            panel: [1558, 836, 1657, 874, "#6b2a31"]
        },
        evening: {
            help: [792, 186.5, 860, 99, 6.31, "#eb9438"],
            new: { t: [365, 325, 550, 357], tc: "#7d0600", b: [374, 401, 647, 662], bc: "#431c00" },
            mods: { t: [726, 344, 879, 376], tc: "#860900", b: [738, 426, 962, 679], bc: "#5a360e" },
            gallery: { t: [1030, 368, 1146, 400], tc: "#830b00", b: [1044, 441, 1245, 658], bc: "#683b14" },
            plate: [1156, 900, 1320, 937, "#b16448"],
            frame: [1528, 761, 1615, 794, "#c16a56"],
            panel: [1529, 817, 1630, 854, "#57161a"]
        },
        morning: {
            help: [795, 200.2, 849, 102, 6.95, "#f1aa51"],
            new: { t: [347, 339, 536, 371], tc: "#7a0901", b: [355, 414, 629, 677], bc: "#3e220b" },
            mods: { t: [712, 360, 868, 392], tc: "#851106", b: [723, 435, 951, 694], bc: "#775738" },
            gallery: { t: [1025, 384, 1146, 416], tc: "#8b1c0b", b: [1036, 461, 1249, 675], bc: "#69442d" },
            plate: [1157, 919, 1320, 956, "#c37e67"],
            frame: [1545, 783, 1641, 819, "#f6ab9e"],
            panel: [1548, 840, 1639, 866, "#6b2d34"]
        },
        night: {
            help: [810, 181.0, 786, 100, 6.82, "#c78b50"],
            new: { t: [355, 318, 543, 350], tc: "#580500", b: [363, 393, 640, 658], bc: "#260f01" },
            mods: { t: [718, 342, 871, 374], tc: "#540600", b: [729, 414, 962, 674], bc: "#3c220b" },
            gallery: { t: [1024, 367, 1143, 399], tc: "#680f00", b: [1035, 444, 1246, 650], bc: "#422513" },
            plate: [1156, 884, 1321, 919, "#b2866b"],
            frame: [1512, 746, 1598, 777, "#d39484"],
            panel: [1519, 799, 1607, 827, "#341223"]
        }
    })
    // </place>

    // ---- the plank: gold letters with a bevel, their depth and a shadow (qml/shaders/boardgold.frag) ----
    Item {
        id: hdr
        readonly property var h: bt.at.help
        width: h[2]; height: h[3]
        x: h[0] - width / 2
        y: h[1] - height / 2 + height * 0.06        // caps sit high in their line
        rotation: h[4]
        Item {
            id: hdrSrc
            anchors.fill: parent
            Text {
                anchors.fill: parent
                anchors.margins: 12
                anchors.leftMargin: 26
                anchors.rightMargin: 26
                text: bt.caps(qsTr("Информация"))
                color: "white"
                font.family: "Georgia"
                font.bold: true
                font.pixelSize: 112
                font.letterSpacing: 3
                fontSizeMode: Text.Fit
                minimumPixelSize: 20
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        ShaderEffectSource { id: hdrTex; sourceItem: hdrSrc; hideSource: true; visible: false; smooth: true }
        ShaderEffect {
            anchors.fill: parent
            property variant src: hdrTex
            property color base: hdr.h[5]
            property point texel: Qt.point(1 / width, 1 / height)
            fragmentShader: "qrc:/shaders/boardgold.frag.qsb"
        }
    }

    // ---- the three sheets: a red title, then the ink --------------------------------------------------
    // one size for the three titles, like the painted ones: the widest word decides
    readonly property string t1: caps(qsTr("Начать игру"))
    readonly property string t2: caps(qsTr("Сохранение"))
    readonly property string t3: caps(qsTr("Галерея"))
    TextMetrics { id: m1; font.family: "Georgia"; font.bold: true; font.pixelSize: 38; text: bt.t1 }
    TextMetrics { id: m2; font.family: "Georgia"; font.bold: true; font.pixelSize: 38; text: bt.t2 }
    TextMetrics { id: m3; font.family: "Georgia"; font.bold: true; font.pixelSize: 38; text: bt.t3 }
    readonly property real squeeze: 0.84        // the painted titles are a narrow serif; Georgia is wide
    readonly property int titleSize: {
        const w = t => (t[2] - t[0]) / bt.squeeze
        const k = Math.min(1, w(at.new.t) / Math.max(1, m1.advanceWidth), w(at.mods.t) / Math.max(1, m2.advanceWidth),
                           w(at.gallery.t) / Math.max(1, m3.advanceWidth))
        return Math.max(14, Math.floor(38 * k))
    }
    component Sheet: Item {
        id: sh
        property var p
        property string title
        property string body
        Text {
            x: sh.p.t[0]
            width: (sh.p.t[2] - sh.p.t[0]) / bt.squeeze
            y: (sh.p.t[1] + sh.p.t[3]) / 2 - height / 2
            height: 48
            transform: Scale { xScale: bt.squeeze }
            text: sh.title
            color: sh.p.tc
            font.family: "Georgia"
            font.bold: true
            font.pixelSize: bt.titleSize
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 14
            verticalAlignment: Text.AlignVCenter
            opacity: 0.93
        }
        Text {
            x: sh.p.b[0]
            y: sh.p.b[1] - 4
            width: sh.p.b[2] - sh.p.b[0]
            height: sh.p.b[3] - sh.p.b[1] + 10
            text: sh.body
            color: sh.p.bc
            font.family: "Calibri"
            font.pixelSize: 25
            fontSizeMode: Text.Fit
            minimumPixelSize: 11
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignLeft
            lineHeight: 1.04
            opacity: 0.92
        }
    }
    Sheet {
        p: bt.at.new
        title: bt.t1
        body: qsTr("Дорогой пионер!\nТы стоишь на пороге удивительных открытий.\nВпереди — лето, новые знакомства, тёплые вечера и истории, которые останутся с тобой навсегда.\nОткрой ворота «Совёнка» и начни своё приключение!\nДобро пожаловать!")
    }
    Sheet {
        p: bt.at.mods
        title: bt.t2
        body: qsTr("Бережно относись к истории своего лагеря. Здесь ты можешь сохранить свой прогресс, чтобы продолжить путь в другой раз. Каждое сохранение — это маленькая страница твоего лета, к которой всегда можно вернуться.\nСохраняй не только игру, но и воспоминания!")
    }
    Sheet {
        p: bt.at.gallery
        title: bt.t3
        body: qsTr("Здесь собраны фотографии участников нашего лагеря. Твои товарищи, яркие моменты, красивые места и тёплые воспоминания — всё, что делает это лето особенным. Загляни в галерею, чтобы вновь почувствовать атмосферу «Совёнка» и увидеть знакомые лица.")
    }

    // ---- a word in a box, condensed like sign lettering ------------------------------------------------
    component SignWord: Item {
        id: sw
        property var r
        property string word
        property string family: "Segoe UI"
        property real squeeze: 0.84
        property int maxSize: 60
        property int style: Text.Normal
        x: r[0] - 4; y: r[1] - 3
        width: r[2] - r[0] + 8; height: r[3] - r[1] + 6
        Text {
            width: sw.width / sw.squeeze
            height: sw.height
            transform: Scale { xScale: sw.squeeze }
            text: bt.caps(sw.word)
            color: sw.r[4]
            style: sw.style
            styleColor: Qt.darker(sw.r[4], 3.2)
            font.family: sw.family
            font.weight: Font.Bold
            font.pixelSize: sw.maxSize
            fontSizeMode: Text.Fit
            minimumPixelSize: 8
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    SignWord { r: bt.at.plate; word: qsTr("Инструменты"); family: "Tahoma"; squeeze: 0.96; maxSize: 23; style: Text.Sunken }
    SignWord { r: bt.at.frame; word: qsTr("Всегда готов!") }
    SignWord { r: bt.at.panel; word: qsTr("Выход") }
}
