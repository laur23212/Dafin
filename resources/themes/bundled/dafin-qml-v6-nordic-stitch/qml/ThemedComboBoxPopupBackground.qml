import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 9
    color: "#FFF8EC"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#A53F48" : "#987B6F"
    // Stitched top border with Scandinavian geometric yarn marks.
    Item { x: 7; y: 3; width: Math.max(0,parent.width-14); height: 12; clip: true; opacity: .60
        Repeater { model: 14; Rectangle { x: index*12; y: index%2 ? 3 : 5; width: 5; height: 2; rotation: index%2 ? -45 : 45; color: index%3 ? "#A53F48" : "#376B55" } }
    }
    Rectangle { x: 7; y: Math.max(0,parent.height-5); width: Math.max(0,parent.width-14); height: 1; color: "#7FA53F48" }
}
