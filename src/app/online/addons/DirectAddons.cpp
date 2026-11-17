#include "app/online/addons/DirectAddons.h"

#include "engine/addons/AddonPackage.h"
#include "engine/addons/AddonRegistry.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace drift::addon::direct {

namespace {

// The ONNX Runtime pack for this platform, exactly as the store's own AI models ask for it.
QJsonObject runtimeRequirement()
{
    QJsonObject prefer;
    for (const char *platform : {"android-arm64", "linux-arm64", "linux-x64", "osx-arm64", "osx-x64",
                                 "win-arm64", "win-x64"}) {
        prefer.insert(QLatin1String(platform),
                      QStringLiteral("onnxruntime.cpu.") + QLatin1String(platform));
    }
    return QJsonObject{{QStringLiteral("kind"), QStringLiteral("onnxruntime")},
                       {QStringLiteral("prefer"), prefer},
                       {QStringLiteral("default"), QJsonValue()},
                       {QStringLiteral("reason"), QStringLiteral("Runs on the AI engine")}};
}

// Demucs' own licence (github.com/facebookresearch/demucs/blob/main/LICENSE). The ONNX conversion
// is MIT as well; it adds no code to Dafin, only the weights in another container.
const char kDemucsLicense[] =
    "MIT License\n"
    "\n"
    "Copyright (c) Meta Platforms, Inc. and affiliates.\n"
    "\n"
    "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
    "of this software and associated documentation files (the \"Software\"), to deal\n"
    "in the Software without restriction, including without limitation the rights\n"
    "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
    "copies of the Software, and to permit persons to whom the Software is\n"
    "furnished to do so, subject to the following conditions:\n"
    "\n"
    "The above copyright notice and this permission notice shall be included in all\n"
    "copies or substantial portions of the Software.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
    "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
    "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
    "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
    "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
    "SOFTWARE.\n";

QList<Addon> buildAddons()
{
    QList<Addon> out;

    // HTDemucs (Demucs v4), the standard 4-source model. Revision-pinned so the hash never goes
    // stale; a newer conversion means a new version here.
    Addon demucs;
    demucs.id = QStringLiteral("htdemucs.v4");
    demucs.version = QStringLiteral("1.0.0");
    demucs.name = QStringLiteral("Vocal remover (HTDemucs)");
    demucs.description = QStringLiteral("Splits music into the voice and the instruments, on this computer.");
    demucs.details = QStringLiteral(
        "Hybrid Transformer Demucs v4 by Meta AI (Rouard, Massa, Défossez, ICASSP 2023), the standard "
        "4-source model, as an ONNX graph with the STFT inside (opset 17). Runs on the AI engine, on the "
        "graphics card when one is set up. About 1.3 GB of memory while it works.");
    demucs.author = QStringLiteral("Meta AI (Demucs) · ONNX conversion by smank");
    demucs.license = QStringLiteral("MIT");
    demucs.licenseText = QString::fromLatin1(kDemucsLicense);
    demucs.source = QStringLiteral(
        "Weights: https://github.com/facebookresearch/demucs (MIT). ONNX conversion: "
        "https://huggingface.co/smank/htdemucs-onnx (MIT), revision "
        "fb1fd25aa6260c253d86bbb4aa26b73aebc79f10, file htdemucs.onnx.");
    demucs.kind = QStringLiteral("separation-model");
    demucs.root = QStringLiteral("models/htdemucs");
    demucs.files.append(File{
        QStringLiteral("https://huggingface.co/smank/htdemucs-onnx/resolve/"
                       "fb1fd25aa6260c253d86bbb4aa26b73aebc79f10/htdemucs.onnx"),
        QStringLiteral("models/htdemucs/htdemucs.onnx"), 304321552,
        QByteArrayLiteral("d2b401f322558cd57d67a752ed7be3fa55178a0626011eda8ac7bb74e17280c0")});
    demucs.catalogEntry = QJsonObject{
        {QStringLiteral("id"), demucs.id},
        {QStringLiteral("tagline"), QStringLiteral("Split a song into the voice and the music")},
        {QStringLiteral("summary"),
         QStringLiteral("Separates the singing or speech from the instruments of any clip, on this computer. "
                        "Keep the voice, the music, or both on their own tracks.")},
        {QStringLiteral("categories"), QJsonArray{QStringLiteral("audio-cleanup")}},
        {QStringLiteral("keywords"), QJsonArray{QStringLiteral("vocals"), QStringLiteral("karaoke"),
                                                QStringLiteral("instrumental"), QStringLiteral("stems"),
                                                QStringLiteral("demucs")}},
        {QStringLiteral("icon"), QStringLiteral("audio")},
        {QStringLiteral("unlocks"),
         QJsonArray{QJsonObject{{QStringLiteral("label"), QStringLiteral("Vocal remover")},
                                {QStringLiteral("location"), QStringLiteral("AI tab")}}}},
        {QStringLiteral("requires"), QJsonArray{runtimeRequirement()}},
        {QStringLiteral("order"), 20},
    };
    out.append(demucs);
    return out;
}

} // namespace

qint64 Addon::downloadSize() const
{
    qint64 total = 0;
    for (const File &file : files)
        total += file.size;
    return total;
}

const QList<Addon> &addons()
{
    static const QList<Addon> list = buildAddons();
    return list;
}

const Addon *find(const QString &id)
{
    for (const Addon &addon : addons()) {
        if (addon.id == id)
            return &addon;
    }
    return nullptr;
}

