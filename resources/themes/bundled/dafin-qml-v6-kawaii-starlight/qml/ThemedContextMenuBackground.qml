import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 24
    color: "#FFF8FC"
    border.width: 1
    border.color: "#D79BBC"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#FFF9FC" }
        GradientStop { position: 1; color: "#FEF2F8" }
    }
    Item { x: Math.max(0,parent.width-63); y: 4; width: 55; height: 35; opacity: 0.62
        Text { x: 3; y: 4; text: "★"; font.pixelSize: 16; color: "#BD438B" }
        Text { x: 26; y: 6; text: "♥"; font.pixelSize: 16; color: "#72B4CA" }
        Text { x: 38; y: 20; text: "✦"; font.pixelSize: 12; color: "#D789BC" }
    }
    Rectangle { x: 6; y: Math.max(0,parent.height-5); width: Math.max(0,parent.width-12); height: 2; radius: 1; color: "#66BD438B" }
}
