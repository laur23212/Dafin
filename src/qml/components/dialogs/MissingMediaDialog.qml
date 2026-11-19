import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: shown after a project opens with media files that could not be found. "Find in folder…"
// walks the chosen folder (and the folders inside it) for the missing files by name and size and
// relinks what it finds as one undoable edit. "Later" leaves it: each missing item in Media also
// has Locate file….
ThemedDialog {
    id: root

    property int count: 0

    title: qsTr("Some media can’t be found")
    acceptText: qsTr("Find in folder…")
    rejectText: qsTr("Later")
    rejectVariant: "secondary"
    preferredWidth: 440

    function openFor(missing) {
        count = missing
        open()
    }

    onAccepted: {
        var url = FileDialogs.openDirectory(qsTr("Find Missing Media"))
        if (!url || url.toString() === "")
            return
        var found = EditorState.bin.relinkMissingFromFolder(url)
        if (found === 0)
            Toasts.warning(qsTr("No missing files were found in that folder."))
    }

    contentItem: ThemedLabel {
        width: parent ? parent.width : 400
        size: "sm"
        wrapMode: Text.WordWrap
        text: qsTr("%n media file(s) used by this project are missing. Pick the folder they are in now (or a folder above it) and Dafin will look for them by name and size.", "", root.count)
    }
}
