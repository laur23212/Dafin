import QtQuick
import Drift

// Romanian Core (tricolor): original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool picked: !!(control && (control.selected === true || control.checked === true || control.active === true))
    readonly property bool focusState: !!(control && control.visualFocus === true)
    readonly property bool press: !!(control && control.down === true)
    radius: 5
    color: picked ? (Theme.darkMode ? "#9B2333" : "#B2293B") : press ? (Theme.darkMode ? "#1E2740" : "#DECF96") : (Theme.darkMode ? "#323C54" : "#F1E5B5")
    border.width: focusState ? 2 : 1
    border.color: focusState ? "#FFE785" : (Theme.darkMode ? "#BAA75D" : "#B19D4A")
    Rectangle { x: 5; y: 2; width: Math.max(0,parent.width-10); height: 1; color: Theme.darkMode ? "#F0E2AB" : "#FFFBED"; opacity: .45 }
}
