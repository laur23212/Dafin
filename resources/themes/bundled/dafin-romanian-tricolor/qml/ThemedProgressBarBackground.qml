import QtQuick
import Drift

// Romanian Core (tricolor): original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property real fraction: control ? Math.max(0,Math.min(1,Number(control.value || 0))) : 0
    color: Theme.darkMode ? "#121A2F" : "#E9DCAA"
    radius: 3
    border.width: 1
    border.color: Theme.darkMode ? "#AB9746" : "#C1AF65"
    clip: true
    Rectangle {
        x: 0; y: 0; width: root.width * root.fraction; height: root.height; radius: 3
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#1F4FC0" }
            GradientStop { position: 0.5; color: "#FCD116" }
            GradientStop { position: 1.0; color: "#CE1126" }
        }
    }
    Repeater { model: Math.max(0,Math.floor(root.width/11)); delegate: Rectangle { x:index*11; y:0; height:root.height; width:1; color:"#45AB963F" } }
}
