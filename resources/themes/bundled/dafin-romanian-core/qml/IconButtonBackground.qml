import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool selected: !!(control && control.active === true)
    readonly property bool pressed: !!(control && control.down === true)
    readonly property bool hover: !!(control && control.hovered === true)
    readonly property bool focused: !!(control && control.visualFocus === true)
    radius: 6
    color: selected ? (Theme.darkMode ? "#7E3037" : "#B3444D") : (pressed ? (Theme.darkMode ? "#1B3033" : "#D8C1A2") : (hover ? (Theme.darkMode ? "#435756" : "#E7D8BD") : "transparent"))
    border.width: focused ? 2 : (selected || hover ? 1 : 0)
    border.color: focused ? "#E6BB79" : (Theme.darkMode ? "#A98A60" : "#A48A67")
    Rectangle { x: 3; y: 3; width: Math.max(0,parent.width-6); height: 1; color: Theme.darkMode ? "#D9C4A3" : "#FFFFFF"; opacity: .54; visible: selected || hover }
    opacity: control && control.enabled === false ? .5 : 1
}
