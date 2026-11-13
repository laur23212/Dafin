import QtQuick
import QtQuick.Controls.Basic
import Drift
// Dafin: Templates > Split Screen. Filmora's thirty layouts (Split Screen 1 … 30) and two extras as
// 16:9 cards, their cells numbered the way Filmora numbers them; hovering one plays its entrance.
// Drag a card onto the timeline (or the preview) for a split screen clip whose cells take the media
// dropped on them; a click adds one at the playhead, or — with two or more clips selected — puts
// those clips into the cells (AppController::activateSplitScreen).
GridView {
    id: root

    readonly property var layouts: EditorState.splitScreenLayouts()
    readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth * 1.5 + Theme.assetCardGap)))
    readonly property real cardWidth: Math.floor(width / columnCount) - Theme.assetCardGap
    readonly property real cardHeight: Math.round(cardWidth * 9 / 16)

    topMargin: 12
    bottomMargin: 12
    clip: true
    reuseItems: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: AppScrollBar { }
    cellWidth: Math.floor(width / columnCount)
    cellHeight: cardHeight + 4 + labelMetrics.height + Theme.assetCardGap
    model: layouts

    FontMetrics {
        id: labelMetrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        font.weight: Font.Medium
    }

    delegate: Column {
        id: card
        required property var modelData
        width: root.cardWidth
        spacing: 4

        Rectangle {
            width: root.cardWidth
            height: root.cardHeight
            radius: Theme.c.effectTemplateCard.radius
            color: cardDrag.hovered ? Theme.c.effectTemplateCard.backgroundHover : Theme.c.effectTemplateCard.background
            border.width: Theme.c.effectTemplateCard.borderWidth
            border.color: Qt.rgba(Theme.c.effectTemplateCard.accent.r, Theme.c.effectTemplateCard.accent.g,
                                  Theme.c.effectTemplateCard.accent.b, 0.35)
            clip: true
            scale: cardDrag.hovered && !cardDrag.active ? 1.03 : 1.0

            Behavior on scale {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            SplitScreenThumb {
                anchors.fill: parent
                anchors.margins: 3
                layout: card.modelData
                playing: cardDrag.hovered && !cardDrag.active
            }

            ThemedToolTip {
                text: qsTr("%1 — drag it onto the timeline, or click to add it at the playhead. With two or more clips selected, a click puts them into the cells.")
                          .arg(card.modelData.label)
                visible: cardDrag.hovered && !cardDrag.active
            }

            AssetDragSource {
                id: cardDrag
                anchors.fill: parent
                kind: "splitScreen"
                payload: card.modelData.id
                label: card.modelData.label
                glyph: Theme.icons.layoutGrid
                onTapped: EditorState.activateSplitScreen(card.modelData.id)
            }
        }

        Text {
            width: root.cardWidth
            text: card.modelData.label
            color: Theme.c.effectTemplateCard.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeCard
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
    }
}
