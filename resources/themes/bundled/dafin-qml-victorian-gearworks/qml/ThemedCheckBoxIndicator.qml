import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 3
    color: root.activeState ? "#D4AF66" : "#191711"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#D8B56C" : "#A78350"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#21180E"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
