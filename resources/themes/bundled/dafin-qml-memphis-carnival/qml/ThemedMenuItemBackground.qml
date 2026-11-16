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
    readonly property color baseFill: root.selectedState ? "#1479AB" : (root.pressedState ? "#FFF2D9" : (root.hoveredState ? "#E2D0DD" : "#E3EDF5"))
    radius: 3
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#DA365E" : (root.selectedState ? "#DA365E" : "#32516A")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#358CB7" : (root.pressedState ? "#FFF2D9" : "#E9F1F7") }
            GradientStop { position: 1.0; color: root.selectedState ? "#1479AB" : root.pressedState ? "#E3EDF5" : "#EAEEEE" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#F2F7FA" }
    }
    Rectangle { x: 0; y: 0; width: 3; height: parent.height; color: "#DA365E" }
}
