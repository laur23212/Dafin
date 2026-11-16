import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool downState: !!(control && control.down === true)
    readonly property bool primaryState: !!(control && control.variant === "primary")
    readonly property color primaryFill: "#0787ac"
    readonly property color pressedFill: "#469fc0"
    readonly property color defaultFill: "#b8e5ed"
    radius: 12
    color: control && control.variant === "ghost" && !control.hovered && !control.down ? "transparent" : (root.primaryState ? root.primaryFill : (root.downState ? root.pressedFill : root.defaultFill))
    border.width: 1
    border.color: control && control.visualFocus ? "#0787ac" : "#51b6d0"
    opacity: control && !control.enabled ? 0.45 : 1
    gradient: Gradient {
            GradientStop { position: 0; color: root.primaryState ? "#0787ac" : (root.downState ? "#469fc0" : "#F3FCFF") }
            GradientStop { position: 1; color: root.primaryState ? "#0787ac" : (root.downState ? "#b8e5ed" : "#72CCDB") }
        }
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 2; height: Math.max(1,parent.height * 0.35); radius: parent.radius; color: "#66FFFFFF"; opacity: root.downState ? 0.16 : 0.8 }
}
