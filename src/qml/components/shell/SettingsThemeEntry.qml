import QtQuick
import Drift

// One theme in the Appearance grid: a small drawing of the editor in the theme's own
// colours (header, media panel, viewer, timeline with a video and an audio clip), the
// name under it, and a QML badge on skins that also reshape controls. The drawing is
// built asynchronously, so opening Appearance with 80+ themes never blocks the window.
Item {
    id: root

    property string themeId: ""
    property string themeName: ""
    property string detailText: ""
    property bool qmlSkin: false
    property bool trusted: false
    // System and dual-variant themes look different on the dark and light side.
    property bool followsVariant: false

    signal activate()

    implicitWidth: 128
    implicitHeight: preview.height + nameLabel.implicitHeight + Theme.spacingXs * 2
    height: implicitHeight

    readonly property bool selected: ThemeManager.selection === themeId

    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.themeName
    Keys.onReturnPressed: root.activate()
    Keys.onSpacePressed: root.activate()

    Rectangle {
        id: preview
        width: root.width
        height: Math.round(root.width * 0.62)
        radius: Theme.c.settingsThemeEntry.radius
        color: Theme.c.settingsThemeEntry.background
        border.width: root.selected || root.activeFocus ? 2 : Theme.c.settingsThemeEntry.borderWidth
        border.color: root.selected || root.activeFocus ? Theme.c.settingsThemeEntry.borderSelected
                      : area.containsMouse ? Theme.c.settingsThemeEntry.foregroundMuted
                      : Theme.c.settingsThemeEntry.border
        clip: true

        Loader {
            anchors.fill: parent
            anchors.margins: 2
            asynchronous: true
            sourceComponent: mock
        }

        Rectangle {
            visible: root.qmlSkin
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 4
            width: badgeText.implicitWidth + 8
            height: badgeText.implicitHeight + 2
            radius: height / 2
            color: Theme.c.settingsThemeEntry.badgeBackground
            border.width: 1
            border.color: Theme.c.settingsThemeEntry.border

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: root.trusted ? "QML" : qsTr("QML · asks")
                color: Theme.c.settingsThemeEntry.badgeForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTiny
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            visible: root.selected
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 4
            width: 16
            height: 16
            radius: 8
            color: Theme.c.settingsThemeEntry.borderSelected

            Text {
                anchors.centerIn: parent
                text: "✓"
                color: Theme.primaryForeground
                font.pixelSize: 11
                font.weight: Font.Bold
            }
        }
    }

    Text {
        id: nameLabel
        anchors.top: preview.bottom
        anchors.topMargin: Theme.spacingXs
        width: root.width
        horizontalAlignment: Text.AlignHCenter
        text: root.themeName
        elide: Text.ElideRight
        color: Theme.c.settingsThemeEntry.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: root.selected ? Font.DemiBold : Font.Normal
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activate()
    }

    ThemedToolTip {
        visible: area.containsMouse
        text: root.detailText.length > 0 ? root.themeName + "\n" + root.detailText : root.themeName
    }

    Component {
        id: mock

        Item {
            id: mockRoot

            readonly property var c: {
                if (root.followsVariant) {
                    const _side = ThemeManager.effectiveVariant
                }
                return ThemeManager.previewColors(root.themeId) || ({})
            }
            function pick(key, fallback) {
                const value = mockRoot.c[key]
                return value !== undefined && value !== null && String(value).length > 0 ? value : fallback
            }
            readonly property color surface: pick("surface", "#202020")
            readonly property color panel: pick("panel", "#2a2a2a")
            readonly property color foreground: pick("foreground", "#e0e0e0")
            readonly property color primary: pick("primary", "#14b8a6")
            readonly property color border: pick("border", "#3a3a3a")
            readonly property color timeline: pick("timeline", panel)
            readonly property real r: Math.min(4, Number(pick("radius", 3)))

            Rectangle {
                anchors.fill: parent
                radius: Math.max(0, preview.radius - 2)
                color: mockRoot.surface
            }

            // Header with the export button.
            Rectangle {
                x: 0; y: 0
                width: parent.width; height: parent.height * 0.12
                color: mockRoot.panel
                Rectangle {
                    x: parent.width * 0.05; y: parent.height * 0.35
                    width: parent.width * 0.22; height: parent.height * 0.3
                    radius: height / 2
                    color: mockRoot.foreground
                    opacity: 0.45
                }
                Rectangle {
                    x: parent.width * 0.78; y: parent.height * 0.2
                    width: parent.width * 0.17; height: parent.height * 0.6
                    radius: mockRoot.r
                    color: mockRoot.primary
                }
            }

            // Media panel.
            Rectangle {
                x: parent.width * 0.03; y: parent.height * 0.16
                width: parent.width * 0.32; height: parent.height * 0.46
                radius: mockRoot.r
                color: mockRoot.panel
                border.width: 1
                border.color: mockRoot.border
                Grid {
                    x: parent.width * 0.1; y: parent.height * 0.14
                    columns: 2
                    spacing: parent.width * 0.08
                    Repeater {
                        model: 4
                        Rectangle {
                            required property int index
                            width: mockRoot.width * 0.1; height: mockRoot.height * 0.12
                            radius: mockRoot.r / 2
                            color: index === 0 ? mockRoot.primary : mockRoot.foreground
                            opacity: index === 0 ? 0.9 : 0.25
                        }
                    }
                }
            }

            // Viewer.
            Rectangle {
                x: parent.width * 0.38; y: parent.height * 0.16
                width: parent.width * 0.59; height: parent.height * 0.46
                radius: mockRoot.r
                color: mockRoot.panel
                border.width: 1
                border.color: mockRoot.border
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: parent.height * 0.12
                    color: "#0c0c0c"
                }
            }

            // Timeline with a video and an audio clip and the playhead.
            Rectangle {
                x: parent.width * 0.03; y: parent.height * 0.66
                width: parent.width * 0.94; height: parent.height * 0.3
                radius: mockRoot.r
                color: mockRoot.timeline
                border.width: 1
                border.color: mockRoot.border
                Rectangle {
                    x: parent.width * 0.06; y: parent.height * 0.18
                    width: parent.width * 0.48; height: parent.height * 0.28
                    radius: mockRoot.r / 2
                    color: mockRoot.pick("clipVideo", "#7c5cc4")
                }
                Rectangle {
                    x: parent.width * 0.2; y: parent.height * 0.56
                    width: parent.width * 0.58; height: parent.height * 0.28
                    radius: mockRoot.r / 2
                    color: mockRoot.pick("clipAudio", "#2aa198")
                }
                Rectangle {
                    x: parent.width * 0.46; y: 0
                    width: 1.5; height: parent.height
                    color: mockRoot.primary
                }
            }
        }
    }
}
