pragma Singleton
import QtQuick
import GenryBL

// Everlasting Summer palette: summer greens of the dialogue box, the gold #ffdd7d of
// the game's text, pioneer red; dark workspace for the editor.
QtObject {
    readonly property color bg: "#0d1310"
    readonly property color bg2: "#121a16"
    readonly property color panel: "#17211c"
    readonly property color panel2: "#1e2b24"
    readonly property color panel3: "#27382f"
    readonly property color line: "#33483c"
    readonly property color text: "#f3f5ec"
    readonly property color dim: "#a9b9a4"
    readonly property color faint: "#65796a"
    readonly property color accent: "#9bd35a"       // leaf green
    readonly property color gold: "#ffdd7d"         // ES dialogue text
    readonly property color pioneer: "#d8332f"      // pioneer tie
    readonly property color paper: "#f4ecd2"
    readonly property color ink: "#4a3b28"
    readonly property color good: "#7bd389"
    readonly property color warn: "#ffc857"
    readonly property color bad: "#ff6b6b"

    readonly property string ui: Engine.font()
    readonly property string riffic: "Corbel"       // ES header font (settings_header)
    readonly property string mono: "Consolas"

    function charColor(tag) {
        switch (tag) {
        case "dv": return "#ffaa00"
        case "un": return "#b956ff"
        case "sl": return "#ffd200"
        case "mi": return "#00deff"
        case "us": return "#ff3200"
        case "mt": return "#00ea32"
        case "el": return "#ffff00"
        case "sh": return "#ffe79c"
        case "mz": return "#4a86ff"
        case "uv": return "#4ee142"
        case "cs": return "#a5a5ff"
        }
        return "#ffdd7d"
    }
    function categoryColor(cat) {
        switch (cat) {
        case "Сцены": return "#ff8a80"
        case "Кадр": return "#80deea"
        case "Диалог": return "#ffdd7d"
        case "Звук": case "Звук и видео": return "#9bd35a"
        case "Эффекты": return "#ce93d8"
        case "Карточки": return "#ffab91"
        case "Логика": return "#90caf9"
        case "Телефон": return "#7dd3fc"
        case "Отношения": return "#ff7a9c"
        case "Инвентарь": return "#c5e1a5"
        case "Меню мода": return "#ffd27d"
        }
        return "#b0bec5"
    }
    function levelColor(level) { return level >= 2 ? bad : level === 1 ? warn : dim }
}
