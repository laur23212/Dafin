import QtQuick
import Drift

// Romanian Core (tricolor): original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool focusState: !!(control && (control.activeFocus === true || control.visualFocus === true))
    readonly property bool hover: !!(control && control.hovered === true)
    readonly property bool error: !!(control && control.hasError === true)
    color: Theme.darkMode ? "#151D30" : "#FFFAE4"
    radius: 5
    border.width: root.focus || root.error ? 2 : 1
    border.color: root.error ? "#DC4F62" : root.focus ? (Theme.darkMode ? "#FFE375" : "#AE283A") : (Theme.darkMode ? "#9A8945" : "#C4B474")
    Rectangle { x: 1; y: 1; width: Math.max(0,parent.width-2); height: 2; color: Theme.darkMode ? "#050912" : "#E3D497"; opacity: .55; radius: 1 }
    Rectangle { x: 3; y: Math.max(0,parent.height-3); width: Math.max(0,parent.width-6); height: 1; color: Theme.darkMode ? "#D6C06B" : "#E5D287"; opacity: .52 }
}
