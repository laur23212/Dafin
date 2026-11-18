import QtQuick
import Drift

// Full-height ghost lane where a drop would insert a new track, with the clip that would land in it.
Item {
    id: newTrackIndicator

    property var panel
    // Not also gated on the drag being active: dropCreatesNewTrack
    // is only ever set during one, and is cleared on exit, drop
    // and abandon — an extra condition here is one more way for
    // the ghost to silently not appear.
    visible: panel.dropCreatesNewTrack
    readonly property real laneHeight: {
        // A file-manager drag has no bin row to ask about, so its lane is
        // sized from the kind guessed off the extension instead.
        const t = panel.pendingDropKind.length > 0
                ? EditorState.trackTypeForKind(panel.pendingDropKind)
                : EditorState.trackTypeForAsset(EditorState.draggingAssetIndex)
        if (t === "video") return Theme.trackHeightVideo
        if (t === "audio") return Theme.trackHeightAudio
        if (t === "shape") return Theme.trackHeightShape
        if (t === "subtitle") return Theme.trackHeightSubtitle
        return Theme.trackHeightText
    }
    // Dafin: a track added under the last one is drawn in the empty lane it will take (where the
    // "Audio 1" lane waits), not straddling the last row's bottom edge.
    readonly property bool appending: panel.dropNewTrackIndex >= panel.tracks.length
    x: 0
    y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
       + panel.newTrackBoundaryY(panel.dropNewTrackIndex)
       + (appending ? Theme.trackGap / 2 : -laneHeight / 2)
    width: parent.width
    height: laneHeight
    z: 240

    TimelineNewTrackGhostLane { }

    // The boundary itself, so it is unambiguous which gap the
    // lane is going into.
    TimelineNewTrackGhostBoundary { visible: !newTrackIndicator.appending }

    TimelineNewTrackGhostClip {
        panel: newTrackIndicator.panel
    }
}
