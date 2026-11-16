import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property real fraction: control ? Math.max(0,Math.min(1, control.visualPosition || 0)) : 0
    color: Theme.darkMode ? "#121F24" : "#DAC5A7"
    radius: 3
    border.width: 1
    border.color: Theme.darkMode ? "#9D8461" : "#AD8D64"
    Rectangle { x: 1; y: 1; width: Math.max(0,(parent.width-2)*root.fraction); height: Math.max(0,parent.height-2); radius: 2; color: Theme.darkMode ? "#C14B54" : "#A8323D" }
    // woven thread-like rail stripe
    Rectangle { x: 2; y: parent.height/2; width: Math.max(0,parent.width-4); height: 1; color: "#70F8E3B7" }
}
