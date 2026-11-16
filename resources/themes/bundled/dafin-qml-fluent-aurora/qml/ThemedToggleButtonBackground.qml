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
    readonly property color baseFill: root.selectedState ? "#176DAB" : (root.pressedState ? "#DAEDF4" : (root.hoveredState ? "#AFD9EB" : "#C4E7F1"))
    radius: 14
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#408ECD" : (root.selectedState ? "#408ECD" : "#77B4C8")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#3781B7" : (root.pressedState ? "#DAEDF4" : "#D2EDF4") }
            GradientStop { position: 1.0; color: root.selectedState ? "#176DAB" : root.pressedState ? "#C4E7F1" : "#CAE8F2" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: Math.max(0,parent.height*0.38); radius: root.radius; color: "#55FFFFFF"; opacity: root.pressedState ? 0.18 : 0.75 }
    }
    Rectangle { x: 3; y: 3; width: Math.max(0,parent.width-6); height: Math.max(0,parent.height*.28); radius: root.radius; color: "#55FFFFFF"; visible: !root.idleGhost }
}
