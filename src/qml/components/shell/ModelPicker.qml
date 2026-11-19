import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: a model dropdown, like the one in the Claude app. `options` is
// [{value, label, hint, recommended}] (ModelCatalog); the last row, "Other…", shows a field to
// type a name. `opened` fires when the list opens, so the owner can load the provider's list.
Column {
    id: root

    property var options: []
    property string current: ""
    property string placeholder: qsTr("Type the model name")
    property bool typing: false

    signal picked(string value)
    signal opened()

    spacing: Theme.spacingSm

    readonly property var rows: {
        var out = []
        for (var i = 0; i < root.options.length; ++i) {
            var o = root.options[i]
            var label = o.label
            if (o.hint && o.hint.length > 0)
                label += "  ·  " + o.hint
            out.push({ label: label, value: o.value, other: false })
        }
        out.push({ label: qsTr("Other…  ·  type a name yourself"), value: "", other: true })
        return out
    }

    readonly property int matchIndex: {
        for (var i = 0; i < root.rows.length - 1; ++i) {
            if (root.rows[i].value === root.current)
                return i
        }
        return root.rows.length - 1
    }

    ThemedComboBox {
        id: box
        width: parent.width
        textRole: "label"
        model: root.rows
        currentIndex: root.typing ? root.rows.length - 1 : root.matchIndex
        Accessible.name: qsTr("Model")
        onActivated: index => {
            var row = root.rows[index]
            if (row.other) {
                root.typing = true
                field.forceActiveFocus()
            } else {
                root.typing = false
                root.picked(row.value)
            }
        }

        Connections {
            target: box.popup
            function onAboutToShow() { root.opened() }
        }
    }

    ThemedTextField {
        id: field
        width: parent.width
        visible: root.typing || root.matchIndex === root.rows.length - 1
        placeholderText: root.placeholder
        text: root.current
        Accessible.name: qsTr("Model name")
        onEditingFinished: {
            var name = text.trim()
            if (name.length > 0)
                root.picked(name)
        }
    }
}
