import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool focusState: !!(control && (control.activeFocus === true || control.visualFocus === true))
    readonly property bool hover: !!(control && control.hovered === true)
    readonly property bool error: !!(control && control.hasError === true)
    color: Theme.darkMode ? "#1A272B" : "#FFF6E4"
    radius: 5
    border.width: root.focus || root.error ? 2 : 1
    border.color: root.error ? "#D95257" : root.focus ? (Theme.darkMode ? "#F5CC7F" : "#A82E3A") : (Theme.darkMode ? "#8A7255" : "#B6A082")
    Rectangle { x: 1; y: 1; width: Math.max(0,parent.width-2); height: 2; color: Theme.darkMode ? "#070D10" : "#D7C3A3"; opacity: .55; radius: 1 }
    Rectangle { x: 3; y: Math.max(0,parent.height-3); width: Math.max(0,parent.width-6); height: 1; color: Theme.darkMode ? "#C5A87C" : "#D7BE95"; opacity: .52 }
}
