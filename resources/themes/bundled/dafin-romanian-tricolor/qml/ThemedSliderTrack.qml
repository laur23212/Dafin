import QtQuick
import Drift

// Romanian Core (tricolor): original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property real fraction: control ? Math.max(0,Math.min(1, control.visualPosition || 0)) : 0
    color: Theme.darkMode ? "#0E1528" : "#E5D79C"
    radius: 3
    border.width: 1
    border.color: Theme.darkMode ? "#AF9C4F" : "#C0AA51"
    Rectangle { x: 1; y: 1; width: Math.max(0,(parent.width-2)*root.fraction); height: Math.max(0,parent.height-2); radius: 2; color: Theme.darkMode ? "#D23A4E" : "#B2283B" }
    // woven thread-like rail stripe
    Rectangle { x: 2; y: parent.height/2; width: Math.max(0,parent.width-4); height: 1; color: "#70FFEFB0" }
}
