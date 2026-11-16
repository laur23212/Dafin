import QtQuick
import QtQuick.Controls.Basic
import Drift

// What Dafin itself can measure about the connected assistant. The assistant's own token
// counts, cached tokens and price are never sent to Dafin, so they are not shown here and
// nothing below is made up: sizes are of Dafin's replies, and the one token figure is
// labelled as an estimate.
Item {
    id: root

    readonly property var totals: EditorState.mcp.usageTotals
    readonly property var rows: EditorState.mcp.usageRows
    readonly property bool hasCalls: (totals.calls || 0) > 0

    function sizeText(bytes) {
        if (!bytes)
            return "0"
        return bytes < 1024 ? qsTr("%1 B").arg(bytes) : bytes < 1048576
                              ? qsTr("%1 KB").arg((bytes / 1024).toFixed(1))
                              : qsTr("%1 MB").arg((bytes / 1048576).toFixed(1))
    }

    function timeText(ms) {
        return ms < 1000 ? qsTr("%1 ms").arg(Math.round(ms)) : qsTr("%1 s").arg((ms / 1000).toFixed(1))
    }

    Column {
        id: summary
        x: Theme.pagePadding
        y: Theme.spacingSm
        width: parent.width - 2 * Theme.pagePadding
        spacing: Theme.spacingXs

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: EditorState.mcp.usageClient.length > 0
                  ? qsTr("Assistant: %1").arg(EditorState.mcp.usageClient)
                  : qsTr("Assistant: not reported by the assistant")
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: root.hasCalls
            text: qsTr("%1 tool calls, %2 failed, %3 repeated after a failure, %4 in Dafin")
                      .arg(root.totals.calls).arg(root.totals.errors).arg(root.totals.retries)
                      .arg(root.timeText(root.totals.totalMs))
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: root.hasCalls
            text: qsTr("Dafin's replies: %1 of text (about %2 tokens, estimated), %3 of images")
                      .arg(root.sizeText(root.totals.textBytes)).arg(root.totals.approxTokens)
                      .arg(root.sizeText(root.totals.imageBytes))
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("The assistant's own input, output and cached tokens and its price are not sent to Dafin, so they are not shown. Its own app has them.")
        }

        Row {
            spacing: Theme.spacingSm

            ThemedButton {
                variant: "secondary"
                glyph: Theme.icons.copy
                text: qsTr("Copy report")
                enabled: root.hasCalls
                onClicked: {
                    EditorState.mcp.copyUsageReport()
                    Toasts.success(qsTr("Copied usage report"))
                }
            }

            ThemedButton {
                variant: "ghost"
                glyph: Theme.icons.trash
                text: qsTr("Reset")
                enabled: root.hasCalls
                onClicked: EditorState.mcp.resetUsage()
            }
        }
    }

    ThemedLabel {
        anchors.top: summary.bottom
        anchors.topMargin: Theme.spacingLg
        x: Theme.pagePadding
        width: parent.width - 2 * Theme.pagePadding
        visible: !root.hasCalls
        wrapMode: Text.WordWrap
        text: EditorState.mcp.running ? qsTr("No tool calls yet. The numbers fill in as a connected assistant works.")
                                      : qsTr("Turn on agent access to let an assistant edit this project.")
    }

    // Biggest repliers first: those are the operations worth asking an assistant to use less.
    ListView {
        anchors.top: summary.bottom
        anchors.topMargin: Theme.spacingMd
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.hasCalls
        clip: true
        model: root.rows
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        header: Item {
            width: ListView.view.width
            height: 24

            Text {
                x: Theme.pagePadding
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Operation · calls · failed/repeated · average · reply size")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        delegate: Item {
            id: row
            required property var modelData
            width: ListView.view.width
            height: 26

            Row {
                x: Theme.pagePadding
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2 * Theme.pagePadding
                spacing: Theme.spacingSm

                Text {
                    width: Math.max(80, parent.width * 0.34)
                    elide: Text.ElideRight
                    text: row.modelData.tool
                    color: row.modelData.errors > 0 ? Theme.destructive : Theme.foreground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                Text {
                    width: 28
                    horizontalAlignment: Text.AlignRight
                    text: row.modelData.calls
                    color: Theme.foreground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Text {
                    width: 44
                    horizontalAlignment: Text.AlignRight
                    text: row.modelData.errors + "/" + row.modelData.retries
                    color: row.modelData.errors > 0 ? Theme.destructive : Theme.mutedForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Text {
                    width: 56
                    horizontalAlignment: Text.AlignRight
                    text: root.timeText(row.modelData.avgMs)
                    color: Theme.mutedForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Text {
                    width: 90
                    horizontalAlignment: Text.AlignRight
                    text: row.modelData.imageBytes > 0
                          ? root.sizeText(row.modelData.textBytes + row.modelData.imageBytes) + " " + qsTr("(image)")
                          : root.sizeText(row.modelData.textBytes)
                    color: Theme.mutedForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }
}
