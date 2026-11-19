import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift
// Every download the app runs: what is running, what waits behind the concurrency cap, and what
// this session already finished. History is session-only by design; the files themselves land
// in the media bin and the folder the user picked, so this is a progress view, not a record.
Item {
    id: root

    required property var section
    required property var host

    readonly property var jobs: Downloads.jobs

    // The list is fed job ids, and reassigned only when the set or order of jobs changes. Feeding
    // it Downloads.jobs directly rebuilt the whole list at every progress tick (about every
    // 0.4 s), destroying each row and its progress bar, so the bar restarted from empty and then
    // grew back: a slideshow. With ids, a row lives for its whole download and its bar eases in
    // place.
    property var jobIds: []

    function syncJobIds() {
        const list = Downloads.jobs
        const next = []
        for (let i = 0; i < list.length; ++i)
            next.push(list[i].id)
        if (next.join("\n") !== jobIds.join("\n"))
            jobIds = next
    }

    Component.onCompleted: syncJobIds()

    Connections {
        target: Downloads
        function onJobsChanged() { root.syncJobIds() }
    }
    readonly property int activeCount: Downloads.activeCount
    readonly property bool hasFinished: {
        const list = Downloads.jobs
        for (let i = 0; i < list.length; ++i) {
            if (list[i].finished)
                return true
        }
        return false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.pagePadding
            spacing: Theme.spacingSm

            Text {
                text: root.activeCount > 0 ? qsTr("%n active", "", root.activeCount)
                                           : qsTr("No downloads running")
                color: Theme.c.marketplace.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.DemiBold
            }

            ThemedLabel {
                visible: root.activeCount > StockInstaller.maxConcurrentDownloads
                text: qsTr("%1 at a time").arg(StockInstaller.maxConcurrentDownloads)
            }

            Item { Layout.fillWidth: true }

            ThemedButton {
                text: qsTr("Clear finished")
                variant: "ghost"
                enabled: root.hasFinished
                onClicked: {
                    Updates.clearDownloadState()
                    Downloads.clearFinished()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.c.marketplace.borderWidth
            color: Theme.c.marketplace.border
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.jobs.length === 0
            glyph: Theme.icons.download
            title: qsTr("Nothing downloaded yet")
            hint: qsTr("Downloads from the Marketplace show up here while they run.")
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.jobs.length > 0
            clip: true
            model: root.jobIds
            spacing: 0
            ScrollBar.vertical: AppScrollBar { }

            delegate: DownloadsJobRow { }
        }
    }
}
