import QtQuick
import Drift

// Dafin: the "Audio 1" lane a new project shows under its video track, as in Filmora. It is not a
// track yet — an empty project stays one track on disk — but sound dropped anywhere over the
// timeline lands here and becomes Audio 1 (TimelinePanel.dropTargetAtYForKind). Gone once the
// project has a track that takes sound. The header column draws its label (TrackHeaderColumn).
Rectangle {
    id: lane

    property var panel

    visible: !panel.hasAudioTrack && panel.tracks.length > 0 && !panel.dropCreatesNewTrack
    x: 0
    y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
       + panel.totalTracksHeightCached + Theme.trackGap
    width: parent.width
    height: Theme.trackHeightAudio
    z: 1
    radius: Theme.radiusSm
    color: "transparent"
    border.width: 1
    border.color: Qt.rgba(Theme.panelBorder.r, Theme.panelBorder.g, Theme.panelBorder.b, 0.9)

    // Held at the left of the visible part of the timeline, wherever it is scrolled.
    Row {
        x: Math.max(0, panel.timelineViewX) + 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        opacity: 0.7

        IconGlyph {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.music
            iconSize: 14
            iconColor: Theme.mutedForeground
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Drag music and sound effects here")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeTiny
        }
    }
}