QJsonObject indexRow(const Addon &addon)
{
    const double size = double(addon.downloadSize());
    return QJsonObject{
        {QStringLiteral("id"), addon.id},
        {QStringLiteral("version"), addon.version},
        {QStringLiteral("name"), addon.name},
        {QStringLiteral("description"), addon.description},
        {QStringLiteral("details"), addon.details + QStringLiteral("\n\n") + addon.source},
        {QStringLiteral("author"), addon.author},
        {QStringLiteral("license"), addon.license},
        {QStringLiteral("source"), addon.source},
        {QStringLiteral("kind"), addon.kind},
        {QStringLiteral("provides"),
         QJsonArray{QJsonObject{{QStringLiteral("kind"), addon.kind},
                                {QStringLiteral("root"), addon.root},
                                {QStringLiteral("items"), 1}}}},
        // Plain files: what is downloaded is what is installed.
        {QStringLiteral("downloadSize"), size},
        {QStringLiteral("installedSize"), size},
        {QStringLiteral("direct"), true},
    };
}

QString cachePath(const Addon &addon, int index)
{
    const QString name = index >= 0 && index < addon.files.size()
                             ? QFileInfo(addon.files.at(index).path).fileName()
                             : QStringLiteral("file");
    return QDir(addonDownloadCacheDir())
        .filePath(QStringLiteral("%1-%2-%3-%4").arg(addon.id, addon.version).arg(index).arg(name));
}

bool installDownloaded(const Addon &addon, const std::function<bool()> &keepGoing, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const auto cancelled = [&] { return keepGoing && !keepGoing(); };

    // Every file first, so a bad one leaves the installed copy (if any) alone.
    for (int i = 0; i < addon.files.size(); ++i) {
        const File &file = addon.files.at(i);
        const QString path = cachePath(addon, i);
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("The download is missing. Try again."));
        if (in.size() != file.size) {
            in.close();
            QFile::remove(path);
            return fail(QStringLiteral("The download is incomplete (%1 of %2 bytes). Try again.")
                            .arg(in.size())
                            .arg(file.size));
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QByteArray chunk(4 << 20, Qt::Uninitialized);
        for (;;) {
            if (cancelled())
                return fail(QStringLiteral("Cancelled"));
            const qint64 got = in.read(chunk.data(), chunk.size());
            if (got < 0)
                return fail(QStringLiteral("Could not read the download: %1").arg(in.errorString()));
            if (got == 0)
                break;
            hash.addData(QByteArrayView(chunk.constData(), got));
        }
        in.close();
        if (hash.result().toHex() != file.sha256.toLower()) {
            QFile::remove(path);
            return fail(QStringLiteral("The download was damaged (checksum mismatch). Try again."));
        }
    }
    if (cancelled())
        return fail(QStringLiteral("Cancelled"));

    const QString destination = addonInstallDir(addon.id);
    const QString staging = destination + QStringLiteral(".partial");
    QDir(staging).removeRecursively();
    if (!QDir().mkpath(staging))
        return fail(QStringLiteral("Cannot create %1").arg(staging));

    QJsonArray files;
    for (int i = 0; i < addon.files.size(); ++i) {
        const File &file = addon.files.at(i);
        const QString target = QDir(staging).filePath(file.path);
        QDir().mkpath(QFileInfo(target).absolutePath());
        // Same volume (addons/cache and addons/<id>), so this is a rename, not a 300 MB copy.
        if (!QFile::rename(cachePath(addon, i), target) && !QFile::copy(cachePath(addon, i), target)) {
            QDir(staging).removeRecursively();
            return fail(QStringLiteral("Could not move the download into place"));
        }
        QFile::remove(cachePath(addon, i));
        files.append(QJsonObject{{QStringLiteral("path"), file.path},
                                 {QStringLiteral("size"), double(file.size)},
                                 {QStringLiteral("sha256"), QString::fromLatin1(file.sha256)},
                                 {QStringLiteral("url"), file.url}});
    }

    const QJsonObject manifest{
        {QStringLiteral("id"), addon.id},
        {QStringLiteral("version"), addon.version},
        {QStringLiteral("name"), addon.name},
        {QStringLiteral("description"), addon.description},
        {QStringLiteral("author"), addon.author},
        {QStringLiteral("license"), addon.license},
        {QStringLiteral("source"), addon.source},
        {QStringLiteral("provides"),
         QJsonArray{QJsonObject{{QStringLiteral("kind"), addon.kind}, {QStringLiteral("root"), addon.root}}}},
        {QStringLiteral("files"), files},
    };
    {
        QSaveFile out(QDir(staging).filePath(QStringLiteral("manifest.json")));
        if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(manifest).toJson()) < 0 || !out.commit()) {
            QDir(staging).removeRecursively();
            return fail(QStringLiteral("Could not write the add-on's manifest"));
        }
    }
    if (!addon.licenseText.isEmpty()) {
        QSaveFile out(QDir(staging).filePath(QStringLiteral("LICENSE.txt")));
        if (out.open(QIODevice::WriteOnly)) {
            out.write(addon.licenseText.toUtf8());
            out.commit();
        }
    }

    QDir(destination).removeRecursively();
    if (!QDir().rename(staging, destination)) {
        QDir(staging).removeRecursively();
        return fail(QStringLiteral("Could not finish installing %1").arg(addon.name));
    }

    PackageInfo info;
    info.id = addon.id;
    info.version = addon.version;
    info.name = addon.name;
    info.description = addon.description;
    info.details = addon.details;
    info.author = addon.author;
    info.license = addon.license;
    info.installedSize = quint64(addon.downloadSize());
    info.provides.append(PackageProvide{addon.kind, addon.root, 1});
    info.raw = manifest;
    return recordInstalledAddon(info, error);
}

} // namespace drift::addon::direct
