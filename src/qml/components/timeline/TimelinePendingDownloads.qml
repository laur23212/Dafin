import QtQuick
import Drift

// Dafin: one placeholder per online card dropped before its download finished
// (TimelinePanel.pendingOnlineDrops), where the clip will land, with the download's progress.
// The clip replaces it once the file is in Media.
Item {
    id: root

    property var panel

    anchors.fill: parent
    z: 245

    Repeater {
        model: root.panel.pendingOnlineDrops

        delegate: Rectangle {
            id: placeholder

            required property var modelData

            readonly property var target: modelData.target || ({ "newTrack": true, "insertIndex": 0 })
            readonly property bool appending: target.newTrack && target.insertIndex >= root.panel.tracks.length
            readonly property real laneHeight: !target.newTrack ? root.panel.trackHeight(target.track)
                                             : modelData.kind === "audio" ? Theme.trackHeightAudio
                                             : modelData.kind === "video" ? Theme.trackHeightVideo
                                             : Theme.trackHeightText
            readonly property real progress: OnlineAssets.downloadsRevision >= 0
                                             ? OnlineAssets.downloadProgress(modelData.key) : 0
            readonly property bool probing: (modelData.assetId || "").length > 0

            x: modelData.at * root.panel.pxPerSecond
            y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
               + (!target.newTrack ? root.panel.trackOffsetY(target.track)
                  : appending ? root.panel.newTrackBoundaryY(target.insertIndex) + Theme.trackGap / 2
                  : root.panel.newTrackBoundaryY(target.insertIndex) - laneHeight / 2)
            width: Math.max(24, modelData.duration * root.panel.pxPerSecond)
            height: laneHeight
            radius: Theme.c.timelineLandingOutline.radius
            clip: true
            color: Qt.rgba(Theme.c.timelineLandingOutline.background.r, Theme.c.timelineLandingOutline.background.g,
                           Theme.c.timelineLandingOutline.background.b, 0.22)
            border.width: Theme.c.timelineLandingOutline.borderWidth
            border.color: Theme.c.timelineLandingOutline.border

            // The download's progress fills the block from the left.
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, placeholder.probing ? 1 : placeholder.progress))
                height: parent.height
                radius: parent.radius
                color: Qt.rgba(Theme.c.timelineLandingOutline.background.r, Theme.c.timelineLandingOutline.background.g,
                               Theme.c.timelineLandingOutline.background.b, 0.30)
                Behavior on width { NumberAnimation { duration: Theme.durationFast } }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: placeholder.probing ? qsTr("Adding “%1”…").arg(placeholder.modelData.title)
                                          : qsTr("Downloading %1% · %2").arg(Math.round(placeholder.progress * 100))
                                                                       .arg(placeholder.modelData.title)
                color: Theme.c.timelineLandingOutline.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTiny
                font.weight: Font.Medium
            }
        }
    }
}
