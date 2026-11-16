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
    readonly property color baseFill: root.selectedState ? "#286D53" : (root.pressedState ? "#E7F1E3" : (root.hoveredState ? "#C4DEC4" : "#E0EFD5"))
    radius: 11
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#2E856A" : (root.selectedState ? "#2E856A" : "#8EB7A2")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#46816B" : (root.pressedState ? "#E7F1E3" : "#E7F3DF") }
            GradientStop { position: 1.0; color: root.selectedState ? "#286D53" : root.pressedState ? "#E0EFD5" : "#E2F0D8" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#F1F8EC" }
    }
    Rectangle { x: 5; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-10); height: 2; radius: 1; color: "#60D6A334" }
}
