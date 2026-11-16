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
    readonly property color baseFill: root.selectedState ? "#22DCEB" : (root.pressedState ? "#101823" : (root.hoveredState ? "#1D4551" : "#1C2834"))
    radius: 3
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#22DCEB" : (root.selectedState ? "#22DCEB" : "#3C6776")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#41E1EE" : (root.pressedState ? "#101823" : "#2E3944") }
            GradientStop { position: 1.0; color: root.selectedState ? "#22DCEB" : root.pressedState ? "#1C2834" : "#192430" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 0; width: Math.max(0,parent.width-6); height: root.activeState ? 2 : 1; color: root.activeState ? "#22DCEB" : "#3C6776" }
    Rectangle { x: parent.width-7; y: 2; width: 5; height: 2; color: "#BC70FF" }
    }
    Rectangle { width: 6; height: 2; x: Math.max(0,parent.width-8); y: Math.max(0,parent.height-4); color: "#BC70FF" }
}
