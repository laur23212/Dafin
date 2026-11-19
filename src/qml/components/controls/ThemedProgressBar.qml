pragma ComponentBehavior: Bound
import QtQuick
import Drift

// Slim determinate progress bar for long-running jobs shown inline in a panel.
//
// The large ring (LabelledProgressRing) is for dialogs that have the room; this is what a panel
// section uses when a job needs a bar next to a status line and a Cancel button.
//
// One persistent bar: the fill is never recreated or reset. A new `value` only changes the target,
// and the fill eases toward it, so a bar that is fed a new number every second flows instead of
// stepping. While `indeterminate` is set (no real value has arrived yet) a short segment sweeps
// along the track instead; the fill itself keeps its place underneath, so the first real value
// eases out from where the bar stood rather than snapping.
Item {
    id: root

    // 0..1, clamped.
    property real value: 0
    // True until the first real progress value arrives, or while the length is unknown.
    property bool indeterminate: false
    // How long the fill takes to reach a new value.
    property int easeDuration: 250
    property real barHeight: Theme.spacingMd

    implicitHeight: barHeight
    height: barHeight

    ThemeSlot {
        anchors.fill: parent
        slot: "ThemedProgressBarBackground"
        control: root
        fallback: Component {
            Rectangle {
                id: track
                radius: Theme.c.themedProgressBar.trackRadius
                color: Theme.c.themedProgressBar.trackBackground
                clip: true

                Rectangle {
                    id: fill
                    visible: !root.indeterminate
                    width: track.width * Math.max(0, Math.min(1, root.value))
                    height: parent.height
                    radius: track.radius
                    color: Theme.c.themedProgressBar.fillBackground

                    // Eases toward every new target and never passes through zero. Not
                    // re-armed on a value change, so a second update mid-ease just retargets.
                    Behavior on width {
                        NumberAnimation {
                            duration: root.easeDuration
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                // Indeterminate: a segment sweeping left to right until a real value exists.
                Rectangle {
                    id: sweep
                    visible: root.indeterminate
                    width: Math.max(track.height * 3, track.width * 0.3)
                    height: parent.height
                    radius: track.radius
                    color: Theme.c.themedProgressBar.fillBackground

                    SequentialAnimation on x {
                        running: root.indeterminate && root.visible
                        loops: Animation.Infinite
                        NumberAnimation {
                            from: -sweep.width
                            to: track.width
                            duration: 1100
                            easing.type: Easing.InOutSine
                        }
                    }
                }
            }
        }
    }
}
