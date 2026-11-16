import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: asks before a connected agent runs something that cannot be undone or that removes a
// safeguard (McpController::confirmGate). The agent waits for this answer; only these buttons
// can give it, never anything the agent sends. Escape, the window closing or the 60-second timeout
// all count as "Don't allow". Return does not accept, so a stray key press cannot approve.
ThemedDialog {
    id: root

    title: qsTr("Allow this agent action?")
    preferredWidth: Theme.dialogWidthMd
    acceptText: qsTr("Allow")
    rejectText: qsTr("Don't allow")
    acceptVariant: "destructive"
    acceptOnReturn: false
    closePolicy: Popup.CloseOnEscape

    property string requestId: ""
    property string tool: ""
    property string reason: ""

    function answer(allow) {
        if (requestId.length === 0)
            return
        const id = requestId
        requestId = ""
        EditorState.mcp.answerConfirmation(id, allow)
    }

    onAccepted: answer(true)
    onRejected: answer(false)

    Connections {
        target: EditorState.mcp
        function onConfirmationRequested(id, tool, reason) {
            root.requestId = id
            root.tool = tool
            root.reason = reason
            root.open()
        }
        function onConfirmationClosed(id) {
            if (id === root.requestId) {
                root.requestId = ""
                root.close()
            }
        }
    }

    // The gate stays off until a dialog exists to ask with (headless runs have none).
    Component.onCompleted: EditorState.mcp.registerConfirmUi()

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : Theme.dialogWidthMd

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: root.reason
            Accessible.name: root.reason
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("Requested by the connected agent (%1). It waits for your answer and gives up after a minute.")
                  .arg(root.tool)
        }
    }
}
