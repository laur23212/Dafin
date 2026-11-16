import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property real fraction: control ? Math.max(0,Math.min(1,Number(control.value || 0))) : 0
    color: Theme.darkMode ? "#17252A" : "#DFCFB4"
    radius: 3
    border.width: 1
    border.color: Theme.darkMode ? "#997C58" : "#B19975"
    clip: true
    Rectangle { x: 0; y: 0; width: root.width * root.fraction; height: root.height; color: Theme.darkMode ? "#B43842" : "#A92F37"; radius: 3 }
    Repeater { model: Math.max(0,Math.floor(root.width/11)); delegate: Rectangle { x:index*11; y:0; height:root.height; width:1; color:"#45997551" } }
}
