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
    readonly property color baseFill: root.selectedState ? "#42634D" : (root.pressedState ? "#F2EEE5" : (root.hoveredState ? "#D3D5C8" : "#EBE9DE"))
    radius: 5
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#536E57" : (root.selectedState ? "#536E57" : "#A8AC9C")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#5C7966" : (root.pressedState ? "#F2EEE5" : "#F0EEE6") }
            GradientStop { position: 1.0; color: root.selectedState ? "#42634D" : root.pressedState ? "#EBE9DE" : "#EDEAE0" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#F6F5F0" }
    }
    // Japanese restraint: a single handmade-ink baseline.
    Rectangle { x: 7; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-14); height: 1; color: "#667C715B" }
}
