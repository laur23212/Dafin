#include "engine/ml/audio/DemucsSeparator.h"

#include "engine/addons/PackageSearchPaths.h"
#include "engine/ml/runtime/OrtSupport.h"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace drift {

namespace {

constexpr int kSampleRate = 44100;
constexpr int kSegmentFrames = 343980; // int(7.8 * 44100), HTDemucs' training segment
constexpr int kChannels = 2;
constexpr int kVocals = 3; // drums, bass, other, vocals
constexpr const char *kModelFile = "htdemucs.onnx";

QString resolveModelDir()
{
    const QStringList roots = PackageSearchPaths::defaultSearchPaths(
        QStringLiteral("DAFIN_VOCALS_MODEL_DIR"), QStringLiteral("models/htdemucs"),
        QStringLiteral("separation-model"));
    for (const QString &root : roots) {
        const QFileInfo file(QDir(root).filePath(QLatin1String(kModelFile)));
        if (file.isFile() && file.size() > 0)
            return root;
    }
    return {};
}

} // namespace

struct DemucsSeparator::Impl
{
    QMutex mutex;
    bool loaded = false;
    QString error;
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inNames, outNames;

    bool ensureLoaded();
};

bool DemucsSeparator::Impl::ensureLoaded()
{
    if (loaded)
        return true;
    // Unlike the other models a failure is not sticky: the usual one is "not downloaded yet",
    // and the download can finish while Dafin runs.
    error.clear();
    if (!ort::ensureLoaded(&error))
        return false;

    const QString dir = resolveModelDir();
    if (dir.isEmpty()) {
        error = QStringLiteral("The vocal remover model is not installed. Download it from AI › Vocal "
                               "remover, or set DAFIN_VOCALS_MODEL_DIR.");
        return false;
    }

    Ort::Env &env = ort::env();
    const std::basic_string<ORTCHAR_T> path = ort::ortPath(QDir(dir).filePath(QLatin1String(kModelFile)));
    if (!ort::buildSessions(env, "vocals", false, &error, [&](Ort::SessionOptions &opts) {
            // The CPU arena keeps the peak of every run and adds to it (1.7 GB after four
            // segments, measured); without it the peak stays at one segment's working set
            // (1.25 GB), at the same speed. What lets 4 GB machines run it.
            opts.DisableCpuMemArena();
            session = std::make_unique<Ort::Session>(env, path.c_str(), opts);
        })) {
        session.reset();
        return false;
    }
    inNames = ort::sessionNames(*session, true);
    outNames = ort::sessionNames(*session, false);
    if (inNames.size() != 1 || outNames.empty()) {
        error = QStringLiteral("%1 has %2 inputs and %3 outputs; expected 1 and 1.")
                    .arg(QLatin1String(kModelFile))
                    .arg(inNames.size())
                    .arg(outNames.size());
        session.reset();
        return false;
    }
    loaded = true;
    return true;
}

DemucsSeparator::DemucsSeparator() : d(std::make_unique<Impl>()) {}
DemucsSeparator::~DemucsSeparator() = default;

DemucsSeparator &DemucsSeparator::instance()
{
    static DemucsSeparator inst;
    return inst;
}

bool DemucsSeparator::modelPresent()
{
    return !resolveModelDir().isEmpty();
}

int DemucsSeparator::sampleRate()
{
    return kSampleRate;
}

int DemucsSeparator::segmentFrames()
{
    return kSegmentFrames;
}

int DemucsSeparator::vocalsSource()
{
    return kVocals;
}

bool DemucsSeparator::available()
{
    QMutexLocker lock(&d->mutex);
    return d->ensureLoaded();
}

QString DemucsSeparator::lastError() const
{
    QMutexLocker lock(&d->mutex);
    return d->error;
}

void DemucsSeparator::unload()
{
    QMutexLocker lock(&d->mutex);
    d->session.reset();
    d->inNames.clear();
    d->outNames.clear();
    d->loaded = false;
}

bool DemucsSeparator::separateSegment(const float *planarIn, float *planarVocalsOut, QString *error)
{
    QMutexLocker lock(&d->mutex);
    if (!d->ensureLoaded()) {
        if (error)
            *error = d->error;
        return false;
    }
    try {
        const std::array<int64_t, 3> shape{1, kChannels, kSegmentFrames};
        // ONNX Runtime only reads an input tensor; the cast is how its C++ API takes one.
        Ort::Value input = Ort::Value::CreateTensor<float>(
            ort::cpuMemory(), const_cast<float *>(planarIn), size_t(kChannels) * kSegmentFrames,
            shape.data(), shape.size());
        const std::vector<const char *> in = ort::cstrs(d->inNames);
        const std::vector<const char *> out{d->outNames.front().c_str()};
        std::vector<Ort::Value> outputs =
            d->session->Run(Ort::RunOptions{nullptr}, in.data(), &input, 1, out.data(), 1);
        if (outputs.empty() || !outputs.front().IsTensor()) {
            if (error)
                *error = QStringLiteral("The vocal remover model returned nothing");
            return false;
        }
        const std::vector<int64_t> dims = outputs.front().GetTensorTypeAndShapeInfo().GetShape();
        // [1, sources, 2, frames]
        if (dims.size() != 4 || dims[0] != 1 || dims[1] <= kVocals || dims[2] != kChannels
            || dims[3] != kSegmentFrames) {
            if (error)
                *error = QStringLiteral("The vocal remover model returned an unexpected shape");
            return false;
        }
        const float *data = outputs.front().GetTensorData<float>();
        std::memcpy(planarVocalsOut, data + size_t(kVocals) * kChannels * kSegmentFrames,
                    sizeof(float) * size_t(kChannels) * kSegmentFrames);
        return true;
    } catch (const Ort::Exception &e) {
        const QString message = QString::fromUtf8(e.what());
        if (error) {
            *error = ort::isOutOfMemoryFailure(message)
                         ? QStringLiteral("Not enough memory to separate the audio. Close other programs "
                                          "and try again.")
                         : message;
        }
        return false;
    }
}

} // namespace drift
