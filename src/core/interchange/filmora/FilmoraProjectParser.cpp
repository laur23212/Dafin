#include "core/interchange/filmora/FilmoraProjectParser.h"

#include "core/interchange/filmora/WfpArchive.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace drift::interchange::filmora {

namespace {

constexpr double kTicksPerUs = 10.0; // 100 ns units

QString tr(const char *text, const char *disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate("FilmoraImport", text, disambiguation, n);
}

// Filmora's times are 100 ns ticks written as JSON numbers; a damaged or hostile file can hold
// anything, and casting a NaN or a huge double to an integer is undefined. False for such a value
// (an absent one reads as 0, as it always has).
bool ticksToUs(const QJsonValue &v, int64_t *us)
{
    const double ticks = v.toDouble();
    // 1e16 ticks is a hundred days short of 32 years: far beyond any edit.
    if (!std::isfinite(ticks) || std::fabs(ticks) > 1e16)
        return false;
    *us = int64_t(ticks / kTicksPerUs);
    return true;
}

bool isStillSuffix(const QString &path)
{
    static const QStringList stills{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                    QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp"),
                                    QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("heic"),
                                    QStringLiteral("svg")};
    return stills.contains(QFileInfo(path).suffix().toLower());
}

// A JSON member of the archive. False only when the zip itself is not to be trusted (`fatal`
// says why); a member that is not there, or whose JSON does not read, comes back as an empty
// object, which is how the parser has always treated it.
bool readJson(WfpArchive &archive, const QString &path, QJsonObject *out, WfpStatus *fatal)
{
    *out = {};
    const WfpArchive::Member *member = archive.find(path);
    if (!member)
        return true;
    WfpStatus status;
    if (archive.readJsonObject(*member, out, &status))
        return true;
    *out = {};
    if (!status.isIntegrityFailure())
        return true;
    *fatal = status;
    return false;
}

// A speed curve with every key at the same value is a constant speed; Filmora writes one for
// every clip. Returns 0 when the curve really ramps.
double constantSpeedOf(const QJsonValue &speedParam)
{
    QJsonObject curve;
    if (speedParam.isObject())
        curve = speedParam.toObject();
    else if (speedParam.isString())
        curve = QJsonDocument::fromJson(speedParam.toString().toUtf8()).object();
    const QJsonArray keys = curve.value(QStringLiteral("keyframeSets")).toArray();
    double first = -1;
    for (const QJsonValue &k : keys) {
        const double v = k.toObject().value(QStringLiteral("_value")).toDouble(-1);
        if (v <= 0)
            continue;
        if (first < 0)
            first = v;
        else if (qAbs(v - first) > 1e-4)
            return 0;
    }
    return first > 0 ? first : 1.0;
}

// Range of the values on a speed curve, for the report: how many keys it has and the slowest and
// fastest of them.
struct CurveSummary
{
    int keys = 0;
    double min = 0;
    double max = 0;
};

CurveSummary summarizeCurve(const QJsonValue &speedParam)
{
    QJsonObject curve;
    if (speedParam.isObject())
        curve = speedParam.toObject();
    else if (speedParam.isString())
        curve = QJsonDocument::fromJson(speedParam.toString().toUtf8()).object();
    CurveSummary out;
    for (const QJsonValue &k : curve.value(QStringLiteral("keyframeSets")).toArray()) {
        const double v = k.toObject().value(QStringLiteral("_value")).toDouble(-1);
        if (v <= 0)
            continue;
        out.min = out.keys == 0 ? v : std::min(out.min, v);
        out.max = out.keys == 0 ? v : std::max(out.max, v);
        ++out.keys;
    }
    return out;
}

// A speed ramp read from Filmora's speedParam keys, accepted only when it is proven.
//
// What is known about the layout is only that keys carry `_value` (the speed). Nothing published
// says what `_time` counts, how the keys are joined, or what unit the speed is in, so none of that
// is trusted: a ramp is mapped only when ALL of these hold, and otherwise it stays what it was, one
// constant speed reported as approximated.
//   1. every key has a finite `_time` and a `_value` inside Dafin's ramp range, the times strictly
//      increase, and the first is 0;
//   2. the times are either 0..1 or ticks that end at the clip's source length (so they say where in
//      the source the key is; times that end at the length on the timeline are not mapped);
//   3. read as straight lines between the keys over the source, the ramp makes the clip last as long
//      as the project says it lasts (0.2 %, at least 20 ms). That is the proof: the file itself
//      agrees with the reading. It is not proof of Filmora's easing between the keys.
struct RampProof
{
    bool ok = false;
    QList<SpeedPoint> points;
    QString why;           // when not ok: the first rule that failed
    int64_t errorUs = 0;   // when ok: how far the predicted length is from the recorded one
};

RampProof proveRamp(const QJsonValue &speedParam, int64_t srcSpanUs, int64_t timelineDurationUs)
{
    RampProof out;
    QJsonObject curveObject;
    if (speedParam.isObject())
        curveObject = speedParam.toObject();
    else if (speedParam.isString())
        curveObject = QJsonDocument::fromJson(speedParam.toString().toUtf8()).object();
    const QJsonArray keys = curveObject.value(QStringLiteral("keyframeSets")).toArray();
    if (keys.size() < 2) {
        out.why = tr("The curve has fewer than two keys.");
        return out;
    }
    if (srcSpanUs <= 0 || timelineDurationUs <= 0) {
        out.why = tr("The clip has no usable length.");
        return out;
    }
    struct Key
    {
        double t;
        double v;
    };
    QList<Key> ks;
    for (const QJsonValue &kv : keys) {
        const QJsonObject k = kv.toObject();
        if (!k.contains(QStringLiteral("_time"))) {
            out.why = tr("The keys do not say where on the clip they are.");
            return out;
        }
        const double t = k.value(QStringLiteral("_time")).toDouble(std::nan(""));
        const double v = k.value(QStringLiteral("_value")).toDouble(std::nan(""));
        if (!std::isfinite(t) || !std::isfinite(v) || t < 0) {
            out.why = tr("A key's position or speed is not a usable number.");
            return out;
        }
        if (v < kMinCurveSpeed || v > kMaxCurveSpeed) {
            out.why = tr("A key's speed (%1×) is outside the %2× to %3× Dafin's ramps cover.")
                          .arg(QString::number(v, 'g', 4), QString::number(kMinCurveSpeed, 'g', 3),
                               QString::number(kMaxCurveSpeed, 'g', 3));
            return out;
        }
        ks.append({t, v});
    }
    for (int i = 1; i < ks.size(); ++i) {
        if (!(ks.at(i).t > ks.at(i - 1).t)) {
            out.why = tr("The keys are not in order along the clip.");
            return out;
        }
    }
    if (ks.first().t != 0.0) {
        out.why = tr("The first key is not at the start of the clip.");
        return out;
    }
    const double last = ks.last().t;
    const double sourceTicks = double(srcSpanUs) * 10.0;
    if (last <= 1.0 + 1e-9) {
        if (std::abs(last - 1.0) > 1e-6) {
            out.why = tr("The last key is not at the end of the clip.");
            return out;
        }
    } else if (std::abs(last - sourceTicks) > 0.005 * sourceTicks) {
        out.why = tr("The keys' positions do not end where the clip's source ends.");
        return out;
    }
    QList<SpeedPoint> points;
    for (const Key &k : std::as_const(ks)) {
        SpeedPoint p;
        p.pos = k.t / last;
        p.speed = k.v;
        points.append(p); // no handles: straight lines between the keys
    }
    SpeedCurve curve;
    curve.setPoints(points);
    if (curve.isEmpty()) {
        out.why = tr("The keys do not make a curve.");
        return out;
    }
    const int64_t predicted = curve.retimedDurationUs(srcSpanUs);
    const int64_t error = std::abs(predicted - timelineDurationUs);
    const int64_t tolerance = std::max<int64_t>(20'000, timelineDurationUs / 500);
    if (error > tolerance) {
        out.why = tr("Read as straight lines between the keys, the ramp would last %1 s, but the clip lasts %2 s.")
                      .arg(QString::number(double(predicted) / 1e6, 'f', 3),
                           QString::number(double(timelineDurationUs) / 1e6, 'f', 3));
        return out;
    }
    out.ok = true;
    out.points = points;
    out.errorUs = error;
    return out;
}

// What a clip's `effectChainList` holds. Only the shape is read, never what an effect would do:
// effects are not imported, so all the report needs is how many there were and what they were
// called. `unrecognized` counts chains whose structure is not the one seen in saved projects
// (effectChainList[].effectList[]).
struct AttachedEffects
{
    int count = 0;
    int unrecognized = 0;
    QStringList ids; // at most kMaxIds, each cut short, for the report
    static constexpr int kMaxIds = 8;
};

AttachedEffects attachedEffectsOf(const QJsonValue &chains)
{
    AttachedEffects out;
    if (chains.isArray()) {
        for (const QJsonValue &chain : chains.toArray()) {
            const QJsonObject chainObj = chain.toObject();
            if (!chain.isObject() || chainObj.isEmpty()) {
                if (!chain.isObject() && !chain.isNull() && !chain.isUndefined())
                    ++out.unrecognized;
                continue;
            }
            if (!chainObj.contains(QStringLiteral("effectList"))) {
                ++out.unrecognized;
                continue;
            }
            for (const QJsonValue &effect : chainObj.value(QStringLiteral("effectList")).toArray()) {
                ++out.count;
                const QString id = effect.toObject().value(QStringLiteral("id")).toString().left(64);
                if (!id.isEmpty() && out.ids.size() < AttachedEffects::kMaxIds)
                    out.ids.append(id);
            }
        }
    } else if (chains.isObject() ? !chains.toObject().isEmpty()
                                 : (chains.isString() && !chains.toString().isEmpty())) {
        ++out.unrecognized;
    }
    return out;
}

QString rateText(const QJsonObject &rate)
{
    return QStringLiteral("%1/%2").arg(rate.value(QStringLiteral("num")).toDouble(), 0, 'f', 0)
        .arg(rate.value(QStringLiteral("den")).toDouble(1), 0, 'f', 0);
}

} // namespace

QString localPathFromFilmora(const QString &uriOrPath)
{
    QString s = uriOrPath.trimmed();
    if (s.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        const QUrl url(s);
        QString local = url.isLocalFile() ? url.toLocalFile() : QString();
        if (local.isEmpty()) {
            // "file:/C:/x" — one slash, which QUrl reads as a path on no host.
            local = QUrl::fromPercentEncoding(s.mid(5).toUtf8());
            while (local.startsWith(QLatin1String("//")))
                local.remove(0, 1);
        }
        // "/C:/Users/…" on Windows.
        if (local.size() > 3 && local.at(0) == QLatin1Char('/') && local.at(2) == QLatin1Char(':'))
            local.remove(0, 1);
        s = local;
    }
    return s;
}

std::optional<ImportedTimeline> parse(const QString &wfpPath, QString *error, CancelPtr cancel)
{
    const QString fileName = QFileInfo(wfpPath).fileName();
    const auto cancelled = [&]() -> std::optional<ImportedTimeline> {
        if (error)
            *error = QStringLiteral("cancelled");
        return std::nullopt;
    };
    if (isCancelled(cancel))
        return cancelled();
    WfpArchive archive(wfpPath);
    archive.setCancel(cancel);
    WfpStatus zipStatus;
    if (!archive.open(&zipStatus)) {
        if (error) {
            if (zipStatus.code == WfpError::CannotOpen)
                *error = tr("Can't open %1.").arg(fileName);
            else if (zipStatus.code == WfpError::NotZip)
                *error = tr("%1 is not a Filmora project Dafin can read.").arg(fileName);
            else
                *error = tr("%1 was not opened: %2").arg(fileName, zipStatus.message());
        }
        return std::nullopt;
    }
    if (archive.members().isEmpty()) {
        if (error)
            *error = tr("%1 is not a Filmora project Dafin can read.").arg(fileName);
        return std::nullopt;
    }

    // Everything below reads members through `archive`, which hands out only bytes it has checked
    // (size, deflate end, CRC, budget). A member that is damaged ends the import; a member that is
    // simply absent or not JSON is skipped, as before.
    WfpStatus fatal;
    const auto failWith = [&]() -> std::optional<ImportedTimeline> {
        if (fatal.code == WfpError::Cancelled)
            return cancelled();
        if (error)
            *error = tr("%1 was not opened: %2").arg(fileName, fatal.message());
        return std::nullopt;
    };

    QJsonObject info;
    if (!readJson(archive, QStringLiteral("ProjectFolder/project_info.json"), &info, &fatal))
        return failWith();
    QString timelinePath;
    const QString mediaId = info.value(QStringLiteral("timeline_mediaId")).toString();
    if (!mediaId.isEmpty())
        timelinePath = QStringLiteral("ProjectFolder/Medias/%1/timeline.wesproj").arg(mediaId);
    QJsonObject root;
    if (!timelinePath.isEmpty() && !readJson(archive, timelinePath, &root, &fatal))
        return failWith();
    if (root.isEmpty()) {
        // Any timeline the archive carries.
        for (const WfpArchive::Member &e : archive.members()) {
            if (!e.isDir && e.canonicalName.endsWith(QLatin1String(".wesproj"))) {
                if (!readJson(archive, e.name, &root, &fatal))
                    return failWith();
                if (!root.value(QStringLiteral("timelineInfos")).toArray().isEmpty())
                    break;
            }
        }
    }
    const QJsonArray timelines = root.value(QStringLiteral("timelineInfos")).toArray();
    if (timelines.isEmpty()) {
        if (error)
            *error = tr("This Filmora project has no timeline Dafin can read. Projects saved by Filmora 15 and "
                        "later work; open an older one in a current Filmora and save it again.");
        return std::nullopt;
    }
    // The main timeline is the one project_info names, else the first.
    int chosen = 0;
    const int currentId = root.value(QStringLiteral("currentTimelineId")).toInt(-1);
    bool currentFound = false;
    for (int i = 0; i < timelines.size(); ++i) {
        if (timelines.at(i).toObject().value(QStringLiteral("timelineId")).toInt(-2) == currentId) {
            chosen = i;
            currentFound = true;
        }
    }
    const QJsonObject timelineObj = timelines.at(chosen).toObject();
    const QString timelineKey = timelineObj.contains(QStringLiteral("timelineId"))
        ? QString::number(timelineObj.value(QStringLiteral("timelineId")).toInt())
        : QStringLiteral("none");

    ImportedTimeline timeline;
    CompatibilityReport &report = timeline.report;

    if (timelines.size() > 1 && !currentFound) {
        CompatEntry e;
        e.status = CompatStatus::Approximated;
        e.construct = QStringLiteral("timeline_choice");
        e.sourceId = QStringLiteral("timeline:%1").arg(timelineKey);
        e.where = tr("Choice of timeline");
        e.reason = tr("The project holds %n timeline(s) and does not say which one is current.", nullptr,
                      int(timelines.size()));
        e.applied = tr("The first timeline was imported.");
        e.hint = tr("If it is the wrong one, set the one you want as current in Filmora and save again.");
        report.add(e);
    }
    for (int i = 0; i < timelines.size(); ++i) {
        if (i == chosen)
            continue;
        const QJsonObject other = timelines.at(i).toObject();
        CompatEntry e;
        e.status = CompatStatus::Omitted;
        e.construct = QStringLiteral("other_timeline");
        e.sourceId = QStringLiteral("timeline:%1").arg(
            other.contains(QStringLiteral("timelineId")) ? QString::number(other.value(QStringLiteral("timelineId")).toInt())
                                                         : QStringLiteral("index%1").arg(i));
        e.where = tr("Another timeline in the project");
        e.reason = tr("Only one timeline is imported; this one is not the current one.");
        e.applied = tr("Left out, with everything on it.");
        e.hint = tr("Open it in Filmora, make it the current timeline and save, to import it.");
        report.add(e);
    }

    timeline.name = info.value(QStringLiteral("project_file_name")).toString(QFileInfo(wfpPath).completeBaseName());
    bool rateStated = false;
    const QJsonObject rate = timelineObj.value(QStringLiteral("frameRate")).toObject();
    const double num = rate.value(QStringLiteral("num")).toDouble();
    const double den = rate.value(QStringLiteral("den")).toDouble(1);
    QString rateAsWritten;
    if (num > 0 && den > 0) {
        timeline.fps = num / den;
        rateStated = true;
        rateAsWritten = rateText(rate);
        if (num == std::floor(num) && den == std::floor(den) && num < 1e9 && den < 1e9) {
            timeline.fpsNum = int(num);
            timeline.fpsDen = int(den);
        }
    } else {
        const QJsonArray r = info.value(QStringLiteral("project_timeline_framerate")).toArray();
        if (r.size() == 2 && r.at(0).toDouble() > 0 && r.at(1).toDouble() > 0) {
            timeline.fps = r.at(0).toDouble() / r.at(1).toDouble();
            rateStated = true;
            rateAsWritten = QStringLiteral("%1/%2").arg(r.at(0).toDouble(), 0, 'f', 0).arg(r.at(1).toDouble(), 0, 'f', 0);
            const double fn = r.at(0).toDouble();
            const double fd = r.at(1).toDouble();
            if (fn == std::floor(fn) && fd == std::floor(fd) && fn < 1e9 && fd < 1e9) {
                timeline.fpsNum = int(fn);
                timeline.fpsDen = int(fd);
            }
        }
    }
    {
        const KeptRate kept = keptFrameRate(timeline.fpsNum, timeline.fpsDen);
        if (kept.valid()) {
            timeline.keptFpsNum = kept.num;
            timeline.keptFpsDen = kept.den;
        }
    }
    {
        CompatEntry e;
        e.construct = QStringLiteral("frame_rate");
        e.sourceId = QStringLiteral("timeline:%1").arg(timelineKey);
        e.where = tr("Frame rate");
        const int projectFps = projectFpsFor(timeline.fps);
        if (!rateStated) {
            e.status = CompatStatus::Unknown;
            e.reason = tr("The project does not state a frame rate.");
            e.applied = tr("%1 fps was assumed.").arg(projectFps);
            e.hint = tr("Check the frame rate in Project settings.");
        } else if (qAbs(timeline.fps - projectFps) < 1e-6) {
            e.status = CompatStatus::Preserved;
        } else if (timeline.keptFpsNum > 0) {
            // 29.97 and its kin: Dafin edits on the whole grid but keeps the exact rate for export.
            e.status = CompatStatus::Preserved;
            e.where = tr("Frame rate");
            e.reason = tr("The frame rate is %1 (%2 fps).")
                           .arg(QStringLiteral("%1/%2").arg(timeline.keptFpsNum).arg(timeline.keptFpsDen),
                                QString::number(timeline.fps, 'f', 5));
            e.applied = tr("Kept as %1/%2 for export (\"Same as project\"); the timeline's editing grid is %3 fps.")
                            .arg(timeline.keptFpsNum).arg(timeline.keptFpsDen).arg(projectFps);
        } else {
            e.status = CompatStatus::Approximated;
            e.reason = tr("The frame rate is %1 (%2 fps).").arg(rateAsWritten, QString::number(timeline.fps, 'f', 5));
            e.applied = tr("Dafin projects use whole frame rates, so this one runs at %1 fps.").arg(projectFps);
            e.hint = tr("Check frame-accurate cuts after importing.");
        }
        report.add(e);
    }

    const int w = timelineObj.value(QStringLiteral("resolutionWidth")).toInt();
    const int h = timelineObj.value(QStringLiteral("resolutionHeight")).toInt();
    if (w > 0 && h > 0) {
        timeline.width = w;
        timeline.height = h;
    } else {
        const QJsonArray r = info.value(QStringLiteral("project_timeline_resolution")).toArray();
        if (r.size() == 2 && r.at(0).toInt() > 0 && r.at(1).toInt() > 0) {
            timeline.width = r.at(0).toInt();
            timeline.height = r.at(1).toInt();
        }
    }
    timeline.sampleRate = timelineObj.value(QStringLiteral("sampleRate"))
                              .toInt(info.value(QStringLiteral("project_sample_rate")).toInt());

    QHash<QString, int> skipped;
    struct Numbered
    {
        int number;
        int order;
        ImportedTrack track;
    };
    QList<Numbered> video;
    QList<Numbered> audio;
    const QJsonArray trackInfos = timelineObj.value(QStringLiteral("trackInfos")).toArray();
    for (int t = 0; t < trackInfos.size(); ++t) {
        const QJsonObject track = trackInfos.at(t).toObject();
        const int type = track.value(QStringLiteral("trackType")).toInt();
        const bool isAudio = type == 2;
        const bool isVideo = type == 1;
        const QString kind = isAudio ? QStringLiteral("audio")
                                     : isVideo ? QStringLiteral("video") : QStringLiteral("type%1").arg(type);
        const int trackLabel = track.value(QStringLiteral("trackTag")).toInt(t + 1);
        ImportedTrack out;
        out.audio = isAudio;
        out.muted = track.value(QStringLiteral("mute")).toBool() || track.value(QStringLiteral("hide")).toBool()
                    || track.value(QStringLiteral("isMute")).toBool() || track.value(QStringLiteral("isHide")).toBool();
        out.locked = track.value(QStringLiteral("lock")).toBool() || track.value(QStringLiteral("isLock")).toBool();
        const QJsonArray clipList = track.value(QStringLiteral("clipList")).toArray();
        for (int ci = 0; ci < clipList.size(); ++ci) {
            if (isCancelled(cancel))
                return cancelled();
            const QJsonObject clip = clipList.at(ci).toObject();
            const QString sourceId = QStringLiteral("%1:%2:%3:%4").arg(timelineKey, kind).arg(t).arg(ci);
            const QString where = isAudio ? tr("Audio track %1, clip %2").arg(trackLabel).arg(ci + 1)
                                  : isVideo ? tr("Video track %1, clip %2").arg(trackLabel).arg(ci + 1)
                                            : tr("Track %1 (type %2), clip %3").arg(trackLabel).arg(type).arg(ci + 1);
            int64_t begin = 0;
            int64_t end = 0;
            int64_t inPoint = 0;
            int64_t outPoint = 0;
            const bool timesValid = ticksToUs(clip.value(QStringLiteral("tlBegin")), &begin)
                && ticksToUs(clip.value(QStringLiteral("tlEnd")), &end)
                && ticksToUs(clip.value(QStringLiteral("inPoint")), &inPoint)
                && ticksToUs(clip.value(QStringLiteral("outPoint")), &outPoint);

            const QString filename = clip.value(QStringLiteral("filename")).toString();
            if (filename.isEmpty() || (!isAudio && !isVideo)) {
                skipped[tr("titles, effects and stickers")] += 1;
                CompatEntry e;
                e.status = CompatStatus::Unsupported;
                e.construct = QStringLiteral("non_media_clip");
                e.sourceId = sourceId;
                e.where = where;
                e.sourceTimeUs = timesValid && begin >= 0 ? begin : -1;
                e.reason = filename.isEmpty()
                    ? tr("The clip names no media file: a title, effect, sticker or another kind of clip Dafin cannot read.")
                    : tr("The clip is on a track that is neither video nor audio (type %1).").arg(type);
                e.applied = tr("Left out.");
                e.hint = tr("Add the title, effect or sticker again in Dafin.");
                report.add(e);
                continue;
            }
            if (!timesValid || end <= begin) {
                CompatEntry e;
                e.status = CompatStatus::Omitted;
                e.construct = QStringLiteral("empty_clip");
                e.sourceId = sourceId;
                e.where = where;
                e.sourceTimeUs = timesValid && begin >= 0 ? begin : -1;
                e.reason = timesValid ? tr("The clip's end is not after its start.")
                                      : tr("The clip's times are not valid numbers.");
                e.applied = tr("Left out.");
                report.add(e);
                continue;
            }
            ImportedClip c;
            c.sourceId = sourceId;
            c.path = localPathFromFilmora(filename);
            c.name = QFileInfo(c.path).fileName();
            c.startUs = begin;
            c.durationUs = end - begin;
            c.srcInUs = inPoint;
            const int64_t srcOut = outPoint;
            const QJsonObject speed = clip.value(QStringLiteral("speed")).toObject();
            c.reverse = speed.value(QStringLiteral("reverse")).toBool();
            c.still = isVideo && isStillSuffix(c.path);
            // The source span over the timeline span is the speed; the curve says the same for a
            // constant speed and is only read to spot a ramp.
            double rateValue = srcOut > c.srcInUs ? double(srcOut - c.srcInUs) / double(c.durationUs) : 0.0;
            const double curve = constantSpeedOf(speed.value(QStringLiteral("speedParam")));
            const bool ramps = curve <= 0;
            // A ramp becomes a Dafin speed ramp only when it is proven (see proveRamp); any other
            // ramp stays one constant speed, said so in the report and the notes.
            RampProof proof;
            if (ramps && !c.still)
                proof = proveRamp(speed.value(QStringLiteral("speedParam")), outPoint - c.srcInUs, c.durationUs);
            if (ramps && !proof.ok) {
                if (!timeline.notes.contains(tr("Speed ramps became constant speeds.")))
                    timeline.notes.append(tr("Speed ramps became constant speeds."));
            } else if (!ramps && rateValue <= 0) {
                rateValue = curve;
            }
            if (!c.still && srcOut > c.srcInUs)
                c.srcSpanUs = srcOut - c.srcInUs;
            const bool speedUsable = rateValue > 0.01 && rateValue < 100.0;
            c.speed = c.still ? 1.0 : (speedUsable ? rateValue : 1.0);
            if (qAbs(c.speed - 1.0) < 1e-3)
                c.speed = 1.0;
            if (proof.ok) {
                c.speedPoints = proof.points;
                c.speed = 1.0; // the ramp carries the speed; Clip::speed stays 1
            }
            if (c.still) {
                c.srcInUs = 0;
                c.reverse = false;
            }

            bool clean = true;
            if (proof.ok) {
                CompatEntry e;
                e.status = CompatStatus::Preserved;
                e.construct = QStringLiteral("speed_curve");
                e.sourceId = sourceId;
                e.where = where;
                e.sourceTimeUs = begin;
                e.applied = tr("Mapped to a Dafin speed ramp of %n point(s), joined by straight lines; it lasts as long as the clip does in the project (within %1 ms).",
                               nullptr, int(proof.points.size()))
                                .arg(QString::number(double(proof.errorUs) / 1000.0, 'f', 1));
                report.add(e);
            } else if (ramps && !c.still) {
                const CurveSummary curveInfo = summarizeCurve(speed.value(QStringLiteral("speedParam")));
                CompatEntry e;
                e.status = CompatStatus::Approximated;
                e.construct = QStringLiteral("speed_curve");
                e.sourceId = sourceId;
                e.where = where;
                e.sourceTimeUs = begin;
                e.reason = tr("The speed changes along the clip (a ramp of %n key(s), %1× to %2×).", nullptr, curveInfo.keys)
                               .arg(QString::number(curveInfo.min, 'g', 4), QString::number(curveInfo.max, 'g', 4));
                e.applied = tr("The ramp became one constant speed of %1×: the source span over the timeline span.")
                                .arg(QString::number(c.speed, 'g', 5));
                if (!proof.why.isEmpty())
                    e.reason += QLatin1Char(' ') + tr("It was not mapped because: %1").arg(proof.why);
                e.hint = tr("Set the ramp again in the clip's Speed tools.");
                report.add(e);
                clean = false;
            } else if (!ramps && !c.still && !speedUsable) {
                CompatEntry e;
                e.status = CompatStatus::Approximated;
                e.construct = QStringLiteral("speed");
                e.sourceId = sourceId;
                e.where = where;
                e.sourceTimeUs = begin;
                e.reason = tr("The clip's speed could not be read, or lies outside 0.01× to 100×.");
                e.applied = tr("The clip plays at normal speed.");
                e.hint = tr("Set the speed again in the clip's Speed tools.");
                report.add(e);
                clean = false;
            }
            const AttachedEffects effects = attachedEffectsOf(clip.value(QStringLiteral("effectChainList")));
            if (effects.count > 0) {
                CompatEntry e;
                e.status = CompatStatus::Omitted;
                e.construct = QStringLiteral("attached_effect");
                e.sourceId = sourceId;
                e.where = where;
                e.sourceTimeUs = begin;
                e.reason = tr("%n effect(s) attached to the clip in Filmora are not imported.", nullptr, effects.count);
                if (!effects.ids.isEmpty())
                    e.reason += QStringLiteral(" (%1)").arg(effects.ids.join(QStringLiteral(", ")));
                e.applied = tr("The media was imported as it is, without the effect(s).");
                e.hint = tr("Add the effect(s) again in Dafin's Effects tab.");
                report.add(e);
                clean = false;
            }
            if (effects.unrecognized > 0) {
                CompatEntry e;
                e.status = CompatStatus::Unknown;
                e.construct = QStringLiteral("attached_effect_unrecognized");
                e.sourceId = sourceId;
                e.where = where;
                e.sourceTimeUs = begin;
                e.reason = tr("The clip has %n effect entry(ies) in a layout Dafin does not recognise.", nullptr,
                              effects.unrecognized);
                e.applied = tr("Left out; the media was imported as it is.");
                e.hint = tr("Check the clip against the original in Filmora.");
                report.add(e);
                clean = false;
            }
            if (clean) {
                CompatEntry e;
                e.status = CompatStatus::Preserved;
                e.construct = isAudio ? QStringLiteral("audio_clip") : QStringLiteral("media_clip");
                e.sourceId = sourceId;
                e.where = where;
                e.sourceTimeUs = begin;
                report.add(e);
            }
            out.clips.append(c);
        }
        if (!isAudio && !isVideo)
            continue; // title, effect and sticker tracks: their clips were counted above
        std::stable_sort(out.clips.begin(), out.clips.end(),
                         [](const ImportedClip &a, const ImportedClip &b) { return a.startUs < b.startUs; });
        const int number = trackLabel;
        (isAudio ? audio : video).append({number, t, out});
    }

    // Filmora numbers tracks from the bottom: track 1 is the lowest video track.
    std::stable_sort(video.begin(), video.end(), [](const Numbered &a, const Numbered &b) {
        return a.number != b.number ? a.number > b.number : a.order > b.order;
    });
    std::stable_sort(audio.begin(), audio.end(), [](const Numbered &a, const Numbered &b) {
        return a.number != b.number ? a.number < b.number : a.order < b.order;
    });

    // A video clip whose sound Filmora keeps as a follower clip on an audio track has that sound
    // there; one without such a clip plays the sound of its own file.
    for (Numbered &v : video) {
        for (ImportedClip &c : v.track.clips) {
            bool follower = false;
            for (const Numbered &a : std::as_const(audio)) {
                for (const ImportedClip &ac : a.track.clips) {
                    if (ac.path == c.path && qAbs(ac.startUs - c.startUs) < 50000
                        && qAbs(ac.durationUs - c.durationUs) < 50000) {
                        follower = true;
                        break;
                    }
                }
                if (follower)
                    break;
            }
            c.keepsOwnAudio = !follower && !c.still;
        }
    }
    for (const Numbered &v : std::as_const(video))
        timeline.tracks.append(v.track);
    for (const Numbered &a : std::as_const(audio))
        timeline.tracks.append(a.track);
    for (auto it = skipped.constBegin(); it != skipped.constEnd(); ++it) {
        timeline.skippedClips += it.value();
        timeline.skippedKinds.append(it.key());
    }
    return timeline;
}

} // namespace drift::interchange::filmora
