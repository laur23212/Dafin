import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    color: "#0d0907"; radius: 5
    border.color: "#966d42"; border.width: 1
    Rectangle {
        x: 1; y: 1; height: Math.max(0,parent.height-2)
        width: Math.max(0,(parent.width-2)*Math.max(0,Math.min(1,control && control.value !== undefined ? control.value : 0)))
        radius: parent.radius; color: "#d1a05b"
    }
}
