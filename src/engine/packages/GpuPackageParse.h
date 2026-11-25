#pragma once

#include "engine/gpu/effects/GpuEffectDefinition.h"
#include "core/effects/EffectPreset.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>

// Shared JSON grammar for GPU packages. Effects (effect.json) and transitions
// (transition.json) declare parameters and pipelines identically; only the
// surrounding metadata differs.
namespace GpuPackageParse {

QString readTextFile(const QString &path, QString *errorOut);

QString slugifyCategory(const QString &raw);

QVariant jsonToVariant(const QJsonValue &value);

// Parse "parameters": [...]. Rejects identifiers colliding with reserved uniforms when gpuBackend.
// packageDir is used to resolve FilePath defaults via resolvePackageAsset; may be empty.
bool parseParameters(const QJsonArray &params, QList<drift::EffectParamSpec> *out, bool gpuBackend,
                     QString *errorOut, const QString &packageDir = {});

// Refuse a package whose "nextFeatures" names something this Drift doesn't implement.
bool checkNextFeatures(const QJsonObject &root, QString *errorOut);

void parseFixedParams(const QJsonObject &obj, QMap<QString, QVariant> *out);

// A pass that samples an intermediate buffer while drawing into that same buffer. OpenGL leaves
// that undefined (a feedback loop): the pass may read pixels it has already written. The compositor
// binds every buffer input and the output target straight from the same pool by id, so nothing
// stops it today; a package avoids it by writing to a second buffer (ping-pong).
struct PassAlias
{
    int passIndex = 0;  // position in the pipeline
    int inputIndex = 0; // which input of the pass names the buffer it also writes
    QString bufferId;
};

// Every aliased pass of a parsed pipeline, in pass order. Pure; no GL.
QList<PassAlias> findAliasedPasses(const drift::GpuEffectDefinition &definition);

// One readable sentence for a log line, a debug report or a package author.
QString describeAlias(const PassAlias &alias);

// What loadGpuPipeline does with an aliased pass.
//   Warn    the package still loads; a warning is logged and added to definition.warnings.
//   Reject  the package does not load, with the same sentence as the error.
// Warn stays the default until the shipped inventory has been shown clean (tests/tst_gpupackagealias
// scans every shipped package); flipping kAliasedPassPolicy to Reject is then the whole change.
enum class AliasPolicy { Warn, Reject };
constexpr AliasPolicy kAliasedPassPolicy = AliasPolicy::Warn;

// Parse "pipeline": { intermediateBuffers, textures, passes }. Sets out->valid on success.
// maxSourceIndex bounds "source_texture" indices: 0 for effects, 1 for transitions.
bool loadGpuPipeline(const QJsonObject &root, const QString &packageDir, int maxSourceIndex,
                     drift::GpuEffectDefinition *out, QString *errorOut,
                     AliasPolicy aliasPolicy = kAliasedPassPolicy);

// Copy the sampler and region params out of a parsed parameter list onto a pipeline, for the runtime.
void collectRuntimeParams(const QList<drift::EffectParamSpec> &params, drift::GpuEffectDefinition *out);

// Resolve an optional relative/absolute asset path inside a package; empty when missing.
QString resolvePackageAsset(const QString &packageDir, const QString &relOrAbs);

} // namespace GpuPackageParse
