import QtQuick
import Drift

// Romanian Core (tricolor): original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool selected: !!(control && control.active === true)
    readonly property bool pressed: !!(control && control.down === true)
    readonly property bool hover: !!(control && control.hovered === true)
    readonly property bool focused: !!(control && control.visualFocus === true)
    radius: 6
    color: selected ? (Theme.darkMode ? "#8E202F" : "#C92E42") : (pressed ? (Theme.darkMode ? "#151F39" : "#E4D496") : (hover ? (Theme.darkMode ? "#3A4560" : "#F0E4B4") : "transparent"))
    border.width: focused ? 2 : (selected || hover ? 1 : 0)
    border.color: focused ? "#FADC65" : (Theme.darkMode ? "#BCA64D" : "#B5A256")
    Rectangle { x: 3; y: 3; width: Math.max(0,parent.width-6); height: 1; color: Theme.darkMode ? "#E5D597" : "#FFFFFF"; opacity: .54; visible: selected || hover }
    opacity: control && control.enabled === false ? .5 : 1
}
