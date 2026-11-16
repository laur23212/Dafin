import QtQuick
import Drift

// Dafin: an accent colour laid over whichever theme is on screen
// (ThemeManager.accentColor, persisted). "Theme" puts the theme's own colour back.
Flow {
    id: root

    width: parent ? parent.width : 0
    spacing: Theme.spacingSm

    readonly property var presets: [
        "#14b8a6", "#06b6d4", "#3b82f6", "#6366f1", "#8b5cf6", "#d946ef",
        "#ec4899", "#ef4444", "#f97316", "#f59e0b", "#eab308", "#84cc16", "#22c55e"
    ]
    readonly property string current: ThemeManager.accentColor.toLowerCase()
    readonly property bool customActive: current.length > 0 && presets.indexOf(current) < 0
    // The theme's own primary colour (previews ignore the accent override).
    readonly property color themeColor: {
        const _rev = ThemeManager.currentTheme + ThemeManager.effectiveVariant
        const colors = ThemeManager.previewColors(ThemeManager.selection) || ({})
        return colors.primary || Theme.c.settingsThemeEntry.borderSelected
    }

    component Swatch: Item {
        id: swatch

        property color fill: "transparent"
        property bool chosen: false
        property string tip: ""
        property string glyph: ""
        signal picked()

        width: 30
        height: 30
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: swatch.tip
        Keys.onReturnPressed: swatch.picked()
        Keys.onSpacePressed: swatch.picked()

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: swatch.chosen || swatch.activeFocus ? 2 : 0
            border.color: Theme.c.settingsThemeEntry.foreground
        }
        Rectangle {
            anchors.centerIn: parent
            width: 22
            height: 22
            radius: 11
            color: swatch.fill
            border.width: 1
            border.color: Theme.c.settingsThemeEntry.border

            Text {
                anchors.centerIn: parent
                visible: swatch.glyph.length > 0
                text: swatch.glyph
                color: Theme.c.settingsThemeEntry.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.DemiBold
            }
        }
        MouseArea {
            id: swatchArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: swatch.picked()
        }
        ThemedToolTip {
            visible: swatchArea.containsMouse
            text: swatch.tip
        }
    }

    Swatch {
        fill: root.themeColor
        chosen: root.current.length === 0
        tip: qsTr("Theme color")
        glyph: "T"
        onPicked: ThemeManager.accentColor = ""
    }

    Repeater {
        model: root.presets
        delegate: Swatch {
            required property string modelData
            fill: modelData
            chosen: root.current === modelData
            tip: modelData
            onPicked: ThemeManager.accentColor = modelData
        }
    }

    Swatch {
        fill: root.customActive ? root.current : "transparent"
        chosen: root.customActive
        tip: qsTr("Custom color…")
        glyph: root.customActive ? "" : "+"
        onPicked: {
            const dialog = dialogLoader.ensure()
            dialog.selectedColor = root.current.length > 0 ? root.current : root.themeColor
            dialog.open()
        }
    }

    LazyLoader {
        id: dialogLoader
        sourceComponent: Component {
            ThemedColorDialog {
                title: qsTr("Accent color")
                onAccepted: ThemeManager.accentColor = selectedColor.toString()
            }
        }
    }
}
