import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#020813"
    border.width: 0
    border.color: "#2B7088"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#0B111B" }
        GradientStop { position: 1; color: "#05101D" }
    }
    // Static bioluminescent pools: softly translucent nested orbs (no high-frequency animation).
    Item { x: Math.max(0,parent.width-96); y: 2; width: 89; height: 70; opacity: .42
        Rectangle { x: 3; y: 3; width: 63; height: 63; radius: 32; color: "#185CDFF2" }
        Rectangle { x: 15; y: 11; width: 40; height: 40; radius: 20; color: "#3668E5FA" }
        Rectangle { x: 48; y: 26; width: 18; height: 18; radius: 9; color: "#54AB92ED" }
        Rectangle { x: 72; y: 50; width: 4; height: 4; radius: 2; color: "#93D6F9" }
    }
    Rectangle { x: 8; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-16); height: 1; color: "#9968E5FA" }
}
