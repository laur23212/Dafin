#pragma once

// Dafin: the native Scene Title renderer (docs/scene-titles.md). A scene is drawn in one Skia pass
// per frame onto a premultiplied, transparent layer, with the maths of its render profile:
//
// - sprite2x: text is drawn at 2x from the scene's exact font file, reduced with the same Lanczos
//   filter the proofs used and laid at whole pixels; rects are solid; lines and routes are
//   antialiased strokes. Rounded shapes are drawn as Skia vectors (no port uses them yet).
// - mask1x: text is a 1x HarfBuzz-shaped mask, cut by clipTo and scaled by opacity; rects replace
//   what is under them, alpha included.
//
// Everything that does not change per frame (fonts, text sprites and masks, constrained widths)
// lives in an immutable SceneRenderer built once per document + param values; painters only add
// the frame's design time, so a static frame keeps one cache key. Skia-free header.

#include "core/scene/SceneTitle.h"
#include "engine/render2d/skia/VectorPainter.h"

#include <QImage>
#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QStringList>

#include <memory>

namespace drift::skia {

class SceneRenderer;

struct SceneBuild
{
    std::shared_ptr<const SceneRenderer> renderer; // null on error
    QString error;
};

// Font folders searched for a scene's files, in this order: `extra`, the system font folders,
// <appDir>/scene-fonts, then the source tree's scene-fonts/ (developer and test builds).
QStringList sceneFontDirs(const QStringList &extra = {});

// Loads, resolves and measures. Cached by document + params + extra font dirs (a few dozen kept).
SceneBuild sceneRenderer(const QJsonObject &doc, const QJsonObject &params = {},
                         const QStringList &extraFontDirs = {});

const scene::Scene &sceneOf(const SceneRenderer &renderer);

// A painter of the scene at design time `designT` (seconds). The scene's canvas is fitted into
// `size` (device px) by height and centred, as a 16:9 design fills a 16:9 layer exactly.
std::shared_ptr<const VectorPainter> makeScenePainter(std::shared_ptr<const SceneRenderer> renderer,
                                                      double designT, QSize size);

// CPU frame, premultiplied ARGB32 of `size` (the scene canvas when empty).
QImage renderSceneFrame(const std::shared_ptr<const SceneRenderer> &renderer, double designT,
                        QSize size = {});

// The BASE matte of the Stage13+ proofs (DARK + 7.2·x + 3.2·(1−y)), RGB32.
QImage sceneBaseMatte(QSize size, scene::Rgba dark = {9, 12, 17, 255});

// The frame laid over a matte the way the proofs did (uint8(clip(dst·(1−a) + src·a))).
QImage compositeOverMatte(const QImage &premultipliedFrame, const QImage &matte);

// Rest boxes of every drawn actor in design px, for comparisons and the inspector.
QHash<QString, QRectF> sceneRestBoxes(const SceneRenderer &renderer);

} // namespace drift::skia
