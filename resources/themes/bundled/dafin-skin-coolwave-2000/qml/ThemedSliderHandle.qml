import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 0
    color: "#8dc7d7"
    border.color: control && control.visualFocus ? "#147b92" : "#536270"
    border.width: 2
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; radius: 1; color: "#ffffff" }
}
