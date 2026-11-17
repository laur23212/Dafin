import QtQuick
import Drift

// Romanian Core 2: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    color: Theme.darkMode ? "#2A344E" : "#F1E4B1"
    radius: 3
    Rectangle { x:0; y:0; width:3; height:parent.height; color:Theme.darkMode ? "#ECD36E" : "#B92A3D" }
    Rectangle { x:0; y:Math.max(0,parent.height-1); height:1; width:parent.width; color:Theme.darkMode ? "#958542" : "#C7B361" }
}
