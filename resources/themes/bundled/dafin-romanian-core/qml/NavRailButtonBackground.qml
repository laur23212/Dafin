import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool chosen: !!(control && control.selected === true)
    color: chosen ? (Theme.darkMode ? "#8E303B" : "#B7414B") : (Theme.darkMode ? "#25373A" : "#F1E4C8")
    radius: 6
    border.width: 1
    border.color: Theme.darkMode ? "#B89C72" : "#98734A"
    Rectangle { x: 3; y: 2; width:Math.max(0,parent.width-6); height:1; color:"#8FFFE8C7" }
}
