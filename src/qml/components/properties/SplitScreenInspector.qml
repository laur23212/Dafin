import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: the Split Screen tab of a split screen clip (AppControllerSplitScreen.cpp), opened when one
// is selected. What each cell shows (media goes in by dropping it onto a cell in the preview, or onto
// the clip in the timeline for the next empty cell), swap or empty a cell, another layout (each cell
// keeps its media), the entrance animation, the border and its colour, and the length. Advanced
// edit opens the cells as a timeline of their own, one row per cell, as Filmora's Advanced Split
// Screen Edit does. Every change is one undo step.
Item {
    id: root

    property int clipDataRevision: 0
    readonly property int trackIndex: EditorState.selectedTrack
    readonly property int clipIndex: EditorState.selectedClip
    readonly property var info: {
        void clipDataRevision
        return EditorState.splitScreenInfo(root.trackIndex, root.clipIndex)
    }
    readonly property var cells: root.info.cells || []
    readonly property var layouts: EditorState.splitScreenLayouts()
    readonly property real borderPx: root.info.borderPx || 0
    // The width the border comes back at when it is switched on again.
    property real lastBorderPx: 6

    height: contentCol.height
    implicitHeight: contentCol.height

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    onBorderPxChanged: if (root.borderPx > 0) root.lastBorderPx = root.borderPx

    component RowLabel: Text {
        color: Theme.panelForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingLg

        // ----- name and Advanced edit ----------------------------------------------------------
        Item {
            width: parent.width
            height: Math.max(nameLabel.implicitHeight, advancedButton.height)

            ThemedLabel {
                id: nameLabel
                anchors.left: parent.left
                anchors.right: advancedButton.left
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: root.info.label || qsTr("Split screen")
                SearchAnchor {
                    searchId: "split-screen"
                    title: qsTr("Split screen")
                    keywords: "split screen layout cells border animation collage grid"
                }
            }

            ThemedButton {
                id: advancedButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                variant: "secondary"
                glyph: Theme.icons.layers
                text: qsTr("Advanced edit")
                tooltip: qsTr("Opens the cells as a timeline of their own, one row per cell: trim them, move them in time, add effects or titles. Double-clicking the clip does the same.")
                onClicked: EditorState.openCompositeClip(root.trackIndex, root.clipIndex)
            }
        }

        ThemedLabel {
            width: parent.width
            size: "sm"
            opacity: 0.8
            wrapMode: Text.WordWrap
            text: qsTr("Drag a video or picture from Media onto a cell in the preview. Dropped on the clip in the timeline, it goes into the next empty cell.")
        }

        // ----- cells ---------------------------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm

            RowLabel {
                width: parent.width
                text: qsTr("Cells (%1 of %2 filled)").arg(root.info.filledCount || 0).arg(root.cells.length)
            }

            Repeater {
                model: root.cells

                delegate: Rectangle {
                    id: cellRow
                    required property var modelData
                    required property int index
                    width: parent ? parent.width : 0
                    height: 36
                    radius: 4
                    color: Theme.panelSecondaryBg

                    Rectangle {
                        id: badge
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22
                        height: 22
                        radius: 11
                        color: cellRow.modelData.filled ? Theme.c.effectTemplateCard.accent
                                                        : Qt.rgba(Theme.c.effectTemplateCard.accent.r,
                                                                  Theme.c.effectTemplateCard.accent.g,
                                                                  Theme.c.effectTemplateCard.accent.b, 0.25)
                        Text {
                            anchors.centerIn: parent
                            text: String(cellRow.modelData.number)
                            color: Theme.c.effectTemplateCard.foregroundOnMedia
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            font.weight: Font.DemiBold
                        }
                    }

                    Text {
                        anchors.left: badge.right
                        anchors.leftMargin: 8
                        anchors.right: cellButtons.left
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: cellRow.modelData.filled ? cellRow.modelData.name : qsTr("Empty — drop media here")
                        color: cellRow.modelData.filled ? Theme.panelForeground : Theme.c.transformInspector.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.italic: !cellRow.modelData.filled
                        elide: Text.ElideMiddle
                    }

                    Row {
                        id: cellButtons
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        ThemedButton {
                            visible: cellRow.index > 0
                            variant: "ghost"
                            glyph: Theme.icons.chevronUp
                            tooltip: qsTr("Swap with cell %1").arg(cellRow.index)
                            onClicked: EditorState.swapSplitScreenCells(root.trackIndex, root.clipIndex,
                                                                        cellRow.index, cellRow.index - 1)
                        }
                        ThemedButton {
                            visible: cellRow.modelData.filled
                            variant: "ghost"
                            glyph: Theme.icons.x
                            tooltip: qsTr("Empty this cell")
                            onClicked: EditorState.clearSplitScreenCell(root.trackIndex, root.clipIndex, cellRow.index)
                        }
                    }
                }
            }
        }

        // ----- layout ----------------------------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm

            RowLabel {
                width: parent.width
                text: qsTr("Layout")
            }

            Flow {
                width: parent.width
                spacing: 6

                Repeater {
                    model: root.layouts

                    delegate: Rectangle {
                        id: layoutChip
                        required property var modelData
                        readonly property bool current: modelData.id === root.info.layout
                        width: 64
                        height: 36
                        radius: 3
                        color: chipHover.hovered ? Theme.c.effectTemplateCard.backgroundHover
                                                 : Theme.c.effectTemplateCard.background
                        border.width: current ? 2 : 1
                        border.color: current ? Theme.c.effectTemplateCard.accent
                                              : Qt.rgba(Theme.c.effectTemplateCard.accent.r,
                                                        Theme.c.effectTemplateCard.accent.g,
                                                        Theme.c.effectTemplateCard.accent.b, 0.35)

                        SplitScreenThumb {
                            anchors.fill: parent
                            anchors.margins: 3
                            layout: layoutChip.modelData
                            numbered: false
                            playing: chipHover.hovered
                        }

                        HoverHandler {
                            id: chipHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: EditorState.setSplitScreenLayout(root.trackIndex, root.clipIndex,
                                                                       layoutChip.modelData.id)
                        }
                        ThemedToolTip {
                            text: layoutChip.current ? layoutChip.modelData.label
                                                     : qsTr("%1 — each cell keeps its media").arg(layoutChip.modelData.label)
                            visible: chipHover.hovered
                        }
                    }
                }
            }
        }

        // ----- animation -----------------------------------------------------------------------
        Row {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedSwitch {
                anchors.verticalCenter: parent.verticalCenter
                tooltip: qsTr("The cells unfold in one after another at the start, as the card shows on hover")
                Binding on checked { value: !!root.info.animate }
                onToggled: EditorState.setSplitScreenAnimation(root.trackIndex, root.clipIndex, checked)
            }
            RowLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Split screen animation")
            }
        }

        // ----- border ----------------------------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm

            Row {
                width: parent.width
                spacing: Theme.spacingSm

                ThemedSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    tooltip: qsTr("A line of colour between the cells")
                    Binding on checked { value: root.borderPx > 0 }
                    onToggled: EditorState.setSplitScreenBorder(root.trackIndex, root.clipIndex,
                                                                checked ? Math.max(1, root.lastBorderPx) : 0,
                                                                root.info.borderColor || "#ffffff")
                }
                RowLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Border")
                }
            }

            Row {
                visible: root.borderPx > 0
                width: parent.width
                spacing: Theme.spacingSm

                ThemedSlider {
                    id: borderSlider
                    width: parent.width - borderColour.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    label: qsTr("Border width")
                    from: 1
                    to: 40
                    valueFormatter: function (v) { return Math.round(v) + " px" }
                    Binding on value {
                        when: !borderSlider.pressed
                        value: root.borderPx
                    }
                    // One undo step per drag: the width is written when the slider is let go.
                    onPressedChanged: {
                        if (!pressed && Math.round(value) !== Math.round(root.borderPx))
                            EditorState.setSplitScreenBorder(root.trackIndex, root.clipIndex, Math.round(value),
                                                             root.info.borderColor || "#ffffff")
                    }
                }

                ColorSwatchField {
                    id: borderColour
                    anchors.verticalCenter: parent.verticalCenter
                    hex: root.info.borderColor || "#ffffff"
                    tooltip: qsTr("Border colour")
                    onEdited: value => EditorState.setSplitScreenBorder(root.trackIndex, root.clipIndex,
                                                                        root.borderPx, value)
                }
            }
        }

        // ----- length ----------------------------------------------------------------------------
        Row {
            width: parent.width
            spacing: Theme.spacingSm

            RowLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Length")
            }
            ThemedNumberField {
                anchors.verticalCenter: parent.verticalCenter
                width: 96
                from: 0.5
                to: 3600
                step: 0.1
                decimals: 1
                unit: "s"
                value: root.info.durationSeconds || 0
                onEdited: value => EditorState.setSplitScreenDuration(root.trackIndex, root.clipIndex, value)
            }
        }
    }
}
