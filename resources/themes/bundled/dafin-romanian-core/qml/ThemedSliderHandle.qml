import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool focused: !!(control && control.visualFocus === true)
    color: Theme.darkMode ? "#E9D1A1" : "#F9E5BA"
    radius: 4
    border.width: focused ? 2 : 1
    border.color: focused ? "#B12D38" : "#765839"
    rotation: 45
    Rectangle { anchors.centerIn: parent; width: parent.width*.36; height: width; radius: 0; color: "#A1323C"; rotation: 0 }
}
