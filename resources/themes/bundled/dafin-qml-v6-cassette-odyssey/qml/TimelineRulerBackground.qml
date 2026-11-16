import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#F0E7D7"
    border.width: 0
    border.color: "#887967"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#F2EADB" }
        GradientStop { position: 1; color: "#EAE0CF" }
    }
    // Angled orange-brown aerospace stripe accents in panel margins.
    Rectangle { x: 3; y: 3; width: Math.min(44,parent.width/4); height: 3; color: "#A63D23" }
    Rectangle { x: 3; y: 8; width: Math.min(44,parent.width/4); height: 3; color: "#B86B2B" }
    Rectangle { x: 3; y: 13; width: Math.min(44,parent.width/4); height: 2; color: "#A63D23" }
    Rectangle { x: Math.max(0,parent.width-39); y: Math.max(0,parent.height-8); width: 8; height: 3; radius: 0; color: "#E94C30" }
    Rectangle { x: Math.max(0,parent.width-28); y: Math.max(0,parent.height-8); width: 8; height: 3; radius: 0; color: "#AC4630" }
}
