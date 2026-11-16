import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#18191B"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#E60012" : "#E8E8E8"
    Rectangle { x: 0; y: 0; width: Math.min(7,parent.width); height: parent.height; color: "#6EE60012" }
    Item { x: Math.max(0,parent.width-66); y: Math.max(0,parent.height-12); width: 60; height: 10; opacity: 0.50
        Repeater { model: 12; Rectangle { x: index*5; y: 0; width: index%3===0 ? 3 : 1; height: 9; color: "#F4F4F4" } }
    }
}
