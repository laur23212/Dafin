import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool pressedState: !!(control && control.down === true)
    readonly property bool focusedState: !!(control && (control.visualFocus === true || control.activeFocus === true))
    readonly property bool selectedState: !!(control && (control.selected === true || control.active === true || control.checked === true || control.variant === "primary"))
    readonly property bool hoveredState: !!(control && control.hovered === true)
    readonly property bool activeState: selectedState || hoveredState || focusedState
    readonly property bool idleGhost: !root.activeState
    readonly property color baseFill: root.selectedState ? "#774A9B" : (root.pressedState ? "#E1D5E9" : (root.hoveredState ? "#DDBDEC" : "#E9CFF4"))
    radius: 21
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#9B5CBF" : (root.selectedState ? "#9B5CBF" : "#CBA9DF")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#8A63A9" : (root.pressedState ? "#E1D5E9" : "#EEDAF7") }
            GradientStop { position: 1.0; color: root.selectedState ? "#774A9B" : root.pressedState ? "#E9CFF4" : "#E7D0F1" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#F5E9FA" }
    }
    Rectangle { x: 5; y: 3; width: Math.max(0,parent.width-10); height: Math.max(0,parent.height*.25); radius: 12; color: "#B5FFFFFF" }
    Rectangle { x: 5; y: Math.max(0,parent.height-6); width: Math.max(0,parent.width-10); height: 3; radius: 2; color: "#4B907AA5" }
    Rectangle { x: 3; y: 4; width: 3; height: Math.max(0,parent.height-10); radius: 2; color: "#98FFFFFF" }
}
