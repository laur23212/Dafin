import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift
// One download in the downloads window: file, progress, retry and cancel.
Rectangle {
    id: root
    // The list hands over a job id, not the job. The list model is only rebuilt when the set of
    // jobs changes, so this row (and its progress bar) lives for the whole download instead of
    // being torn down and recreated at every progress tick, which restarted the bar from empty.
    // A job map is still accepted, for a caller that has one.
    required property var modelData

    readonly property var liveJob: {
        if (typeof modelData !== "string")
            return modelData
        const list = Downloads.jobs
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === modelData)
                return list[i]
        }
        return null
    }
    // Last job seen, so a row that is about to be removed keeps drawing instead of going blank.
    property var lastJob: ({})
    onLiveJobChanged: if (liveJob) lastJob = liveJob
    readonly property var job: liveJob ? liveJob : lastJob
    width: ListView.view ? ListView.view.width : 0
    height: rowLayout.implicitHeight + Theme.spacingXl * 2
    color: rowHover.hovered ? Theme.c.downloadsJobRow.backgroundHover : "transparent"

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    HoverHandler { id: rowHover }

    RowLayout {
        id: rowLayout
        anchors.fill: parent
        anchors.margins: Theme.spacingXl
        spacing: Theme.spacingXl

        // The file leads and the status rides on it as an emblem, the way a
        // download list is normally read: what it is first, how it is going second.
        Rectangle {
            Layout.alignment: Qt.AlignTop
            implicitWidth: Theme.spacing3xl + Theme.spacingLg
            implicitHeight: Theme.spacing3xl + Theme.spacingLg
            radius: Theme.c.downloadsJobRow.iconRadius
            color: Theme.c.downloadsJobRow.iconBackground

            IconGlyph {
                anchors.centerIn: parent
                glyph: DownloadFormat.kindGlyph(root.job)
                iconSize: Theme.iconSizeBase
                iconColor: Theme.c.downloadsJobRow.icon
            }

            // Painted on the window background rather than the tile so the
            // emblem stays legible where it overhangs the corner.
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: -Theme.spacingXs
                width: Theme.spacingXl + Theme.spacingXs
                height: width
                radius: Theme.c.downloadsJobRow.badgeRadius
                color: Theme.c.downloadsJobRow.badgeBackground

                IconGlyph {
                    anchors.centerIn: parent
                    glyph: DownloadFormat.statusGlyph(root.job)
                    iconSize: Theme.iconSizeMd
                    iconColor: DownloadFormat.statusColor(root.job)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                Layout.fillWidth: true
                text: root.job.title
                color: Theme.c.downloadsJobRow.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
            }

            // One persistent bar per download: it eases toward each new value in place and
            // never restarts from empty. It sweeps (indeterminate) until the length is known
            // and the first real value has arrived, so a bar parked at 0 never reads as a stall.
            ThemedProgressBar {
                Layout.fillWidth: true
                visible: root.job.status === "downloading"
                indeterminate: !(Number(root.job.bytesTotal) > 0)
                               || (Number(root.job.progress || 0) <= 0
                                   && Number(root.job.bytesReceived || 0) <= 0)
                value: Number(root.job.progress || 0)
            }

            Text {
                Layout.fillWidth: true
                text: DownloadFormat.detailLine(root.job)
                color: root.job.status === "failed" ? Theme.c.downloadsJobRow.destructive
                                                        : Theme.c.downloadsJobRow.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                maximumLineCount: 2
            }

            // Where the file went. Worth showing because the user chose it, and
            // a download that landed somewhere they did not expect is the thing
            // they will come here to check.
            Text {
                Layout.fillWidth: true
                visible: root.job.status === "done"
                         && String(root.job.destinationDir).length > 0
                text: root.job.destinationDir
                color: Theme.c.downloadsJobRow.foregroundMuted
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
                elide: Text.ElideLeft
            }
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            visible: root.job.status === "downloading"
                     && Number(root.job.bytesTotal) > 0
            text: Math.round(Number(root.job.progress || 0) * 100) + "%"
            color: Theme.c.downloadsJobRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            visible: root.job.retryable
            glyph: Theme.icons.refresh
            tooltip: qsTr("Try again")
            onClicked: root.job.kind === "update"
                       ? Updates.retryDownload()
                       : Downloads.retry(root.job.itemId)
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            visible: !root.job.finished
            glyph: Theme.icons.x
            tooltip: qsTr("Cancel")
            enabled: root.job.status !== "preparing"
            onClicked: root.job.kind === "update"
                       ? Updates.cancelDownload()
                       : Downloads.cancel(root.job.itemId)
        }
    }
}
