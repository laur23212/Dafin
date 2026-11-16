import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 3
    color: "#e6be7d"
    border.color: control && control.visualFocus ? "#d1a05b" : "#966d42"
    border.width: 2
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; radius: 1; color: "#eed4a7" }
}
