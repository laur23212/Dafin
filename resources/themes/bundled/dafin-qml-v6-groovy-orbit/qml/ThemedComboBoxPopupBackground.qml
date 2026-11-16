import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 16
    color: "#FFDC9D"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#B62966" : "#A34F6D"
    // Static 60s paisley arcs and ribbon bands, away from main content area.
    Item { x: Math.max(0,parent.width-72); y: 4; width: 64; height: 57; clip: true; opacity: 0.33
        Repeater { model: 3; Rectangle { x: 3+index*9; y: 2+index*5; width: 51-index*13; height: 47-index*12; radius: 25; color: "transparent"; border.width: 3; border.color: index%2 ? "#6D4D99" : "#B62966" } }
    }
    Rectangle { x: 5; y: Math.max(0,parent.height-6); width: Math.min(parent.width/3,72); height: 3; radius: 2; color: "#B62966"; rotation: -3 }
}
