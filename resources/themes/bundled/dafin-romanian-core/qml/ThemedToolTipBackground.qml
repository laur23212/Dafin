import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property color ornament: Theme.darkMode ? "#D66B68" : "#A7333A"
    readonly property color gold: Theme.darkMode ? "#D7AF70" : "#957045"
    readonly property color base: Theme.c.panelFrame.background
    radius: 8
    color: root.base
    border.width: 1
    border.color: root.gold
    clip: true
    
    
    
}
