import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    color: Theme.darkMode ? "#324642" : "#E8D7BA"
    radius: 3
    Rectangle { x:0; y:0; width:3; height:parent.height; color:Theme.darkMode ? "#DABB80" : "#AA3940" }
    Rectangle { x:0; y:Math.max(0,parent.height-1); height:1; width:parent.width; color:Theme.darkMode ? "#866E51" : "#B69A72" }
}
