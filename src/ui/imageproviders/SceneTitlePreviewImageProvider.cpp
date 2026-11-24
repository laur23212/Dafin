#include "ui/imageproviders/SceneTitlePreviewImageProvider.h"

#include "core/scene/SceneCatalog.h"
#include "core/scene/SceneTitle.h"
#ifdef DRIFT_WITH_SKIA
#include "engine/render2d/scene/ScenePainter.h"
#endif

#include <QUrlQuery>

SceneTitlePreviewImageProvider::SceneTitlePreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage SceneTitlePreviewImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int q = id.indexOf(QLatin1Char('?'));
    const QString sceneId = q < 0 ? id : id.left(q);
    const QUrlQuery query(q < 0 ? QString() : id.mid(q + 1));
    const QSize card(requestedSize.width() > 0 ? requestedSize.width() : 384,
                     requestedSize.height() > 0 ? requestedSize.height() : 216);
    QImage out(card, QImage::Format_RGB32);
    out.fill(QColor(9, 12, 17));
#ifdef DRIFT_WITH_SKIA
    if (const std::optional<drift::scene::SceneEntry> e = drift::scene::SceneCatalog::instance().find(sceneId)) {
        const drift::skia::SceneBuild build = drift::skia::sceneRenderer(e->document);
        if (build.renderer) {
            const drift::scene::Scene &sc = drift::skia::sceneOf(*build.renderer);
            const double t = query.hasQueryItem(QStringLiteral("t"))
                                 ? query.queryItemValue(QStringLiteral("t")).toDouble()
                                 : (sc.holdStart + sc.holdEnd) / 2.0;
            const QImage frame = drift::skia::renderSceneFrame(build.renderer, t, card);
            out = drift::skia::compositeOverMatte(frame, drift::skia::sceneBaseMatte(card, sc.mixColor));
        }
    }
#endif
    if (size)
        *size = out.size();
    return out;
}
