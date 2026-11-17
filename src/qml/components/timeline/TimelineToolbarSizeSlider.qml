import QtQuick
import Drift

// Timeline size: makes tracks, clips, filmstrips and waveforms bigger or smaller, like Filmora's
// timeline size slider. Separate from the horizontal zoom. Double-click resets to the default;
// the handle clicks onto the Small / Medium / Large / Extra large presets while dragging.
ThemedSlider {
    id: sizeSlider

    label: qsTr("Timeline size")
    width: 84
    anchors.verticalCenter: parent.verticalCenter
    from: EditorState.timelineSizeMin()
    to: EditorState.timelineSizeMax()
    defaultValue: 1.0
    detents: [0.75, 1.0, 1.5, 2.0]
    detentRange: 0.03
    value: EditorState.timelineSize
    onMoved: EditorState.timelineSize = value
    // The reset assigns `value` directly, which would drop the binding above; put it back.
    onDefaultRestored: v => {
        EditorState.timelineSize = v
        sizeSlider.value = Qt.binding(() => EditorState.timelineSize)
    }
    valueFormatter: function () {
        return qsTr("Size %1×").arg(EditorState.timelineSize.toFixed(2))
    }
}
