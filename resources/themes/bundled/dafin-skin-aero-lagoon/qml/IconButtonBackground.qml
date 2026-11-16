import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool downState: !!(control && control.down === true)
    radius: 12
    color: root.downState ? "#469fc0" : (control && (control.active || control.hovered) ? "#b8e5ed" : "#00000000")
    border.width: 1
    border.color: control && control.visualFocus ? "#0787ac" : "#51b6d0"
    opacity: control && !control.enabled ? 0.45 : 1
    
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 2; height: Math.max(1,parent.height * 0.35); radius: parent.radius; color: "#66FFFFFF"; opacity: root.downState ? 0.16 : 0.8 }
}
