import QtQuick
import Drift

// Playhead line and scrubber head.
Item {
    id: playhead

    property var panel
    property Flickable view
    property var interpolator
    readonly property bool headDragging: playheadDragArea.drag.active
    // The scissors chip sits on the bar: click it to cut at the bar, drag it up or down along the
    // bar to put it where you like (kept while the timeline is open).
    readonly property real scissorsWidth: 28
    readonly property real scissorsHeight: 42
    readonly property real scissorsMinY: Theme.timelineRulerHeight
    readonly property real scissorsMaxY: Math.max(scissorsMinY, height - scissorsHeight)
    property real scissorsY: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight + 8
    y: 0
    // Above the seek strip (z: 2), otherwise the scrub area
    // covers the handle and swallows the press before drag starts.
    z: 3
    width: Theme.playheadLineWidth
    height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
            + panel.totalTracksHeightCached

    Binding {
        target: playhead
        property: "x"
        value: interpolator.seconds * panel.pxPerSecond
        when: !playheadDragArea.drag.active && !playheadLineDrag.drag.active
    }

    // Follow either drag live so the preview scrubs with it.
    onXChanged: {
        if (playheadDragArea.drag.active || playheadLineDrag.drag.active)
            EditorState.playheadSeconds = playhead.x / panel.pxPerSecond
    }

    function finishSeek() {
        EditorState.playheadSeconds =
            EditorState.snapTime(playhead.x / panel.pxPerSecond)
    }

    TimelinePlayheadLine { }

    TimelinePlayheadHead { }

    // Wide grab across the seek strip head.
    MouseArea {
        id: playheadDragArea
        width: Theme.playheadSeekGrabWidth
        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
        x: -(width - Theme.playheadLineWidth) / 2
        y: 0
        cursorShape: Qt.SizeHorCursor
        preventStealing: true
        hoverEnabled: true
        drag.target: playhead
        drag.axis: Drag.XAxis
        drag.threshold: 0
        drag.minimumX: 0
        drag.maximumX: view.contentWidth - Theme.playheadLineWidth
        onPressed: panel.beginPlayheadSeek()
        onReleased: {
            playhead.finishSeek()
            panel.endPlayheadSeek()
        }
        onCanceled: {
            playhead.finishSeek()
            panel.endPlayheadSeek()
        }
    }

    // Narrow grab down the timeline line — stays thin so clip
    // clicks aren't stolen.
    MouseArea {
        id: playheadLineDrag
        width: 7
        height: parent.height - playheadDragArea.height
        x: -(width - Theme.playheadLineWidth) / 2
        y: playheadDragArea.height
        cursorShape: Qt.SizeHorCursor
        preventStealing: true
        drag.target: playhead
        drag.axis: Drag.XAxis
        drag.threshold: 0
        drag.minimumX: 0
        drag.maximumX: view.contentWidth - Theme.playheadLineWidth
        onPressed: panel.beginPlayheadSeek()
        onReleased: {
            playhead.finishSeek()
            panel.endPlayheadSeek()
        }
        onCanceled: {
            playhead.finishSeek()
            panel.endPlayheadSeek()
        }
    }

    // Scissors chip: click cuts at the bar (nothing selected cuts every clip under it, a selection
    // cuts only that), dragging moves it up and down the bar. Declared last so it is on top.
    Rectangle {
        id: scissorsChip
        width: playhead.scissorsWidth
        height: playhead.scissorsHeight
        radius: width / 2
        x: -(width - Theme.playheadLineWidth) / 2
        y: Math.max(playhead.scissorsMinY, Math.min(playhead.scissorsMaxY, playhead.scissorsY))
        color: Theme.c.timelinePlayheadHead.background
        border.width: scissorsDrag.containsMouse || scissorsDrag.pressed ? 1 : 0
        border.color: "white"

        IconGlyph {
            anchors.centerIn: parent
            glyph: Theme.icons.scissors
            iconSize: 20
            iconColor: "white"
        }

        MouseArea {
            id: scissorsDrag
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: pressed ? Qt.SizeVerCursor : Qt.PointingHandCursor
            property real pressSceneY: 0
            property real pressChipY: 0
            property bool moved: false
            onPressed: (mouse) => {
                pressSceneY = mapToItem(playhead, mouse.x, mouse.y).y
                pressChipY = scissorsChip.y
                moved = false
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                const dy = mapToItem(playhead, mouse.x, mouse.y).y - pressSceneY
                if (!moved && Math.abs(dy) < 3)
                    return
                moved = true
                playhead.scissorsY = Math.max(playhead.scissorsMinY,
                                              Math.min(playhead.scissorsMaxY, pressChipY + dy))
            }
            onClicked: {
                if (!moved)
                    EditorState.splitSelectionOrAllAtPlayhead()
            }
        }

        ThemedToolTip {
            text: qsTr("Cut at the bar — the selected clip, or every clip under it when nothing is selected. Drag up or down to move this button along the bar.")
            visible: scissorsDrag.containsMouse && !scissorsDrag.pressed
        }
    }
}
