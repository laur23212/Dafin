import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    radius: 4
    color: Theme.darkMode ? "#E1B669" : "#AD3941"
    border.width: 1
    border.color: Theme.darkMode ? "#4A2920" : "#FFF0D3"
    Rectangle { anchors.centerIn: parent; width:Math.max(2,parent.width*.24); height:Math.max(2,parent.height*.24); rotation:45; color: Theme.darkMode ? "#952C36" : "#FFE7B3" }
}
