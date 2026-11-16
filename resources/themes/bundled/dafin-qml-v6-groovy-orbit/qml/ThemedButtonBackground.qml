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
    readonly property color baseFill: root.selectedState ? "#A52865" : (root.pressedState ? "#EFC185" : (root.hoveredState ? "#E88165" : "#F19265"))
    radius: 16
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#B62966" : (root.selectedState ? "#B62966" : "#A34F6D")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#B2467B" : (root.pressedState ? "#EFC185" : "#F4AB88") }
            GradientStop { position: 1.0; color: root.selectedState ? "#A52865" : root.pressedState ? "#F19265" : "#F09E6D" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#F9CEBA" }
    }
    Rectangle { x: 6; y: 4; width: 12; height: 6; rotation: -18; radius: 5; color: "#88FFDE67" }
    Rectangle { x: Math.max(0,parent.width-17); y: Math.max(0,parent.height-9); width: 12; height: 5; radius: 5; rotation: 20; color: "#99B62966" }
}
