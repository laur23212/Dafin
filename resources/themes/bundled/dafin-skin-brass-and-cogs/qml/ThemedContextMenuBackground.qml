import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    id: root
    property var control
    color: "#3a2b20"
    radius: 5
    border.width: 0
    border.color: "#966d42"
    gradient: Gradient {
        GradientStop {position: 0.0; color: "#4C3826"}
        GradientStop {position: 1.0; color: "#3a2b20"}
    }
    Rectangle { x: 8; y: 2; width: Math.max(0,parent.width-16); height: 1; color: "#89704E" }
}
