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
    readonly property bool idleGhost: !!(control && control.variant === "ghost" && !root.activeState)
    readonly property color baseFill: root.selectedState ? "#A7327B" : (root.pressedState ? "#F1D9EC" : (root.hoveredState ? "#F1BED9" : "#FBD5E8"))
    radius: 20
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#BD438B" : (root.selectedState ? "#BD438B" : "#D79BBC")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#B34F8D" : (root.pressedState ? "#F1D9EC" : "#FCDFED") }
            GradientStop { position: 1.0; color: root.selectedState ? "#A7327B" : root.pressedState ? "#FBD5E8" : "#F8D6E9" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#FDECF5" }
    }
    Rectangle { x: 5; y: 3; width: Math.max(0,parent.width-10); height: 6; radius: 3; color: "#BAFFFFFF" }
    Text { x: Math.max(0,parent.width-16); y: 1; text: "★"; font.pixelSize: 9; color: "#A7327B"; opacity: 0.8 }
}
