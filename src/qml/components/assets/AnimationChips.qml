import QtQuick
import QtQuick.Controls.Basic
import Drift
// Dafin: In / Out clip animations as draggable chips, so an animation can be dropped on a clip
// (or tapped to apply it to the selected clip) instead of going through the Animation menu.
// Payload is "animIn:<kind>" / "animOut:<kind>", the slots and kinds AnimationInspector edits.
Column {
    id: root
    spacing: Theme.spacingSm

    property string which: "animIn"
    readonly property var kinds: [
        { kind: "fade", label: qsTr("Fade") },
        { kind: "slideUp", label: qsTr("Slide up") },
        { kind: "slideDown", label: qsTr("Slide down") },
        { kind: "slideLeft", label: qsTr("Slide left") },
        { kind: "slideRight", label: qsTr("Slide right") },
        { kind: "zoomIn", label: qsTr("Zoom in") },
        { kind: "zoomOut", label: qsTr("Zoom out") },
        { kind: "pop", label: qsTr("Pop") },
        { kind: "spinCW", label: qsTr("Spin CW") },
        { kind: "spinCCW", label: qsTr("Spin CCW") },
        { kind: "bounce", label: qsTr("Bounce") }
    ]

    function applyToSelected(kind) {
        if (EditorState.selectedTrack < 0 || EditorState.selectedClip < 0)
            return
        EditorState.setClipAnimation(EditorState.selectedTrack, EditorState.selectedClip,
                                     root.which, { kind: kind })
    }

    Row {
        spacing: Theme.spacingSm
        Repeater {
            model: [
                { which: "animIn", label: qsTr("Animation in") },
                { which: "animOut", label: qsTr("Animation out") }
            ]
            delegate: Rectangle {
                required property var modelData
                readonly property bool on: root.which === modelData.which
                width: slotLabel.implicitWidth + Theme.spacingXl
                height: Theme.controlHeight
                radius: Theme.c.transitionPresetCard.radius
                color: on || slotHover.hovered ? Theme.c.transitionPresetCard.backgroundHover
                                               : Theme.c.transitionPresetCard.background
                border.width: on ? Theme.c.transitionPresetCard.borderWidth : 0
                border.color: Theme.c.transitionPresetCard.border
                Text {
                    id: slotLabel
                    anchors.centerIn: parent
                    text: parent.modelData.label
                    color: Theme.c.animationInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                HoverHandler { id: slotHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.which = parent.modelData.which }
            }
        }
    }

    Flow {
        width: parent.width
        spacing: Theme.spacingSm
        Repeater {
            model: root.kinds
            delegate: Rectangle {
                id: chip
                required property var modelData
                width: chipLabel.implicitWidth + Theme.spacingXl
                height: Theme.controlHeight
                radius: Theme.c.transitionPresetCard.radius
                color: chipDrag.hovered ? Theme.c.transitionPresetCard.backgroundHover
                                        : Theme.c.transitionPresetCard.background
                opacity: chipDrag.active ? Theme.c.transitionPresetCard.dragOpacity : 1
                Text {
                    id: chipLabel
                    anchors.centerIn: parent
                    text: chip.modelData.label
                    color: Theme.c.animationInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                AssetDragSource {
                    id: chipDrag
                    anchors.fill: parent
                    kind: "animation"
                    payload: root.which + ":" + chip.modelData.kind
                    label: chip.modelData.label
                    onTapped: root.applyToSelected(chip.modelData.kind)
                }
                ThemedToolTip {
                    text: qsTr("%1 — drag onto a clip, or click to apply to the selected clip").arg(chip.modelData.label)
                    visible: chipDrag.hovered
                }
            }
        }
    }
}
