import QtQuick
import Drift

// Romanian Core 2: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property color ornament: Theme.darkMode ? "#E0243A" : "#CE1126"
    readonly property color gold: Theme.darkMode ? "#E3BE1E" : "#9C7F00"
    readonly property color yellow: "#FCD116"
    readonly property color blue: Theme.darkMode ? "#2455C9" : "#002B7F"
    readonly property real inset: Math.ceil(root.radius * 0.3) + 1
    readonly property color base: Theme.c.editorHeader.background
    radius: 8
    color: root.base
    border.width: 0
    border.color: root.gold
    clip: true
    
    // The Romanian flag across the background (blue | yellow | red), kept faint on purpose:
    // the panel is the figure and the flag is the ground, so text keeps its contrast.
    Row {
        x: root.inset; y: root.inset
        width: Math.max(0, root.width - 2 * root.inset); height: Math.max(0, root.height - 2 * root.inset)
        opacity: Theme.darkMode ? 0.17 : 0.12
        Rectangle { width: parent.width / 3; height: parent.height; color: root.blue }
        Rectangle { width: parent.width / 3; height: parent.height; color: root.yellow }
        Rectangle { width: parent.width / 3; height: parent.height; color: root.ornament }
    }

    // A string of small flags along the foot, one simple shape repeated at an even rhythm.
    Item {
        x: 8; y: Math.max(0, root.height - 16); width: Math.max(0, root.width - 16); height: 9; clip: true
        Rectangle { x: 0; y: 0; width: parent.width; height: 1; color: root.gold; opacity: 0.55 }
        Repeater {
            model: Math.max(0, Math.floor(parent.width / 18))
            delegate: Row {
                x: index * 18 + 3; y: 1; height: 8
                Rectangle { width: 4; height: 8; color: root.blue }
                Rectangle { width: 4; height: 8; color: root.yellow }
                Rectangle { width: 4; height: 8; color: root.ornament }
            }
        }
    }

    
}
