import QtQuick
import Drift

// Romanian Core 2: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    radius: 4
    color: Theme.darkMode ? "#F7D653" : "#BB2B3E"
    border.width: 1
    border.color: Theme.darkMode ? "#56141D" : "#FFF6D3"
    Rectangle { anchors.centerIn: parent; width:Math.max(2,parent.width*.24); height:Math.max(2,parent.height*.24); rotation:45; color: Theme.darkMode ? "#9D2434" : "#FFF0B3" }
}
