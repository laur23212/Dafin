import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 16
    color: "#FFF5E4"
    border.width: 1
    border.color: "#AD8A68"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#FFF6E7" }
        GradientStop { position: 1; color: "#FBEED9" }
    }
    // Minimal 1950s starburst engraving, right side of panel chrome.
    Item { x: Math.max(0,parent.width-58); y: 2; width: 48; height: 48; opacity: 0.47
        Repeater { model: 10
            Rectangle { x: 23; y: 11; width: 1; height: 13; color: "#A94E2B";
                        rotation: index*36; transformOrigin: Item.Bottom }
        }
        Rectangle { x: 21; y: 21; width: 5; height: 5; radius: 3; color: "#A94E2B" }
    }
}
