import QtQuick
import Drift

// Dafin: a copy button that says whether the copy worked. `copy` returns true only when the
// clipboard read back the copied text (EditorState.mcp.copyText and the other copy calls), so
// "Copied" is never shown for a copy that did not happen.
ThemedButton {
    id: root

    property var copy: function () { return false }
    property string label: ""
    property string copiedText: qsTr("✓ Copied")
    // 0 = ready, 1 = copied, 2 = failed
    property int result: 0

    glyph: result === 1 ? Theme.icons.check : Theme.icons.copy
    text: result === 1 ? copiedText
        : result === 2 ? qsTr("Could not copy, try again")
        : label

    onClicked: {
        root.result = root.copy() ? 1 : 2
        resetTimer.restart()
    }

    Timer {
        id: resetTimer
        interval: 2500
        onTriggered: root.result = 0
    }
}
