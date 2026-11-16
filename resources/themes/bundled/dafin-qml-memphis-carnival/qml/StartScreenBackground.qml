import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#FFF3DA"
    border.width: 0
    border.color: "#32516A"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#FFF4DE" }
        GradientStop { position: 1; color: "#FAF2DF" }
    }
    // Confetti dot motif is background-only and never captures input.
    Item { x: Math.max(0,parent.width-99); y: 5; width: 90; height: 33; opacity: 0.65
        Repeater { model: 6
            Rectangle { x: (index%3)*28; y: Math.floor(index/3)*20; width: 5; height: 5;
                        radius: index%2 ? 0 : 3; color: index%3 === 0 ? "#DA365E" : (index%3 === 1 ? "#097FB2" : "#E5B53B") }
        }
    }
    Rectangle { x: 8; y: Math.max(0,parent.height-6); width: Math.min(50,parent.width/4); height: 3; color: "#DA365E" }
}
