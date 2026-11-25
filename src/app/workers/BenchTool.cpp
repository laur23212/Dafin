#include "app/workers/BenchTool.h"

#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "core/project/Transition.h"
#include "core/project/clip/Clip.h"
#include "engine/EngineTuning.h"
#include "engine/compositor/FrameCompositor.h"
#include "engine/compositor/GpuCompositor.h"
#include "engine/decode/probe/MediaProbe.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/diagnostics/BenchPhases.h"
#include "engine/diagnostics/BenchStats.h"
#include "engine/diagnostics/RouteStats.h"
#include "engine/encode/export/EncoderProbe.h"
#include "engine/encode/export/Exporter.h"
#include "engine/encode/proxies/ReverseProxyCache.h"
#include "engine/packages/assets/EmojiCatalog.h"
#include "engine/packages/assets/FontCatalog.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/project/ProjectBundle.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <numeric>
#include <optional>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef DRIFT_VERSION
#define DRIFT_VERSION ""
#endif
#ifndef DAFIN_BUILD_LABEL
#define DAFIN_BUILD_LABEL ""
#endif

// How the benchmark works and how to read it: docs/cuda-benchmark.md. In short: every run is its
// own child process (clean decoder, GL and cache state, and the environment switch for the variant
// set before anything starts), the two variants of a pair alternate A B / B A, one warm-up run of
// each is made first and left out, and the summary states median and p95 across runs. Nothing in a
// report names a file: media is identified by size and SHA-256.

