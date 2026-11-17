import QtQuick
import Drift

// Romanian Core 2: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool focused: !!(control && control.visualFocus === true)
    color: Theme.darkMode ? "#F7E393" : "#FFF0B4"
    radius: 4
    border.width: focused ? 2 : 1
    border.color: focused ? "#B5293C" : "#85732A"
    rotation: 45
    Rectangle { anchors.centerIn: parent; width: parent.width*.36; height: width; radius: 0; color: "#AC2739"; rotation: 0 }
}
