import QtQuick
import Drift

// Romanian Core 2: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool chosen: !!(control && control.selected === true)
    color: chosen ? (Theme.darkMode ? "#9B2333" : "#CA2E43") : (Theme.darkMode ? "#1F2840" : "#F9EEC0")
    radius: 6
    border.width: 1
    border.color: Theme.darkMode ? "#C9B461" : "#AB9437"
    Rectangle { x: 3; y: 2; width:Math.max(0,parent.width-6); height:1; color:"#8FFFF4C7" }
}
