import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 2
    color: "#F0E7D7"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#A63D23" : "#887967"
    // Angled orange-brown aerospace stripe accents in panel margins.
    Rectangle { x: 3; y: 3; width: Math.min(44,parent.width/4); height: 3; color: "#A63D23" }
    Rectangle { x: 3; y: 8; width: Math.min(44,parent.width/4); height: 3; color: "#B86B2B" }
    Rectangle { x: 3; y: 13; width: Math.min(44,parent.width/4); height: 2; color: "#A63D23" }
    Rectangle { x: Math.max(0,parent.width-39); y: Math.max(0,parent.height-8); width: 8; height: 3; radius: 0; color: "#E94C30" }
    Rectangle { x: Math.max(0,parent.width-28); y: Math.max(0,parent.height-8); width: 8; height: 3; radius: 0; color: "#AC4630" }
}
