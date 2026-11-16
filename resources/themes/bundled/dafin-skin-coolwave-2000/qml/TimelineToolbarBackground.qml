import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    id: root
    property var control
    color: "#d3d5d7"
    radius: 0
    border.width: 0
    border.color: "#536270"
    gradient: Gradient {
        GradientStop {position: 0.0; color: "#d3d5d7"}
        GradientStop {position: 1.0; color: "#d3d5d7"}
    }
    Rectangle { x: 1; y: 1; width: Math.max(0,parent.width-2); height: 1; color: "#ffffff" }
    Rectangle { x: 1; y: Math.max(0,parent.height-2); width: Math.max(0,parent.width-2); height: 1; color: "#6e7581" }
}