namespace drift::worker {

namespace {

namespace bench = drift::bench;
namespace routes = drift::routes;

constexpr const char *kRunSchema = "dafin-bench-run/1";
constexpr const char *kSummarySchema = "dafin-bench-summary/1";

// dafin.exe is a GUI program: from a console its output goes nowhere unless it attaches to the
// console it was started from (as TemplateTool does).
void attachConsole()
{
#ifdef Q_OS_WIN
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *ignored = nullptr;
        if (_fileno(stdout) < 0)
            freopen_s(&ignored, "CONOUT$", "w", stdout);
        if (_fileno(stderr) < 0)
            freopen_s(&ignored, "CONOUT$", "w", stderr);
    }
#endif
}

void say(const QString &text)
{
    std::fputs(text.toUtf8().constData(), stdout);
    std::fputs("\n", stdout);
    std::fflush(stdout);
}

QString argValue(const QStringList &args, const QString &name, const QString &fallback = {})
{
    const int at = args.indexOf(name);
    return at >= 0 && at + 1 < args.size() ? args.at(at + 1) : fallback;
}

QString sha256OfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

bool writeJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return true;
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonArray toArray(const std::vector<double> &v)
{
    QJsonArray out;
    for (double x : v)
        out.append(x);
    return out;
}

std::vector<double> fromArray(const QJsonValue &value)
{
    std::vector<double> out;
    for (const QJsonValue &v : value.toArray())
        out.push_back(v.toDouble());
    return out;
}

QJsonObject routesJson(const routes::Snapshot &snap)
{
    QJsonObject imports;
    for (int i = 0; i < routes::kImportRoutes; ++i)
        imports.insert(QLatin1String(routes::id(routes::ImportRoute(i))), double(snap.imports[i]));
    QJsonObject encodes;
    for (int i = 0; i < routes::kEncodeRoutes; ++i)
        encodes.insert(QLatin1String(routes::id(routes::EncodeRoute(i))), double(snap.encodes[i]));
    QJsonObject fallbacks;
    for (int i = 0; i < routes::kFallbacks; ++i)
        fallbacks.insert(QLatin1String(routes::id(routes::Fallback(i))), double(snap.fallbacks[i]));
    return QJsonObject{
        {QStringLiteral("imports"), imports},
        {QStringLiteral("encodes"), encodes},
        {QStringLiteral("fallbacks"), fallbacks},
        {QStringLiteral("plan"), QLatin1String(routes::id(snap.plan))},
    };
}

QJsonObject phasesJson(const bench::Snapshot &snap)
{
    QJsonObject out;
    for (int i = 0; i < bench::kPhases; ++i) {
        const bench::PhaseTotals &p = snap.phases[i];
        out.insert(QLatin1String(bench::id(bench::Phase(i))),
                   QJsonObject{{QStringLiteral("ms"), double(p.ns) / 1e6},
                               {QStringLiteral("count"), double(p.count)},
                               {QStringLiteral("max_ms"), double(p.maxNs) / 1e6}});
    }
    return out;
}

double phaseMs(const QJsonObject &run, const char *phase)
{
    return run.value(QStringLiteral("phases"))
        .toObject()
        .value(QLatin1String(phase))
        .toObject()
        .value(QStringLiteral("ms"))
        .toDouble();
}

double routeCount(const QJsonObject &routesObj, const char *group, const char *id)
{
    return routesObj.value(QLatin1String(group)).toObject().value(QLatin1String(id)).toDouble();
}

QJsonObject buildInfo()
{
    return QJsonObject{{QStringLiteral("version"), QStringLiteral(DRIFT_VERSION)},
                       {QStringLiteral("label"), QStringLiteral(DAFIN_BUILD_LABEL)}};
}

QJsonObject machineInfo()
{
    const bool gpu = GpuCompositor::isAvailable();
    const drift::gl::GlStatusInfo gl = GpuCompositor::status();
    return QJsonObject{
        {QStringLiteral("gpu_compositor"), gpu},
        {QStringLiteral("gl_vendor"), gl.vendor},
        {QStringLiteral("gl_renderer"), gl.renderer},
        {QStringLiteral("gl_version"), QStringLiteral("%1.%2%3").arg(gl.major).arg(gl.minor).arg(gl.isEs ? QStringLiteral(" ES") : QString())},
        {QStringLiteral("gl_software"), gl.software},
        // GPU names, PCI ids, driver versions and the FFmpeg version; no paths.
        {QStringLiteral("signature"), drift::encoderprobe::machineSignature()},
    };
}

// ---------------------------------------------------------------------------------------------
// The child: one measured run.

bool runExport(const drift::Project &project, const QStringList &args, QJsonObject *result)
{
    const QString codec = argValue(args, QStringLiteral("--codec"), QStringLiteral("h264"));
    const QString outPath = argValue(args, QStringLiteral("--export-file"));
    const double seconds = argValue(args, QStringLiteral("--seconds"), QStringLiteral("20")).toDouble();

    const QVariantMap codecInfo = Exporter::videoCodecById(codec);
    result->insert(QStringLiteral("codec"), codec);
    result->insert(QStringLiteral("hardware"), codecInfo.value(QStringLiteral("hardware")).toBool());
    if (!codecInfo.value(QStringLiteral("available")).toBool()) {
        result->insert(QStringLiteral("status"), QStringLiteral("UNSUPPORTED"));
        result->insert(QStringLiteral("reason"), QStringLiteral("The encoder is not available on this machine"));
        return false;
    }

    ExportSettings settings = Exporter::defaultSettings();
    settings.videoCodecId = codec;
    settings.rateControl = QStringLiteral("crf");
    settings.crf = codecInfo.value(QStringLiteral("defaultCrf"), 23).toInt();
    settings.videoPreset = codecInfo.value(QStringLiteral("defaultPreset")).toString();
    settings.targetHeight = 0;
    settings.startUs = 0;
    settings.endUs = std::min<drift::TimeUs>(project.durationUs(), drift::TimeUs(seconds * 1e6));

    QString error;
    QElapsedTimer timer;
    timer.start();
    const bool ok = Exporter::run(project, settings, outPath, &error, [](double) { return true; });
    const double wallS = double(timer.nsecsElapsed()) / 1e9;

    const double frames = std::round(double(settings.endUs - settings.startUs) * project.fps() / 1e6);
    result->insert(QStringLiteral("wall_s"), wallS);
    result->insert(QStringLiteral("frames"), frames);
    result->insert(QStringLiteral("fps_achieved"), wallS > 0 ? frames / wallS : 0.0);
    result->insert(QStringLiteral("crf"), settings.crf);
    result->insert(QStringLiteral("preset"), settings.videoPreset);
    if (!ok) {
        // The error may name the output file; the report names no files.
        error.replace(QDir::toNativeSeparators(outPath), QStringLiteral("<output>"));
        error.replace(outPath, QStringLiteral("<output>"));
        result->insert(QStringLiteral("status"), QStringLiteral("FAIL"));
        result->insert(QStringLiteral("reason"), error.isEmpty() ? QStringLiteral("Export failed") : error);
        return false;
    }

    // What came out, checked independently of the exporter's own idea of it.
    const QFileInfo info(outPath);
    const MediaInfo out = MediaProbe::probe(outPath);
    result->insert(QStringLiteral("out_bytes"), double(info.size()));
    result->insert(QStringLiteral("out_duration_s"), out.ok ? double(out.durationUs) / 1e6 : 0.0);
    for (const StreamInfo &s : out.streams) {
        if (s.type == StreamInfo::Type::Video && !s.attachedPicture) {
            result->insert(QStringLiteral("out_w"), s.width);
            result->insert(QStringLiteral("out_h"), s.height);
            result->insert(QStringLiteral("out_fps"), s.fps);
            break;
        }
    }
    result->insert(QStringLiteral("status"), QStringLiteral("PASS"));
    return true;
}

bool runPreview(const drift::Project &project, const QStringList &args, QJsonObject *result)
{
    const int frames = argValue(args, QStringLiteral("--frames"), QStringLiteral("300")).toInt();
    const int seeks = argValue(args, QStringLiteral("--seeks"), QStringLiteral("60")).toInt();
    const double scale = argValue(args, QStringLiteral("--preview-scale"), QStringLiteral("1")).toDouble();

    FrameCompositor compositor;
    compositor.setProject(&project);

    // What PlaybackEngine::playbackRenderOptions asks for while playing at Full quality, with
    // proxies off so the original media (and its decoder) is what gets measured.
    FrameCompositor::RenderOptions play;
    play.previewScale = std::clamp(scale, 0.05, 1.0);
    play.readAheadUs = drift::TimeUs(drift::tuning::readAheadMs.load(std::memory_order_relaxed)) * 1000;
    play.allowProxies = false;
    play.deferAudioAnalysis = true;

    const int fps = std::max(1, project.fps());
    const drift::TimeUs step = 1'000'000 / fps;
    const drift::TimeUs duration = project.durationUs();
    constexpr int kWarm = 10;
    const int available = int(duration / step) - kWarm - 1;
    const int n = std::max(0, std::min(frames, available));
    result->insert(QStringLiteral("budget_ms"), 1000.0 / fps);
    result->insert(QStringLiteral("preview_scale"), play.previewScale);
    if (n <= 0) {
        result->insert(QStringLiteral("status"), QStringLiteral("BLOCKED"));
        result->insert(QStringLiteral("reason"), QStringLiteral("The project is too short for a playback sweep"));
        return false;
    }

    // Warm decoders, shaders and the GL context first; that is not what playback feels like.
    for (int i = 0; i < kWarm; ++i)
        compositor.compositeToTextureAt(drift::TimeUs(i) * step, play);
    bench::reset();
    routes::reset();

    // Playback: a forward sweep, timed the way CompositorService times a preview frame.
    std::vector<double> frameMs, frameSyncMs, frameDecodeMs;
    int invalid = 0;
    const int syncIndex = int(bench::Phase::PreviewCudaSync);
    QElapsedTimer sweep;
    sweep.start();
    for (int i = 0; i < n; ++i) {
        const drift::TimeUs t = drift::TimeUs(kWarm + i) * step;
        ClipReaderPool::resetDecodeWaitNs();
        const uint64_t syncBefore = bench::snapshot().phases[syncIndex].ns;
        QElapsedTimer timer;
        timer.start();
        const GpuFrameTexture frame = compositor.compositeToTextureAt(t, play);
        frameMs.push_back(double(timer.nsecsElapsed()) / 1e6);
        frameDecodeMs.push_back(double(ClipReaderPool::decodeWaitNs()) / 1e6);
        frameSyncMs.push_back(double(bench::snapshot().phases[syncIndex].ns - syncBefore) / 1e6);
        if (!frame.isValid())
            ++invalid;
    }
    const double sweepS = double(sweep.nsecsElapsed()) / 1e9;
    result->insert(QStringLiteral("routes"), routesJson(routes::snapshot()));
    result->insert(QStringLiteral("frame_ms"), toArray(frameMs));
    result->insert(QStringLiteral("frame_sync_ms"), toArray(frameSyncMs));
    result->insert(QStringLiteral("frame_decode_ms"), toArray(frameDecodeMs));
    result->insert(QStringLiteral("invalid_frames"), invalid);
    result->insert(QStringLiteral("sweep_wall_s"), sweepS);
    result->insert(QStringLiteral("fps_achieved"), sweepS > 0 ? n / sweepS : 0.0);

    // Scrub: seeks to fixed pseudo-random times (same seed every run), paused, approximate after
    // the first two as PlaybackEngine does while the playhead is dragged.
    routes::reset();
    FrameCompositor::RenderOptions scrub = play;
    scrub.readAheadUs = 0;
    std::vector<double> seekMs, seekDecodeMs;
    int invalidSeeks = 0;
    uint32_t state = 0x9E3779B9u;
    for (int k = 0; k < seeks; ++k) {
        state = state * 1664525u + 1013904223u; // fixed LCG: the same positions on every machine
        const drift::TimeUs t = drift::TimeUs((double(state) / 4294967296.0) * double(duration - step));
        scrub.approximateSeek = k >= 2;
        ClipReaderPool::resetDecodeWaitNs();
        QElapsedTimer timer;
        timer.start();
        const GpuFrameTexture frame = compositor.compositeToTextureAt(t, scrub);
        seekMs.push_back(double(timer.nsecsElapsed()) / 1e6);
        seekDecodeMs.push_back(double(ClipReaderPool::decodeWaitNs()) / 1e6);
        if (!frame.isValid())
            ++invalidSeeks;
    }
    result->insert(QStringLiteral("scrub_routes"), routesJson(routes::snapshot()));
    result->insert(QStringLiteral("seek_ms"), toArray(seekMs));
    result->insert(QStringLiteral("seek_decode_ms"), toArray(seekDecodeMs));
    result->insert(QStringLiteral("invalid_seeks"), invalidSeeks);

    // A run that drew nothing is not a fast run.
    const bool ok = invalid == 0 && invalidSeeks == 0;
    result->insert(QStringLiteral("status"), ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    if (!ok)
        result->insert(QStringLiteral("reason"),
                       QStringLiteral("%1 playback frames and %2 seeks came back empty").arg(invalid).arg(invalidSeeks));
    return ok;
}

// ---------------------------------------------------------------------------------------------
// The driver.

struct Variant
{
    QString name;
    QString mode; // "export" | "preview"
    QList<QPair<QString, QString>> env;
};

// The fixed scene for --media: two decoded layers on every frame, an effect chain on top, and a
// crossfade when two files are given, like tools/benchcomposite. Same files in, same project out.
std::optional<drift::Project> sceneFromMedia(const QStringList &paths, const QString &scene, double seconds,
                                             QString *error)
{
    struct Source
    {
        QString path;
        drift::TimeUs duration = 0;
        int w = 0, h = 0;
        double fps = 30;
    };
    QList<Source> sources;
    for (const QString &path : paths) {
        const MediaInfo info = MediaProbe::probe(path);
        if (!info.ok || info.durationUs <= 0) {
            *error = QStringLiteral("Could not read media file %1: %2").arg(sources.size() + 1).arg(info.errorString);
            return std::nullopt;
        }
        Source s;
        s.path = path;
        s.duration = info.durationUs;
        for (const StreamInfo &st : info.streams) {
            if (st.type == StreamInfo::Type::Video && !st.attachedPicture) {
                s.w = st.width;
                s.h = st.height;
                if (st.fps > 1.0)
                    s.fps = st.fps;
                break;
            }
        }
        if (s.w <= 0 || s.h <= 0) {
            *error = QStringLiteral("Media file %1 has no video stream").arg(sources.size() + 1);
            return std::nullopt;
        }
        sources.append(s);
    }

    drift::TimeUs length = drift::TimeUs(seconds * 1e6);
    for (const Source &s : sources)
        length = std::min(length, s.duration);
    if (length < 2'000'000) {
        *error = QStringLiteral("The media is shorter than 2 seconds");
        return std::nullopt;
    }

    const Source &first = sources.first();
    const Source &second = sources.size() > 1 ? sources.at(1) : first;
    drift::Project project;
    // The project takes the first file's own size and rate: an export at source size, no scaler.
    project.setResolution(first.w & ~1, first.h & ~1);
    project.setFps(std::max(1, int(std::lround(first.fps))));
    project.tracks().clear();

    const auto clipOf = [](const QString &id, const Source &s, drift::TimeUs start, drift::TimeUs dur) {
        drift::Clip clip;
        clip.id = id;
        clip.type = drift::ClipType::Video;
        clip.path = s.path;
        clip.timelineStart = start;
        clip.timelineDuration = dur;
        clip.srcIn = 0;
        clip.srcOut = dur;
        return clip;
    };
    const auto effect = [](const char *catalogId) {
        drift::Effect e;
        e.catalogId = QLatin1String(catalogId);
        return e;
    };

    drift::Track top;
    top.type = drift::TrackType::Video;
    if (scene == QLatin1String("single")) {
        top.clips << clipOf(QStringLiteral("benchA"), first, 0, length);
        project.tracks() << top;
        return project;
    }

    if (sources.size() > 1) {
        const drift::TimeUs half = length / 2;
        drift::Clip a = clipOf(QStringLiteral("benchA"), first, 0, half + 500'000);
        a.effects << effect("adjust.contrast") << effect("stylize.vignette") << effect("rgb_split");
        drift::Clip b = clipOf(QStringLiteral("benchB"), second, half - 500'000, length - (half - 500'000));
        b.effects << effect("adjust.saturation");
        top.clips << a << b;
        drift::Transition fade;
        fade.id = QStringLiteral("benchFade");
        fade.fromClipId = a.id;
        fade.toClipId = b.id;
        fade.kindId = QStringLiteral("crossfade");
        fade.durationUs = 1'000'000;
        top.transitions << fade;
    } else {
        drift::Clip a = clipOf(QStringLiteral("benchA"), first, 0, length);
        a.effects << effect("adjust.contrast") << effect("stylize.vignette") << effect("rgb_split");
        a.transformW.setKeyframe(0, double(project.width()));
        a.transformW.setKeyframe(length, double(project.width()) * 1.25);
        a.transformH.setKeyframe(0, double(project.height()));
        a.transformH.setKeyframe(length, double(project.height()) * 1.25);
        top.clips << a;
    }
    drift::Track bottom;
    bottom.type = drift::TrackType::Video;
    drift::Clip base = clipOf(QStringLiteral("benchBase"), second, 0, length);
    base.effects << effect("adjust.saturation");
    bottom.clips << base;
    project.tracks() << top << bottom;
    return project;
}

std::optional<drift::Project> loadProject(const QString &path, const QString &outDir, QString *error)
{
    QFile probe(path);
    if (!probe.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Could not open the project: %1").arg(probe.errorString());
        return std::nullopt;
    }
    const QByteArray head = probe.peek(64).trimmed();
    if (head.startsWith('{')) {
        const QByteArray data = probe.readAll();
        auto parsed = AppController::parseProjectJson(data, error);
        if (!parsed)
            return std::nullopt;
        return std::move(parsed->project);
    }
    probe.close();

    const auto info = drift::bundle::readManifest(path, error);
    if (!info)
        return std::nullopt;
    QHash<QString, QString> remap;
    if (info->embeddedBytes > 0) {
        // Its own folder under the benchmark's, so nothing of the editor's cache is touched.
        const QString dest = QDir(outDir).filePath(QStringLiteral("project-media"));
        say(QStringLiteral("Unpacking the project's embedded media…"));
        if (!drift::bundle::extract(path, dest, {}, &remap, error))
            return std::nullopt;
    }
    auto parsed = AppController::parseProjectJson(info->document, error);
    if (!parsed)
        return std::nullopt;
    drift::Project project = std::move(parsed->project);
    if (!remap.isEmpty())
        ProjectFileController::remapPaths(project, remap);
    return project;
}

struct RunRecord
{
    QString variant;
    int run = 0; // 0 = warm-up
    QJsonObject data;
};

QJsonObject runChild(const Variant &variant, const QStringList &childArgs, const QString &resultPath,
                     const QString &logPath, int timeoutMs)
{
    QFile::remove(resultPath);
    QProcess child;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // A switch the user has set for their own reasons would turn the baseline into the variant.
    env.remove(QStringLiteral("DRIFT_NO_HWACCEL"));
    env.remove(QStringLiteral("DRIFT_EXPORT_SWSCALE"));
    for (const auto &kv : variant.env)
        env.insert(kv.first, kv.second);
    child.setProcessEnvironment(env);
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.setStandardOutputFile(logPath);
    child.start(QCoreApplication::applicationFilePath(), childArgs);
    QJsonObject out;
    if (!child.waitForStarted(30'000)) {
        out.insert(QStringLiteral("status"), QStringLiteral("BLOCKED"));
        out.insert(QStringLiteral("reason"), QStringLiteral("Could not start the benchmark process"));
        return out;
    }
    if (!child.waitForFinished(timeoutMs)) {
        child.kill();
        child.waitForFinished(10'000);
        out.insert(QStringLiteral("status"), QStringLiteral("FAIL"));
        out.insert(QStringLiteral("reason"), QStringLiteral("Timed out after %1 s").arg(timeoutMs / 1000));
        return out;
    }
    out = readJson(resultPath);
    if (out.isEmpty()) {
        out.insert(QStringLiteral("status"), QStringLiteral("FAIL"));
        out.insert(QStringLiteral("reason"),
                   QStringLiteral("The run ended without a result (exit code %1%2)")
                       .arg(child.exitCode())
                       .arg(child.exitStatus() == QProcess::CrashExit ? QStringLiteral(", crashed") : QString()));
    }
    return out;
}

QString fmt(double v, int decimals = 1)
{
    return QString::number(v, 'f', decimals);
}

// "median / p95 / min-max" of one metric across runs.
QString spread(const std::vector<double> &v, int decimals = 1)
{
    if (v.empty())
        return QStringLiteral("—");
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    return QStringLiteral("%1 / %2 (%3–%4)")
        .arg(fmt(bench::median(v), decimals), fmt(bench::percentile(v, 95), decimals), fmt(*lo, decimals),
             fmt(*hi, decimals));
}

QJsonObject spreadJson(const std::vector<double> &v)
{
    if (v.empty())
        return {};
    return QJsonObject{{QStringLiteral("n"), int(v.size())},
                       {QStringLiteral("median"), bench::median(v)},
                       {QStringLiteral("p95"), bench::percentile(v, 95)},
                       {QStringLiteral("min"), *std::min_element(v.begin(), v.end())},
                       {QStringLiteral("max"), *std::max_element(v.begin(), v.end())}};
}

QString summarize(const QList<RunRecord> &records, const QJsonObject &header, const QString &outDir)
{
    const auto measured = [&](const QString &variant) {
        QList<QJsonObject> out;
        for (const RunRecord &r : records)
            if (r.variant == variant && r.run > 0 && r.data.value(QStringLiteral("status")).toString() == QLatin1String("PASS"))
                out.append(r.data);
        return out;
    };
    const auto byRun = [&](const QString &variant) {
        QHash<int, QJsonObject> out;
        for (const RunRecord &r : records)
            if (r.variant == variant && r.run > 0 && r.data.value(QStringLiteral("status")).toString() == QLatin1String("PASS"))
                out.insert(r.run, r.data);
        return out;
    };
    const auto statusLine = [&](const QString &variant) {
        int pass = 0, other = 0;
        QStringList reasons;
        for (const RunRecord &r : records) {
            if (r.variant != variant || r.run == 0)
                continue;
            if (r.data.value(QStringLiteral("status")).toString() == QLatin1String("PASS"))
                ++pass;
            else {
                ++other;
                const QString why = r.data.value(QStringLiteral("status")).toString() + QStringLiteral(": ")
                    + r.data.value(QStringLiteral("reason")).toString();
                if (!reasons.contains(why))
                    reasons << why;
            }
        }
        return QStringLiteral("%1 PASS, %2 not%3").arg(pass).arg(other).arg(reasons.isEmpty() ? QString() : QStringLiteral(" (") + reasons.join(QStringLiteral("; ")) + QStringLiteral(")"));
    };

    QString md;
    QJsonObject json{{QStringLiteral("schema"), QLatin1String(kSummarySchema)}, {QStringLiteral("header"), header}};
    const QJsonObject machine = header.value(QStringLiteral("machine")).toObject();
    md += QStringLiteral("# Dafin CUDA benchmark\n\n");
    md += QStringLiteral("- Build: %1 %2\n").arg(header.value(QStringLiteral("build")).toObject().value(QStringLiteral("version")).toString(),
                                                 header.value(QStringLiteral("build")).toObject().value(QStringLiteral("label")).toString());
    md += QStringLiteral("- Renderer: %1 (%2), OpenGL %3\n").arg(machine.value(QStringLiteral("gl_renderer")).toString(),
                                                                  machine.value(QStringLiteral("gl_vendor")).toString(),
                                                                  machine.value(QStringLiteral("gl_version")).toString());
    md += QStringLiteral("- Machine: `%1`\n").arg(machine.value(QStringLiteral("signature")).toString());
    md += QStringLiteral("- Project: %1, %2×%3 @ %4 fps, %5 s, SHA-256 `%6`\n")
              .arg(header.value(QStringLiteral("source")).toString())
              .arg(header.value(QStringLiteral("width")).toInt())
              .arg(header.value(QStringLiteral("height")).toInt())
              .arg(header.value(QStringLiteral("fps")).toInt())
              .arg(fmt(header.value(QStringLiteral("seconds")).toDouble()))
              .arg(header.value(QStringLiteral("project_sha256")).toString().left(16));
    for (const QJsonValue &m : header.value(QStringLiteral("media")).toArray())
        md += QStringLiteral("  - media: %1 MB, SHA-256 `%2`\n")
                  .arg(fmt(m.toObject().value(QStringLiteral("bytes")).toDouble() / 1e6))
                  .arg(m.toObject().value(QStringLiteral("sha256")).toString().left(16));
    md += QStringLiteral("- Runs: %1 paired per variant, alternating A B / B A, one warm-up of each left out. "
                         "Figures are **median / p95 (min–max) across runs**; with 5 runs p95 is the slowest run.\n\n")
              .arg(header.value(QStringLiteral("runs")).toInt());

    // Export.
    const QList<QJsonObject> gpu = measured(QStringLiteral("export_gpu"));
    const QList<QJsonObject> cpu = measured(QStringLiteral("export_cpu"));
    std::vector<bench::ExportRun> exportRuns;
    if (!gpu.isEmpty() || !cpu.isEmpty()) {
        md += QStringLiteral("## Export (codec `%1`)\n\n").arg(header.value(QStringLiteral("codec")).toString());
        md += QStringLiteral("| | export_gpu (default) | export_cpu (`DRIFT_EXPORT_SWSCALE=1`) |\n|---|---|---|\n");
        const auto col = [](const QList<QJsonObject> &runs, const std::function<double(const QJsonObject &)> &f) {
            std::vector<double> v;
            for (const QJsonObject &r : runs)
                v.push_back(f(r));
            return v;
        };
        const auto wall = [](const QJsonObject &r) { return r.value(QStringLiteral("wall_s")).toDouble(); };
        const auto fps = [](const QJsonObject &r) { return r.value(QStringLiteral("fps_achieved")).toDouble(); };
        const auto share = [](const char *phase) {
            return [phase](const QJsonObject &r) {
                const double w = r.value(QStringLiteral("wall_s")).toDouble() * 1000.0;
                return w > 0 ? 100.0 * phaseMs(r, phase) / w : 0.0;
            };
        };
        md += QStringLiteral("| Wall time, s | %1 | %2 |\n").arg(spread(col(gpu, wall), 2), spread(col(cpu, wall), 2));
        md += QStringLiteral("| Frames/s | %1 | %2 |\n").arg(spread(col(gpu, fps)), spread(col(cpu, fps)));
        md += QStringLiteral("| Compose + GL submit, % of wall | %1 | — |\n").arg(spread(col(gpu, share("export_compose"))));
        md += QStringLiteral("| Waiting for GL readback fence, % | %1 | — |\n").arg(spread(col(gpu, share("export_fence_wait"))));
        md += QStringLiteral("| PBO map + copy to host NV12, % | %1 | — |\n").arg(spread(col(gpu, share("export_host_copy"))));
        md += QStringLiteral("| Encode + mux + audio, % | %1 | %2 |\n").arg(spread(col(gpu, share("export_send"))), spread(col(cpu, share("export_send"))));
        md += QStringLiteral("| CPU compose + swscale, % | — | %1 |\n").arg(spread(col(cpu, share("export_cpu_compose"))));
        md += QStringLiteral("| Status | %1 | %2 |\n\n").arg(statusLine(QStringLiteral("export_gpu")), statusLine(QStringLiteral("export_cpu")));

        // Paired: the same run index, one right after the other.
        const auto gpuRuns = byRun(QStringLiteral("export_gpu"));
        const auto cpuRuns = byRun(QStringLiteral("export_cpu"));
        std::vector<double> deltas;
        for (auto it = gpuRuns.constBegin(); it != gpuRuns.constEnd(); ++it)
            if (cpuRuns.contains(it.key()) && cpuRuns.value(it.key()).value(QStringLiteral("wall_s")).toDouble() > 0)
                deltas.push_back(100.0 * (1.0 - wall(it.value()) / wall(cpuRuns.value(it.key()))));
        if (!deltas.empty())
            md += QStringLiteral("Paired: the GPU route is **%1 %** faster than the CPU route (median; p95 %2 %, %3 pairs).\n\n")
                      .arg(fmt(bench::median(deltas)), fmt(bench::percentile(deltas, 95)))
                      .arg(deltas.size());

        if (!gpu.isEmpty()) {
            const QJsonObject r0 = gpu.first().value(QStringLiteral("routes")).toObject();
            md += QStringLiteral("Route proof (first measured export_gpu run): plan `%1`; encodes direct_cuda %2, gl_pbo_host_nv12 %3, cpu_swscale %4; "
                                 "imports cuda-interop %5, hw-download %6, software-pbo %7, d3d11 %8, vaapi %9.\n\n")
                      .arg(r0.value(QStringLiteral("plan")).toString())
                      .arg(routeCount(r0, "encodes", "direct_cuda"))
                      .arg(routeCount(r0, "encodes", "gl_pbo_host_nv12"))
                      .arg(routeCount(r0, "encodes", "cpu_swscale"))
                      .arg(routeCount(r0, "imports", "cuda-interop"))
                      .arg(routeCount(r0, "imports", "hw-download"))
                      .arg(routeCount(r0, "imports", "software-pbo"))
                      .arg(routeCount(r0, "imports", "d3d11-interop"))
                      .arg(routeCount(r0, "imports", "vaapi-dmabuf"));
        }
        json.insert(QStringLiteral("export_gpu_wall_s"), spreadJson(col(gpu, wall)));
        json.insert(QStringLiteral("export_cpu_wall_s"), spreadJson(col(cpu, wall)));
        json.insert(QStringLiteral("export_paired_gain_pct"), spreadJson(deltas));

        for (const QJsonObject &r : gpu) {
            bench::ExportRun e;
            e.wallS = r.value(QStringLiteral("wall_s")).toDouble();
            e.nvenc = r.value(QStringLiteral("codec")).toString().contains(QLatin1String("nvenc"));
            e.frames = qint64(r.value(QStringLiteral("frames")).toDouble());
            const QJsonObject ro = r.value(QStringLiteral("routes")).toObject();
            e.hostNv12 = qint64(routeCount(ro, "encodes", "gl_pbo_host_nv12"));
            e.directCuda = qint64(routeCount(ro, "encodes", "direct_cuda"));
            e.hostCopyS = phaseMs(r, "export_host_copy") / 1000.0;
            e.sendS = phaseMs(r, "export_send") / 1000.0;
            exportRuns.push_back(e);
        }
    }

    // Preview and scrub.
    const QList<QJsonObject> hw = measured(QStringLiteral("preview_hw"));
    const QList<QJsonObject> sw = measured(QStringLiteral("preview_sw"));
    std::vector<bench::PreviewRun> previewRuns;
    if (!hw.isEmpty() || !sw.isEmpty()) {
        const auto col = [](const QList<QJsonObject> &runs, const char *key, double p) {
            std::vector<double> v;
            for (const QJsonObject &r : runs) {
                const std::vector<double> samples = fromArray(r.value(QLatin1String(key)));
                v.push_back(p < 0 ? (samples.empty() ? 0.0 : std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size())
                                  : bench::percentile(samples, p));
            }
            return v;
        };
        const auto scalar = [](const QList<QJsonObject> &runs, const char *key) {
            std::vector<double> v;
            for (const QJsonObject &r : runs)
                v.push_back(r.value(QLatin1String(key)).toDouble());
            return v;
        };
        const double budget = (hw.isEmpty() ? sw : hw).first().value(QStringLiteral("budget_ms")).toDouble();
        md += QStringLiteral("## Preview playback (scale %1, frame budget %2 ms)\n\n")
                  .arg(fmt((hw.isEmpty() ? sw : hw).first().value(QStringLiteral("preview_scale")).toDouble(), 2), fmt(budget));
        md += QStringLiteral("| | preview_hw (default decode) | preview_sw (`DRIFT_NO_HWACCEL=1`) |\n|---|---|---|\n");
        md += QStringLiteral("| Frame time p50, ms | %1 | %2 |\n").arg(spread(col(hw, "frame_ms", 50)), spread(col(sw, "frame_ms", 50)));
        md += QStringLiteral("| Frame time p95, ms | %1 | %2 |\n").arg(spread(col(hw, "frame_ms", 95)), spread(col(sw, "frame_ms", 95)));
        md += QStringLiteral("| Frames/s achieved | %1 | %2 |\n").arg(spread(scalar(hw, "fps_achieved")), spread(scalar(sw, "fps_achieved")));
        md += QStringLiteral("| CUDA sync per frame (mean), ms | %1 | — |\n").arg(spread(col(hw, "frame_sync_ms", -1), 2));
        md += QStringLiteral("| Decoder wait per frame (mean), ms | %1 | %2 |\n").arg(spread(col(hw, "frame_decode_ms", -1), 2), spread(col(sw, "frame_decode_ms", -1), 2));
        md += QStringLiteral("| Status | %1 | %2 |\n\n").arg(statusLine(QStringLiteral("preview_hw")), statusLine(QStringLiteral("preview_sw")));

        md += QStringLiteral("## Scrubbing (%1 seeks, same positions every run)\n\n").arg(header.value(QStringLiteral("seeks")).toInt());
        md += QStringLiteral("| | preview_hw | preview_sw |\n|---|---|---|\n");
        md += QStringLiteral("| Seek p50, ms | %1 | %2 |\n").arg(spread(col(hw, "seek_ms", 50)), spread(col(sw, "seek_ms", 50)));
        md += QStringLiteral("| Seek p95, ms | %1 | %2 |\n").arg(spread(col(hw, "seek_ms", 95)), spread(col(sw, "seek_ms", 95)));
        md += QStringLiteral("| Decoder wait per seek (mean), ms | %1 | %2 |\n\n").arg(spread(col(hw, "seek_decode_ms", -1)), spread(col(sw, "seek_decode_ms", -1)));

        const auto hwRuns = byRun(QStringLiteral("preview_hw"));
        const auto swRuns = byRun(QStringLiteral("preview_sw"));
        std::vector<double> p95Delta, seekDelta;
        for (auto it = hwRuns.constBegin(); it != hwRuns.constEnd(); ++it) {
            if (!swRuns.contains(it.key()))
                continue;
            const QJsonObject &s = swRuns.value(it.key());
            p95Delta.push_back(bench::percentile(fromArray(s.value(QStringLiteral("frame_ms"))), 95)
                               - bench::percentile(fromArray(it.value().value(QStringLiteral("frame_ms"))), 95));
            seekDelta.push_back(bench::percentile(fromArray(s.value(QStringLiteral("seek_ms"))), 95)
                                - bench::percentile(fromArray(it.value().value(QStringLiteral("seek_ms"))), 95));
        }
        if (!p95Delta.empty())
            md += QStringLiteral("Paired (software minus hardware decode): playback p95 **%1 ms**, scrub p95 **%2 ms** (median over %3 pairs; positive = hardware is faster).\n\n")
                      .arg(fmt(bench::median(p95Delta)), fmt(bench::median(seekDelta)))
                      .arg(p95Delta.size());
        if (!hw.isEmpty()) {
            const QJsonObject r0 = hw.first().value(QStringLiteral("routes")).toObject();
            md += QStringLiteral("Route proof (first measured preview_hw run, playback): cuda-interop %1, hw-download %2, software-pbo %3, d3d11 %4, vaapi %5; CUDA import refused %6.\n\n")
                      .arg(routeCount(r0, "imports", "cuda-interop"))
                      .arg(routeCount(r0, "imports", "hw-download"))
                      .arg(routeCount(r0, "imports", "software-pbo"))
                      .arg(routeCount(r0, "imports", "d3d11-interop"))
                      .arg(routeCount(r0, "imports", "vaapi-dmabuf"))
                      .arg(routeCount(r0, "fallbacks", "cuda-interop-refused"));
        }
        json.insert(QStringLiteral("preview_hw_p95_ms"), spreadJson(col(hw, "frame_ms", 95)));
        json.insert(QStringLiteral("preview_sw_p95_ms"), spreadJson(col(sw, "frame_ms", 95)));
        json.insert(QStringLiteral("scrub_hw_p95_ms"), spreadJson(col(hw, "seek_ms", 95)));
        json.insert(QStringLiteral("scrub_sw_p95_ms"), spreadJson(col(sw, "seek_ms", 95)));

        for (const QJsonObject &r : hw) {
            bench::PreviewRun p;
            p.frameMs = fromArray(r.value(QStringLiteral("frame_ms")));
            p.frameSyncMs = fromArray(r.value(QStringLiteral("frame_sync_ms")));
            p.seekMs = fromArray(r.value(QStringLiteral("seek_ms")));
            p.seekDecodeMs = fromArray(r.value(QStringLiteral("seek_decode_ms")));
            p.budgetMs = r.value(QStringLiteral("budget_ms")).toDouble();
            const QJsonObject play = r.value(QStringLiteral("routes")).toObject();
            const QJsonObject seek = r.value(QStringLiteral("scrub_routes")).toObject();
            p.cudaImports = qint64(routeCount(play, "imports", "cuda-interop"));
            p.nvdecFrames = qint64(routeCount(seek, "imports", "cuda-interop") + routeCount(seek, "fallbacks", "cuda-interop-refused")
                                   + p.cudaImports + routeCount(play, "fallbacks", "cuda-interop-refused"));
            previewRuns.push_back(p);
        }
    }

    // The verdicts.
    const bench::Thresholds t;
    const bench::Decision c4 = bench::decideExport(exportRuns, t);
    const bench::Decision c5 = bench::decidePreviewSync(previewRuns, t);
    const bench::Decision c6 = bench::decideScrub(previewRuns, t);
    md += QStringLiteral("## Verdict\n\n");
    md += QStringLiteral("A ceiling can rule a change out, never prove a gain: GO means \"worth an opt-in experiment\", whose own A/B decides.\n\n");
    md += QStringLiteral("| Change | Verdict | Why |\n|---|---|---|\n");
    md += QStringLiteral("| C4 direct CUDA→NVENC export | **%1** | %2 |\n").arg(QLatin1String(bench::id(c4.verdict)), QString::fromStdString(c4.reason));
    md += QStringLiteral("| C5 preview CUDA sync | **%1** | %2 |\n").arg(QLatin1String(bench::id(c5.verdict)), QString::fromStdString(c5.reason));
    md += QStringLiteral("| C6 NVDEC pool for scrubbing | **%1** | %2 |\n\n").arg(QLatin1String(bench::id(c6.verdict)), QString::fromStdString(c6.reason));
    md += QStringLiteral("Bars: export at least %1 % shorter; preview p95 over the frame budget with the CUDA sync at least %2 % of frame time; "
                         "scrub p95 over %3 ms with the decoder at least %4 % of it.\n")
              .arg(fmt(t.exportMinFraction * 100, 0), fmt(t.previewMinSyncShare * 100, 0), fmt(t.scrubLaggyP95Ms, 0),
                   fmt(t.scrubMinDecodeShare * 100, 0));
    const auto verdictJson = [](const bench::Decision &d) {
        return QJsonObject{{QStringLiteral("verdict"), QLatin1String(bench::id(d.verdict))},
                           {QStringLiteral("value"), d.value},
                           {QStringLiteral("reason"), QString::fromStdString(d.reason)}};
    };
    json.insert(QStringLiteral("c4"), verdictJson(c4));
    json.insert(QStringLiteral("c5"), verdictJson(c5));
    json.insert(QStringLiteral("c6"), verdictJson(c6));

    writeJson(QDir(outDir).filePath(QStringLiteral("summary.json")), json);
    QFile file(QDir(outDir).filePath(QStringLiteral("summary.md")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(md.toUtf8());
    return md;
}

void printUsage()
{
    say(QStringLiteral(
        "Usage:\n"
        "  dafin --bench --media <video>[,<video2>] [options]\n"
        "  dafin --bench --project <file.drift|file.json> [options]\n\n"
        "Options:\n"
        "  --runs N            paired runs per variant (default 5)\n"
        "  --out DIR           where results go (default Documents/Dafin Benchmarks/<time>)\n"
        "  --seconds S         export / scene length (default 20)\n"
        "  --frames N          playback frames per preview run (default 300)\n"
        "  --seeks N           scrub seeks per preview run (default 60)\n"
        "  --preview-scale X   preview resolution, 1 = Full (default 1)\n"
        "  --codec ID          export encoder (default: h264_nvenc if available, else Dafin's default)\n"
        "  --scene layered|single   built-in scene for --media (default layered)\n"
        "  --only export|preview    run one half\n\n"
        "See docs/cuda-benchmark.md."));
}

} // namespace

int runBenchChild(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    QCoreApplication::setApplicationName(QStringLiteral("Dafin"));
    QCoreApplication::setOrganizationName(QStringLiteral("Dafin"));
    // QApplication as in the export worker: the compositor needs a GL context on an offscreen
    // surface, and no window is ever opened.
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    const QString mode = argValue(args, QStringLiteral("--bench-run"));
    const QString projectPath = argValue(args, QStringLiteral("--project-json"));
    const QString resultPath = argValue(args, QStringLiteral("--result"));

    QJsonObject result{{QStringLiteral("schema"), QLatin1String(kRunSchema)},
                       {QStringLiteral("mode"), mode},
                       {QStringLiteral("status"), QStringLiteral("FAIL")},
                       {QStringLiteral("build"), buildInfo()}};
    const auto finish = [&](int code) {
        if (!resultPath.isEmpty())
            writeJson(resultPath, result);
        return code;
    };

    QString error;
    const drift::Project project = drift::Project::fromJson(readJson(projectPath), &error);
    if (!error.isEmpty() || project.tracks().isEmpty()) {
        result.insert(QStringLiteral("status"), QStringLiteral("BLOCKED"));
        result.insert(QStringLiteral("reason"), QStringLiteral("Could not read the benchmark project"));
        return finish(2);
    }

    // What the editor loads at startup that rendering reads (as the export worker does).
    reloadFontCatalog();
    reloadUserFonts();
    reloadEmojiCatalog();
    reloadEffectCatalog();
    drift::ReverseProxyCache::instance().load();

    result.insert(QStringLiteral("machine"), machineInfo());
    result.insert(QStringLiteral("env"),
                  QJsonObject{{QStringLiteral("DRIFT_NO_HWACCEL"), qEnvironmentVariableIsSet("DRIFT_NO_HWACCEL")},
                              {QStringLiteral("DRIFT_EXPORT_SWSCALE"), qEnvironmentVariableIsSet("DRIFT_EXPORT_SWSCALE")}});

    bench::reset();
    routes::reset();
    bench::setEnabled(true);
    bool ok = false;
    if (mode == QLatin1String("export")) {
        ok = runExport(project, args, &result);
        result.insert(QStringLiteral("routes"), routesJson(routes::snapshot()));
    } else if (mode == QLatin1String("preview")) {
        ok = runPreview(project, args, &result);
    } else {
        result.insert(QStringLiteral("status"), QStringLiteral("BLOCKED"));
        result.insert(QStringLiteral("reason"), QStringLiteral("Unknown benchmark mode"));
    }
    bench::setEnabled(false);
    result.insert(QStringLiteral("phases"), phasesJson(bench::snapshot()));
    ClipReaderPool::instance().releaseAll();
    return finish(ok ? 0 : 1);
}

int runBenchTool(int argc, char *argv[])
{
    attachConsole();
    QCoreApplication::setApplicationName(QStringLiteral("Dafin"));
    QCoreApplication::setOrganizationName(QStringLiteral("Dafin"));
    QApplication app(argc, argv);
    const QStringList args = app.arguments();

    const QString mediaArg = argValue(args, QStringLiteral("--media"));
    const QString projectArg = argValue(args, QStringLiteral("--project"));
    if (args.contains(QStringLiteral("--help")) || (mediaArg.isEmpty() == projectArg.isEmpty())) {
        printUsage();
        return mediaArg.isEmpty() == projectArg.isEmpty() && !args.contains(QStringLiteral("--help")) ? 2 : 0;
    }
    const int runs = std::clamp(argValue(args, QStringLiteral("--runs"), QStringLiteral("5")).toInt(), 1, 50);
    const double seconds = std::max(2.0, argValue(args, QStringLiteral("--seconds"), QStringLiteral("20")).toDouble());
    const QString frames = argValue(args, QStringLiteral("--frames"), QStringLiteral("300"));
    const QString seeks = argValue(args, QStringLiteral("--seeks"), QStringLiteral("60"));
    const QString scale = argValue(args, QStringLiteral("--preview-scale"), QStringLiteral("1"));
    const QString scene = argValue(args, QStringLiteral("--scene"), QStringLiteral("layered"));
    const QString only = argValue(args, QStringLiteral("--only"));

    QString outDir = argValue(args, QStringLiteral("--out"));
    if (outDir.isEmpty())
        outDir = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                     .filePath(QStringLiteral("Dafin Benchmarks/%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    if (!QDir().mkpath(outDir) || !QDir().mkpath(QDir(outDir).filePath(QStringLiteral("runs")))) {
        say(QStringLiteral("Could not create the output folder %1").arg(QDir::toNativeSeparators(outDir)));
        return 2;
    }

    reloadFontCatalog();
    reloadEffectCatalog();

    // The project every run reads, written once so all of them see exactly the same document.
    QString error;
    std::optional<drift::Project> project;
    QJsonArray media;
    if (!mediaArg.isEmpty()) {
        const QStringList paths = mediaArg.split(QLatin1Char(','), Qt::SkipEmptyParts);
        project = sceneFromMedia(paths, scene, seconds, &error);
        for (const QString &path : paths)
            media.append(QJsonObject{{QStringLiteral("bytes"), double(QFileInfo(path).size())},
                                     {QStringLiteral("sha256"), sha256OfFile(path)}});
    } else {
        project = loadProject(projectArg, outDir, &error);
    }
    if (!project) {
        say(error.isEmpty() ? QStringLiteral("Could not build the benchmark project") : error);
        return 2;
    }
    const QString projectJson = QDir(outDir).filePath(QStringLiteral("bench-project.json"));
    if (!writeJson(projectJson, project->toJson())) {
        say(QStringLiteral("Could not write %1").arg(QDir::toNativeSeparators(projectJson)));
        return 2;
    }
    const QString projectSha =
        QString::fromLatin1(QCryptographicHash::hash(project->toCompactJson(), QCryptographicHash::Sha256).toHex());

    QString codec = argValue(args, QStringLiteral("--codec"));
    if (codec.isEmpty()) {
        codec = Exporter::videoCodecById(QStringLiteral("h264_nvenc")).value(QStringLiteral("available")).toBool()
            ? QStringLiteral("h264_nvenc")
            : Exporter::defaultSettings().videoCodecId;
    }

    QJsonObject header{
        {QStringLiteral("schema"), QLatin1String(kSummarySchema)},
        {QStringLiteral("build"), buildInfo()},
        {QStringLiteral("machine"), machineInfo()},
        {QStringLiteral("source"), mediaArg.isEmpty() ? QStringLiteral("project file") : QStringLiteral("built-in %1 scene").arg(scene)},
        {QStringLiteral("media"), media},
        {QStringLiteral("project_sha256"), projectSha},
        {QStringLiteral("width"), project->width()},
        {QStringLiteral("height"), project->height()},
        {QStringLiteral("fps"), project->fps()},
        {QStringLiteral("seconds"), std::min(seconds, double(project->durationUs()) / 1e6)},
        {QStringLiteral("codec"), codec},
        {QStringLiteral("runs"), runs},
        {QStringLiteral("seeks"), seeks.toInt()},
        {QStringLiteral("started"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
    };
    writeJson(QDir(outDir).filePath(QStringLiteral("header.json")), header);

    QList<QPair<Variant, Variant>> pairs;
    if (only.isEmpty() || only == QLatin1String("export"))
        pairs.append({Variant{QStringLiteral("export_gpu"), QStringLiteral("export"), {}},
                      Variant{QStringLiteral("export_cpu"), QStringLiteral("export"),
                              {{QStringLiteral("DRIFT_EXPORT_SWSCALE"), QStringLiteral("1")}}}});
    if (only.isEmpty() || only == QLatin1String("preview"))
        pairs.append({Variant{QStringLiteral("preview_hw"), QStringLiteral("preview"), {}},
                      Variant{QStringLiteral("preview_sw"), QStringLiteral("preview"),
                              {{QStringLiteral("DRIFT_NO_HWACCEL"), QStringLiteral("1")}}}});

    // Schedule: per pair, a warm-up of each, then A B, B A, A B…
    QList<QPair<Variant, int>> schedule;
    for (const auto &pair : pairs) {
        schedule.append({pair.first, 0});
        schedule.append({pair.second, 0});
        for (int k = 1; k <= runs; ++k) {
            const bool aFirst = k % 2 == 1;
            schedule.append({aFirst ? pair.first : pair.second, k});
            schedule.append({aFirst ? pair.second : pair.first, k});
        }
    }

    say(QStringLiteral("Dafin benchmark: %1 runs, results in %2").arg(schedule.size()).arg(QDir::toNativeSeparators(outDir)));
    say(QStringLiteral("Keep the machine idle and on mains power; do not use Dafin meanwhile."));

    QFile jsonl(QDir(outDir).filePath(QStringLiteral("results.jsonl")));
    if (!jsonl.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        say(QStringLiteral("Could not write results.jsonl"));
        return 2;
    }
    QList<RunRecord> records;
    int index = 0;
    for (const auto &[variant, run] : schedule) {
        ++index;
        const QString tag = QStringLiteral("%1-%2-%3").arg(index, 2, 10, QLatin1Char('0')).arg(variant.name).arg(run == 0 ? QStringLiteral("warmup") : QString::number(run));
        const QString resultPath = QDir(outDir).filePath(QStringLiteral("runs/%1.json").arg(tag));
        const QString logPath = QDir(outDir).filePath(QStringLiteral("runs/%1.log").arg(tag));
        QStringList childArgs{QStringLiteral("--bench-run"), variant.mode,
                              QStringLiteral("--project-json"), projectJson,
                              QStringLiteral("--result"), resultPath};
        if (variant.mode == QLatin1String("export")) {
            // The newest file of each variant is kept, to look at or compare.
            childArgs << QStringLiteral("--codec") << codec << QStringLiteral("--seconds") << QString::number(seconds)
                      << QStringLiteral("--export-file")
                      << QDir(outDir).filePath(QStringLiteral("%1.mp4").arg(variant.name));
        } else {
            childArgs << QStringLiteral("--frames") << frames << QStringLiteral("--seeks") << seeks
                      << QStringLiteral("--preview-scale") << scale;
        }
        std::fprintf(stdout, "[%d/%d] %s %s… ", index, int(schedule.size()), qUtf8Printable(variant.name),
                     run == 0 ? "warm-up" : qUtf8Printable(QStringLiteral("run %1").arg(run)));
        std::fflush(stdout);
        QJsonObject data = runChild(variant, childArgs, resultPath, logPath, variant.mode == QLatin1String("export") ? 3'600'000 : 900'000);
        data.insert(QStringLiteral("variant"), variant.name);
        data.insert(QStringLiteral("run"), run);
        data.insert(QStringLiteral("order"), index);
        jsonl.write(QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n');
        jsonl.flush();
        const QString status = data.value(QStringLiteral("status")).toString();
        if (variant.mode == QLatin1String("export"))
            say(QStringLiteral("%1 s %2").arg(fmt(data.value(QStringLiteral("wall_s")).toDouble(), 2), status));
        else
            say(QStringLiteral("p95 %1 ms %2").arg(fmt(bench::percentile(fromArray(data.value(QStringLiteral("frame_ms"))), 95)), status));
        if (status != QLatin1String("PASS") && !data.value(QStringLiteral("reason")).toString().isEmpty())
            say(QStringLiteral("    %1").arg(data.value(QStringLiteral("reason")).toString()));
        records.append(RunRecord{variant.name, run, data});
    }
    jsonl.close();

    const QString md = summarize(records, header, outDir);
    say(QString());
    say(md);
    say(QStringLiteral("Written: %1").arg(QDir::toNativeSeparators(QDir(outDir).filePath(QStringLiteral("summary.md")))));
    return 0;
}

} // namespace drift::worker
