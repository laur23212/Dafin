import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    id: root
    property var control
    color: "#fff3fa"
    radius: 21
    border.width: 1
    border.color: "#e59bbf"
    gradient: Gradient {
        GradientStop {position: 0.0; color: "#FFF7FC"}
        GradientStop {position: 1.0; color: "#fff3fa"}
    }
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; radius: 2; color: "#BFFFFFFF" }
}
