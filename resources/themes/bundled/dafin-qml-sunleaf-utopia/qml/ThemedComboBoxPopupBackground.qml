import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 11
    color: "#F8FAEA"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#2E856A" : "#8EB7A2"
    // Abstract paired leaves in the chrome corner.
    Item { x: Math.max(0,parent.width-53); y: 6; width: 40; height: 24; opacity: 0.62
        Rectangle { x: 4; y: 6; width: 20; height: 9; radius: 8; rotation: -30; color: "#2E856A" }
        Rectangle { x: 17; y: 10; width: 17; height: 8; radius: 7; rotation: 29; color: "#B9A83A" }
        Rectangle { x: 13; y: 4; width: 2; height: 19; rotation: -40; color: "#487D56" }
    }
    Rectangle { x: 8; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-16); height: 1; color: "#5E2E856A" }
}
