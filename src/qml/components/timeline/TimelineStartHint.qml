import QtQuick
import Drift

// Dafin: Filmora's "drag and drop media here" line on the first track of a timeline with no clips
// yet, so an empty project says where things go. No mouse handling: drops pass straight through.
Row {
    id: hint

    property var panel

    visible: panel.tracks.length > 0 && !panel.dropCreatesNewTrack && panel.dropTrackIndex < 0
             && (panel.tracks, !panel.timelineHasClips())
    x: Math.max(0, panel.timelineViewX) + 12
    y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
       + (panel.trackHeight(0) - height) / 2
    z: 1
    spacing: 6
    opacity: 0.7

    IconGlyph {
        anchors.verticalCenter: parent.verticalCenter
        glyph: Theme.icons.film
        iconSize: 14
        iconColor: Theme.mutedForeground
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Drag and drop media, titles and stickers here to start your video")
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeTiny
    }
}
