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
    readonly property color baseFill: root.selectedState ? "#91502D" : (root.pressedState ? "#F0DDC2" : (root.hoveredState ? "#E0B993" : "#EACDA7"))
    radius: 13
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#A94E2B" : (root.selectedState ? "#A94E2B" : "#AD8A68")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#A0684A" : (root.pressedState ? "#F0DDC2" : "#EFD8BB") }
            GradientStop { position: 1.0; color: root.selectedState ? "#91502D" : root.pressedState ? "#EACDA7" : "#ECD1AE" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#F6E8D7" }
    }
    Rectangle { x: 5; y: 4; width: 10; height: 2; color: "#A94E2B"; rotation: -15 }
    Rectangle { x: Math.max(0,parent.width-16); y: Math.max(0,parent.height-6); width: 9; height: 2; color: "#688564"; rotation: 12 }
}
