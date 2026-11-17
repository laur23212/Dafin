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
    readonly property color base: Theme.c.editorHeader.background
    radius: 8
    color: root.base
    border.width: 0
    border.color: root.gold
    clip: true
    
    // Flag order, left to right: blue | yellow | red.
    Row {
        x: 0; y: Math.max(0, root.height - 3); width: root.width; height: 3
        Rectangle { width: parent.width / 3; height: 3; color: root.blue }
        Rectangle { width: parent.width / 3; height: 3; color: root.yellow }
        Rectangle { width: parent.width / 3; height: 3; color: root.ornament }
    }

    
    // "Dinti de lup" (wolf teeth): the sawtooth border of Romanian weaving and carved gates.
    Item {
        x: 6; y: Math.max(0, root.height - 12); width: Math.max(0, root.width - 12); height: 7; clip: true
        Repeater {
            model: Math.max(0, Math.ceil(parent.width / 14))
            delegate: Rectangle {
                x: index * 14 + 2; y: 3; width: 7; height: 7; rotation: 45
                color: index % 2 === 0 ? root.yellow : root.ornament
                opacity: 0.85
            }
        }
    }

    
}
