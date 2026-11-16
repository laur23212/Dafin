import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    id: root
    property var control
    color: "#ecfbff"
    radius: 0
    border.width: 0
    border.color: "#51b6d0"
    gradient: Gradient {
        GradientStop {position: 0.0; color: "#F1FCFF"}
        GradientStop {position: 1.0; color: "#DFF5FA"}
    }
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width - 4); height: Math.max(1,parent.height*0.14); radius: parent.radius; color: "#44FFFFFF" }
}
