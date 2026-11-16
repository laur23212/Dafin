import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 17
    color: root.activeState ? "#A83E94" : "#FDE1ED"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#B552A6" : "#D5A8D7"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#FFFFFF"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
