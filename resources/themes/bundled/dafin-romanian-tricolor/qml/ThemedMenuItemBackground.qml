import QtQuick
import Drift

// Romanian Core (tricolor): original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool highlighted: !!(control && (control.highlighted === true || control.hovered === true || control.down === true))
    color: highlighted ? (Theme.darkMode ? "#38435F" : "#F0E2A9") : "transparent"
    radius: 3
    Rectangle { x: 0; y: 2; width: root.highlighted ? 3 : 0; height: Math.max(0,parent.height-4); color: Theme.darkMode ? "#EDD05C" : "#A92638" }
}
