import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#221821"
    border.width: 0
    border.color: "#9A748D"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#2A2029" }
        GradientStop { position: 1; color: "#291E29" }
    }
    // Delicate plum filigree without covering editable content.
    Rectangle { x: 8; y: 8; width: Math.max(0,parent.width-16); height: 1; color: "#5BDAA8CA" }
    Item { x: Math.max(0,parent.width-45); y: 6; width: 34; height: 26; opacity: 0.50
        Repeater { model: 3
            Rectangle { x: index*12 + 4; y: 7 + (index%2)*5; width: 5; height: 5;
                        rotation: 45; color: "#D8B4D3" }
        }
    }
}
