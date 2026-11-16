import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 15
    color: "#071A2B"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#68E5FA" : "#2B7088"
    // Static bioluminescent pools: softly translucent nested orbs (no high-frequency animation).
    Item { x: Math.max(0,parent.width-96); y: 2; width: 89; height: 70; opacity: .42
        Rectangle { x: 3; y: 3; width: 63; height: 63; radius: 32; color: "#185CDFF2" }
        Rectangle { x: 15; y: 11; width: 40; height: 40; radius: 20; color: "#3668E5FA" }
        Rectangle { x: 48; y: 26; width: 18; height: 18; radius: 9; color: "#54AB92ED" }
        Rectangle { x: 72; y: 50; width: 4; height: 4; radius: 2; color: "#93D6F9" }
    }
    Rectangle { x: 8; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-16); height: 1; color: "#9968E5FA" }
}
