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
    readonly property color baseFill: root.selectedState ? "#255E7E" : (root.pressedState ? "#F0F3F7" : (root.hoveredState ? "#CAD7E1" : "#E6EBF1"))
    radius: 6
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#386B8B" : (root.selectedState ? "#386B8B" : "#BAC7D4")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.baseFill }
            GradientStop { position: 1.0; color: root.baseFill }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 1; y: parent.height - 1; width: Math.max(0,parent.width - 2); height: 1; color: "#BAC7D4"; opacity: 0.62 }
    }
}
