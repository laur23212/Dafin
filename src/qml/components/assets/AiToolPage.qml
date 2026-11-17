import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: the scrolling body every AI tool view shares (all but Auto captions, which scrolls its
// transcript instead): a muted line on what the tool does, then the tool's own controls, stacked.
Item {
    id: root

    property string description: ""
    // The width the controls should take.
    readonly property real contentWidth: body.width
    default property alias content: body.data

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: body.height + Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: body
            x: Theme.pagePadding
            width: scroller.width - Theme.pagePadding * 2
            topPadding: Theme.spacingSm
            spacing: Theme.spacingLg

            AiHint {
                text: root.description
                visible: text.length > 0
            }
        }
    }
}
