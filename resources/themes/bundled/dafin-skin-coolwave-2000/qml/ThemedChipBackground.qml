import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool downState: !!(control && control.down === true)
    readonly property bool primaryState: !!(control && control.selected)
    radius: 1
    color: root.primaryState ? "#126f87" : (root.downState ? "#6e7581" : "#b6bac1")
    border.width: 2
    border.color: control && control.visualFocus ? "#147b92" : "#536270"
    opacity: control && !control.enabled ? 0.45 : 1
    gradient: Gradient {
            GradientStop { position: 0; color: root.primaryState ? "#147b92" : (root.downState ? "#6e7581" : "#E7E8E9") }
            GradientStop { position: 1; color: root.primaryState ? "#147b92" : (root.downState ? "#b6bac1" : "#A3A8B0") }
        }
    Rectangle { x: 1; y: 1; width: Math.max(0,parent.width-2); height: 1; color: root.downState ? "#6e7581" : "#ffffff" }
    Rectangle { x: 1; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.downState ? "#6e7581" : "#ffffff" }
    Rectangle { x: 1; y: parent.height-2; width: Math.max(0,parent.width-2); height: 1; color: root.downState ? "#ffffff" : "#6e7581" }
    Rectangle { x: parent.width-2; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.downState ? "#ffffff" : "#6e7581" }
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 0; height: 1; color: "#48505F" }
}
