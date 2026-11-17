import QtQuick
import Drift

// Romanian Core (tricolor): original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property color ornament: Theme.darkMode ? "#E0243A" : "#CE1126"
    readonly property color gold: Theme.darkMode ? "#E3BE1E" : "#9C7F00"
    readonly property color yellow: "#FCD116"
    readonly property color blue: Theme.darkMode ? "#2455C9" : "#002B7F"
    readonly property color base: Theme.c.timelinePanel.trackBackground
    radius: 8
    color: root.base
    border.width: 1
    border.color: root.gold
    clip: true
    
    
    
}
