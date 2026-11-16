import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#0B132B"
    border.width: 0
    border.color: "#A68B4E"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#141B32" }
        GradientStop { position: 1; color: "#101931" }
    }
    // Original 1920s-inspired fan geometry, confined to panel background.
    Rectangle { x: 6; y: 6; width: Math.max(0,parent.width-12); height: 1; color: "#66D4AF37" }
    Rectangle { x: 6; y: Math.max(0,parent.height-7); width: Math.max(0,parent.width-12); height: 1; color: "#55D4AF37" }
    Item { x: Math.max(0,parent.width-88); y: 0; width: 80; height: 55; clip: true; opacity: 0.42
        Repeater { model: 9
            Rectangle { x: 39; y: 0; width: 1; height: 62; color: "#D4AF37";
                        rotation: (index-4)*15; transformOrigin: Item.Bottom }
        }
    }
}
