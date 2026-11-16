import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool active: !!(control && (control.selected === true || control.checked === true || control.active === true))
    color: active ? (Theme.darkMode ? "#7D343F" : "#B04447") : "transparent"
    radius: 5
    border.width: active ? 1 : 0
    border.color: Theme.darkMode ? "#D3B781" : "#8C6044"
    Rectangle { anchors.bottom:parent.bottom; height:2; width:parent.width; color:Theme.darkMode ? "#E4B677" : "#A22E37"; visible:active }
}
