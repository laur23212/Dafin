import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 21
    color: "#FFF8FC"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#9B5CBF" : "#CBA9DF"
    Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 19; color: "transparent"; border.color: "#9FFFFFFF"; border.width: 3 }
    Rectangle { x: 10; y: Math.max(0,parent.height-7); width: Math.max(0,parent.width-20); height: 3; radius: 2; color: "#44987FB8" }
}
