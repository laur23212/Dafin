import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Dafin: shown while importing a Filmora project when a media file's name matches several
// different files near the project. Dafin does not guess which is meant: it lists them with what
// is known about each (folder, size, length) and lets the person choose, or leave the clip
// unlinked. Closing the dialog cancels the import; the open project is left as it was.
ThemedDialog {
    id: root

    // [{key, clipName, candidates: [{path, folder, sizeBytes, durationUs, checked}]}]
    property var questions: []
    // key -> index of the chosen candidate; -1 leaves the clip unlinked
    property var picks: ({})
    property bool answered: false

    title: qsTr("Which file is it?")
    acceptText: qsTr("Continue import")
    rejectText: qsTr("Cancel import")
    rejectVariant: "secondary"
    preferredWidth: 560
    closePolicy: Popup.NoAutoClose

    function openFor(list) {
        questions = list
        var initial = {}
        for (var i = 0; i < list.length; ++i)
            initial[list[i].key] = 0
        picks = initial
        answered = false
        open()
    }

    function sizeText(bytes) {
        if (bytes < 0)
            return ""
        if (bytes >= 1073741824)
            return (bytes / 1073741824).toFixed(2) + " GB"
        if (bytes >= 1048576)
            return (bytes / 1048576).toFixed(1) + " MB"
        return Math.max(1, Math.round(bytes / 1024)) + " KB"
    }

    function lengthText(us) {
        if (us === undefined || us < 0)
            return ""
        var s = Math.round(us / 1000000)
        return Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2)
    }

    function describe(c) {
        var parts = [c.folder]
        var size = sizeText(c.sizeBytes)
        var len = lengthText(c.durationUs)
        if (size !== "")
            parts.push(size)
        if (len !== "")
            parts.push(len)
        return parts.join("  ·  ")
    }

    onAccepted: {
        answered = true
        var answer = {}
        for (var i = 0; i < questions.length; ++i) {
            var q = questions[i]
            var index = picks[q.key]
            answer[q.key] = (index >= 0 && index < q.candidates.length) ? q.candidates[index].path : ""
        }
        EditorState.projectFile.answerImportRelinkQuestions(answer)
    }

    // Any way out other than Continue ends the import.
    onClosed: {
        if (!answered)
            EditorState.projectFile.cancelOtherEditorImport()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 520

        ThemedLabel {
            Layout.fillWidth: true
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("Several different files have the name of a media file this project uses, and nothing tells them apart. Choose the right one for each, or leave the clip unlinked and link it later.")
        }

        Repeater {
            model: root.questions

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs

                required property var modelData

                ThemedLabel {
                    Layout.fillWidth: true
                    size: "sm"
                    font.bold: true
                    elide: Text.ElideMiddle
                    text: modelData.clipName
                }

                ThemedComboBox {
                    Layout.fillWidth: true
                    model: {
                        var list = []
                        for (var i = 0; i < modelData.candidates.length; ++i)
                            list.push(root.describe(modelData.candidates[i]))
                        list.push(qsTr("Leave this clip unlinked"))
                        return list
                    }
                    currentIndex: {
                        var picked = root.picks[modelData.key]
                        return picked < 0 ? modelData.candidates.length : picked
                    }
                    onActivated: function (index) {
                        var next = root.picks
                        next[modelData.key] = index >= modelData.candidates.length ? -1 : index
                        root.picks = next
                    }
                }
            }
        }
    }
}
