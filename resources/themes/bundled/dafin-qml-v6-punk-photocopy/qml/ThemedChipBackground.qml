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
    readonly property color baseFill: root.selectedState ? "#C92D3B" : (root.pressedState ? "#0F1012" : (root.hoveredState ? "#5F4145" : "#444042"))
    radius: 0
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#EF4953" : (root.selectedState ? "#EF4953" : "#8B7671")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#D14A56" : (root.pressedState ? "#0F1012" : "#534F51") }
            GradientStop { position: 1.0; color: root.selectedState ? "#C92D3B" : root.pressedState ? "#444042" : "#373436" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#804348" }
    }
    Rectangle { x: 3; y: Math.max(0,parent.height-5); width: Math.max(0,parent.width-6); height: 2; rotation: -1; color: "#EF4953" }
    Rectangle { x: Math.max(0,parent.width-11); y: 2; width: 8; height: 3; rotation: 11; color: "#88F4EEE7" }
}
