import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool pressed: !!(control && control.down === true)
    color: pressed ? "#85252F" : (Theme.darkMode ? "#BA3440" : "#AB2D39")
    radius: 6
    border.width: 1
    border.color: "#E8C895"
    Rectangle { anchors.fill:parent; anchors.margins:3; color:"transparent"; radius:4; border.width:1; border.color:"#78F4DDB6" }
    Rectangle { x:5; y:3; width:6; height:6; rotation:45; color:"#F7E4B9"; opacity:.78 }
}
