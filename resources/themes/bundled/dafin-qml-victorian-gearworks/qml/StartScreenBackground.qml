import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#19140F"
    border.width: 0
    border.color: "#A78350"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#211C17" }
        GradientStop { position: 1; color: "#241D15" }
    }
    // Riveted iron frame and copper-piping lip.
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; color: "#8BA66539" }
    Rectangle { x: 3; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-6); height: 1; color: "#91D8B56C" }
    Repeater { model: 4
        Rectangle { x: index%2===0 ? 7 : Math.max(7,parent.width-11);
                    y: index<2 ? 7 : Math.max(7,parent.height-11);
                    width: 5; height: 5; radius: 3; color: "#D8B56C";
                    border.width: 1; border.color: "#4A2F20" }
    }
}
