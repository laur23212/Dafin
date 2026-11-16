import QtQuick
import Drift

// Dafin: one kind of theme as a grid of preview tiles. kind "color" lists System,
// the built-ins and palette themes; kind "qml" lists skins whose qml/ files also
// reshape buttons, panels and sliders. Rows come from ThemeManager.themes, which is
// read once and changes only when a theme is imported, removed or trusted, so
// choosing a theme does not rebuild the grid. A filter hides tiles without
// recreating them.
Column {
    id: root

    property string kind: "color"
    property string filter: ""

    width: parent ? parent.width : 0
    spacing: Theme.spacingSm

    signal themeChosen(string themeId)

    function variantText(row) {
        if (row.system)
            return qsTr("Follows light and dark")
        if (row.hasVariants)
            return qsTr("Dark and light")
        return row.variant === "light" ? qsTr("Light") : qsTr("Dark")
    }

    function matches(row, needle) {
        if (needle.length === 0)
            return true
        const label = String(row.label || row.name || row.id).toLowerCase()
        const group = String(row.group || "").toLowerCase()
        return label.indexOf(needle) >= 0 || group.indexOf(needle) >= 0
    }

    readonly property var rows: {
        const installed = ThemeManager.themes || []
        const out = []
        if (root.kind === "color") {
            out.push({ id: "system", label: qsTr("System"), group: "Dafin", variant: "",
                       hasVariants: false, system: true, kind: "color", trusted: false })
        }
        for (let i = 0; i < installed.length; ++i) {
            const row = installed[i]
            if ((row.kind || "color") === root.kind)
                out.push(row)
        }
        return out
    }

    readonly property string needle: root.filter.trim().toLowerCase()
    readonly property int shownCount: {
        let n = 0
        for (let i = 0; i < rows.length; ++i) {
            if (root.matches(rows[i], root.needle))
                ++n
        }
        return n
    }

    readonly property real minTileWidth: 120
    readonly property int columns: Math.max(1, Math.floor((width + Theme.spacingMd) / (minTileWidth + Theme.spacingMd)))
    readonly property real tileWidth: Math.floor((width - Theme.spacingMd * (columns - 1)) / columns)

    Grid {
        width: root.width
        columns: root.columns
        columnSpacing: Theme.spacingMd
        rowSpacing: Theme.spacingMd

        Repeater {
            model: root.rows
            delegate: SettingsThemeEntry {
                required property var modelData
                visible: root.matches(modelData, root.needle)
                width: root.tileWidth
                themeId: modelData.id
                themeName: modelData.label || modelData.name || modelData.id
                detailText: (modelData.group ? modelData.group + " · " : "") + root.variantText(modelData)
                            + (modelData.kind === "qml"
                               ? "\n" + (modelData.trusted ? qsTr("Also reshapes buttons, panels and sliders.")
                                                           : qsTr("Asks before its QML runs."))
                               : "")
                qmlSkin: modelData.kind === "qml"
                trusted: !!modelData.trusted
                followsVariant: !!modelData.system || !!modelData.hasVariants
                onActivate: root.themeChosen(modelData.id)
            }
        }
    }

    Text {
        visible: root.shownCount === 0
        width: parent.width
        wrapMode: Text.WordWrap
        text: root.rows.length === 0 ? qsTr("None installed yet.")
                                     : qsTr("No themes match “%1”.").arg(root.filter.trim())
        color: Theme.c.settingsThemeList.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
