import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool pressed: !!(control && control.down === true)
    readonly property bool hovered: !!(control && control.hovered === true)
    readonly property bool focused: !!(control && control.visualFocus === true)
    readonly property bool primary: !!(control && control.variant === "primary")
    readonly property bool ghost: !!(control && control.variant === "ghost" && !hovered && !pressed && !focused)
    readonly property color red: Theme.darkMode ? "#B53240" : "#9F2937"
    readonly property color wood: Theme.darkMode ? "#40534F" : "#E1CFB1"
    readonly property color gold: Theme.darkMode ? "#DDBB83" : "#8C643F"
    color: root.ghost ? "transparent" : (root.primary ? red : (pressed ? (Theme.darkMode ? "#263E40" : "#D1B895") : hovered ? (Theme.darkMode ? "#51665D" : "#EADABE") : wood))
    radius: 7
    border.width: root.ghost ? 0 : (root.focused ? 2 : 1)
    border.color: root.focused ? gold : (root.primary ? gold : (Theme.darkMode ? "#977B5D" : "#B09570"))
    opacity: control && control.enabled === false ? .52 : 1
    clip: true
    // The double edge evokes carved gates and polished wooden trim.
    Rectangle { x: 2; y: 2; width: Math.max(0, parent.width-4); height: 1; color: root.primary ? "#F6D9AB" : root.gold; opacity: root.pressed ? .3 : .72; visible: !root.ghost }
    Rectangle { x: 2; y: Math.max(1,parent.height-3); width: Math.max(0,parent.width-4); height: 1; color: "#151B1B"; opacity: .48; visible: !root.ghost }
    Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 4; color: "transparent"; border.width: root.primary ? 1 : 0; border.color: "#80FBE8C7"; visible: !root.ghost }
    // Small embroidered diamond on the leading edge of the primary action.
    Rectangle { x: 7; y: parent.height/2 - 3; width: 6; height: 6; rotation: 45; color: "transparent"; border.width: 1; border.color: "#EAD2A1"; visible: root.primary && parent.width > 70 }
}
