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
    readonly property color baseFill: root.selectedState ? "#D4AF66" : (root.pressedState ? "#191711" : (root.hoveredState ? "#6C583A" : "#584631"))
    radius: 3
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#D8B56C" : (root.selectedState ? "#D8B56C" : "#A78350")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#DABA7B" : (root.pressedState ? "#191711" : "#655541") }
            GradientStop { position: 1.0; color: root.selectedState ? "#D4AF66" : root.pressedState ? "#584631" : "#483A29" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#856D46" }
    }
    Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 1; color: "transparent"; border.width: 1; border.color: "#7ED8B56C" }
    Repeater { model: 2; Rectangle { x: index===0 ? 4 : Math.max(0,parent.width-8); y: Math.max(1,parent.height/2-2); width: 4; height: 4; radius: 2; color: "#CBA258"; border.width: 1; border.color: "#50321F" } }
}
