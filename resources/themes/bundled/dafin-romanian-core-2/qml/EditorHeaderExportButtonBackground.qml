import QtQuick
import Drift

// Romanian Core 2: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool pressed: !!(control && control.down === true)
    color: pressed ? "#8B1F2E" : (Theme.darkMode ? "#C22C40" : "#B0283A")
    radius: 6
    border.width: 1
    border.color: "#F8E185"
    Rectangle { anchors.fill:parent; anchors.margins:3; color:"transparent"; radius:4; border.width:1; border.color:"#78FFEEAB" }
    Rectangle { x:5; y:3; width:6; height:6; rotation:45; color:"#FFEFB1"; opacity:.78 }
}
