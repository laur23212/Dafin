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
    readonly property bool idleGhost: false
    readonly property color baseFill: root.selectedState ? "#9D3540" : (root.pressedState ? "#F0E0CC" : (root.hoveredState ? "#DFC0AC" : "#EAD8BF"))
    radius: 9
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#A53F48" : (root.selectedState ? "#A53F48" : "#987B6F")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#AB515B" : (root.pressedState ? "#F0E0CC" : "#EFE1CE") }
            GradientStop { position: 1.0; color: root.selectedState ? "#9D3540" : root.pressedState ? "#EAD8BF" : "#ECDAC2" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#F6EDE2" }
    }
    Rectangle { x: 4; y: 4; width: 4; height: 4; rotation: 45; color: "#87A53F48" }
    Rectangle { x: Math.max(0,parent.width-9); y: Math.max(0,parent.height-9); width: 4; height: 4; rotation: 45; color: "#88376B55" }
}
