#pragma once

#include <QImage>
#include <QQuickImageProvider>

// Dafin: a Scene Title card for Titles › Scenes, drawn by the native scene renderer (the same
// pixels the timeline gets) over the proofs' BASE matte. image://scenetitle/<sceneId>?t=<seconds>
// picks the design instant; without t it is the middle of the hold. A scene that cannot be drawn
// (a missing font) gives a dark card, and its error is in the install report.
class SceneTitlePreviewImageProvider : public QQuickImageProvider
{
public:
    SceneTitlePreviewImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
