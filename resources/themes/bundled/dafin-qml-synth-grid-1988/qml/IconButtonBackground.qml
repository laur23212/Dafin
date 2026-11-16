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
    readonly property color baseFill: root.selectedState ? "#EF68CD" : (root.pressedState ? "#100C2B" : (root.hoveredState ? "#462B64" : "#24204F"))
    radius: 1
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#FA67D5" : (root.selectedState ? "#FA67D5" : "#7656BA")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#F17DD4" : (root.pressedState ? "#100C2B" : "#36325D") }
            GradientStop { position: 1.0; color: root.selectedState ? "#EF68CD" : root.pressedState ? "#24204F" : "#1F1B46" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 0; width: Math.max(0,parent.width-6); height: root.activeState ? 2 : 1; color: root.activeState ? "#FA67D5" : "#7656BA" }
    Rectangle { x: parent.width-7; y: 2; width: 5; height: 2; color: "#51DDF0" }
    }
}
