import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 11
    color: "#1B2935"
    border.width: 1
    border.color: "#AB8D6B"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#23303C" }
        GradientStop { position: 1; color: "#1F2D39" }
    }
    // Stationary etched watch-dial ticks and metal flutes, no moving mechanism.
    Item { x: Math.max(0,parent.width-61); y: 2; width: 55; height: 55; opacity: .52; clip: true
        Rectangle { x: 5; y: 5; width: 45; height: 45; radius: 23; color: "transparent"; border.width: 1; border.color: "#D9B48D" }
        Repeater { model: 12; Rectangle { x: 26; y: 5; width: 1; height: 6; color: "#D9B48D"; rotation: index*30; transformOrigin: Item.Bottom } }
        Rectangle { x: 25; y: 25; width: 6; height: 6; radius: 3; color: "#D9B48D" }
    }
    Rectangle { x: 7; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-14); height: 1; color: "#66D9B48D" }
}
