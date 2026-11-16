import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    id: root
    property var control
    color: "#3a2b20"
    radius: 5
    border.width: 2
    border.color: "#966d42"
    gradient: Gradient {
        GradientStop {position: 0.0; color: "#4C3826"}
        GradientStop {position: 1.0; color: "#3a2b20"}
    }
    Repeater { model: 4
        Rectangle {
            width: 5; height: 5; radius: 2.5; color: "#C79E64";
            border.color: "#291A10"; border.width: 1
            x: (index % 2 === 0) ? 6 : Math.max(6, parent.width - 11)
            y: (index < 2) ? 5 : Math.max(5, parent.height - 10)
        }
    }
    Rectangle { x: 8; y: 2; width: Math.max(0,parent.width-16); height: 1; color: "#89704E" }
}
