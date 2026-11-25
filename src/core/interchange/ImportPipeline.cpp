#include "core/interchange/ImportPipeline.h"

#include "core/interchange/filmora/FilmoraProjectParser.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>

namespace drift::interchange {

namespace {

// Each file is asked about once, whoever asks (the search for a moved file, then the layout).
MediaProbeFn cachedProbe(const MediaProbeFn &probe)
{
    if (!probe)
        return {};
    auto cache = std::make_shared<QHash<QString, MediaFacts>>();
    return [probe, cache](const QString &path) {
        const auto it = cache->constFind(path);
        if (it != cache->constEnd())
            return *it;
        const MediaFacts facts = probe(path);
        cache->insert(path, facts);
        return facts;
    };
}

} // namespace

bool fitImportedLayouts(Project &project, const MediaProbeFn &probe, const CancelPtr &cancel)
{
    QHash<QString, QSize> sizes;
    const auto sizeOf = [&](const Clip &clip) {
        const auto it = sizes.constFind(clip.path);
        if (it != sizes.constEnd())
            return *it;
        QSize size;
        if (probe && QFileInfo(clip.path).isFile()) {
            const MediaFacts facts = probe(clip.path);
            if (facts.known)
                size = facts.size;
        }
        sizes.insert(clip.path, size);
        return size;
    };
    const double canvasW = qMax(1, project.width());
    const double canvasH = qMax(1, project.height());
    for (Track &track : project.tracks()) {
        for (Clip &clip : track.clips) {
            if (clip.type != ClipType::Video && clip.type != ClipType::Image)
                continue;
            if (isCancelled(cancel))
                return false;
            const QSize media = sizeOf(clip);
            double x = 0, y = 0, w = canvasW, h = canvasH;
            if (media.width() > 0 && media.height() > 0) {
                const double scale = qMin(canvasW / media.width(), canvasH / media.height());
                w = media.width() * scale;
                h = media.height() * scale;
                x = (canvasW - w) / 2.0;
                y = (canvasH - h) / 2.0;
            }
            clip.transformX = {};
            clip.transformY = {};
            clip.transformW = {};
            clip.transformH = {};
            clip.transformX.setKeyframe(0, x);
            clip.transformY.setKeyframe(0, y);
            clip.transformW.setKeyframe(0, qMax(1.0, w));
            clip.transformH.setKeyframe(0, qMax(1.0, h));
        }
    }
    return !isCancelled(cancel);
}

ImportOutcome runFilmoraImport(const ImportRequest &request)
{
    ImportOutcome out;
    const auto cancelled = [&out]() {
        out = ImportOutcome{};
        out.status = ImportOutcome::Status::Cancelled;
        return out;
    };
    if (isCancelled(request.cancel))
        return cancelled();

    QString readError;
    out.timeline = filmora::parse(request.wfpPath, &readError, request.cancel);
    if (!out.timeline) {
        if (isCancelled(request.cancel) || readError == QLatin1String("cancelled"))
            return cancelled();
        out.status = ImportOutcome::Status::Failed;
        out.error = readError.isEmpty() ? QCoreApplication::translate("Import", "Failed to open the Filmora project")
                                        : readError;
        return out;
    }

    const MediaProbeFn probe = cachedProbe(request.probe);
    BuildOptions options;
    options.probe = probe;
    options.cancel = request.cancel;
    options.relinkChoices = request.relinkChoices;
    out.built = toProject(*out.timeline, request.wfpPath, options);
    if (out.built.cancelled || isCancelled(request.cancel))
        return cancelled();
    if (!out.built.questions.isEmpty()) {
        out.status = ImportOutcome::Status::NeedsChoice;
        return out;
    }
    if (!fitImportedLayouts(out.built.project, probe, request.cancel))
        return cancelled();
    out.status = ImportOutcome::Status::Ok;
    return out;
}

} // namespace drift::interchange
