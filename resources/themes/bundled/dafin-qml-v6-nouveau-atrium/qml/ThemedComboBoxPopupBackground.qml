import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 12
    color: "#284B43"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#E7C57A" : "#B4A474"
    // Curls and mirrored leaf flourishes are contained in the corners.
    Item { x: Math.max(0,parent.width-67); y: 5; width: 60; height: 53; opacity: 0.45; clip: true
        Rectangle { x: 5; y: 3; width: 51; height: 50; radius: 25; color: "transparent"; border.width: 2; border.color: "#E7C57A" }
        Rectangle { x: 16; y: 10; width: 28; height: 30; radius: 15; color: "transparent"; border.width: 1; border.color: "#CF9CB5" }
        Rectangle { x: 8; y: 32; width: 15; height: 7; radius: 7; rotation: -24; color: "#88B8C78D" }
        Rectangle { x: 35; y: 24; width: 16; height: 8; radius: 8; rotation: 26; color: "#88E0B26B" }
    }
    Rectangle { x: 9; y: Math.max(0,parent.height-5); width: Math.max(0,parent.width-18); height: 1; color: "#77E7C57A" }
}
