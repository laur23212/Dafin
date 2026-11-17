#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include <functional>

// Dafin: add-ons that do not come from Drift's store.
//
// The store (drift-addons.cutwire.org) only serves .driftpkg packs signed with the Drift team's
// key, and Dafin cannot sign one. A model Dafin needs that the store does not carry is therefore
// listed here instead: plain files at a fixed, revision-pinned URL, each with its size and
// SHA-256 compiled in. AddonManager merges these rows into its index, so they download through the
// same queue (progress, Cancel, resume of a .part), and once every file matches its hash they are
// moved into addons/<id>/ and recorded in installed.json like any store add-on: the kind's search
// paths find them, and Settings › Downloaded AI models lists and deletes them.
//
// The pinned hash is what makes this as safe as a signature for these files: a mirror, a changed
// upstream file or a truncated download all fail the check, and nothing is installed.
namespace drift::addon::direct {

struct File
{
    QString url;
    QString path;      // inside the add-on folder, forward slashes
    qint64 size = 0;
    QByteArray sha256; // lower-case hex
};

struct Addon
{
    QString id;
    QString version;
    QString name;
    QString description;
    QString details;
    QString author;
    QString license;   // SPDX id
    QString licenseText;
    QString source;    // where the weights and the conversion come from
    QString kind;      // what it provides, e.g. "separation-model"
    QString root;      // the provided folder, inside the add-on
    QList<File> files;
    // Store-catalog fields (tagline, requires, …) for AddonCatalog, which plans the AI engine in.
    QJsonObject catalogEntry;

    qint64 downloadSize() const;
};

const QList<Addon> &addons();
const Addon *find(const QString &id);

// The row AddonManager adds to its index: the same fields the store's index rows have, plus
// "direct": true and "source". No "url": downloads go through the file list instead.
QJsonObject indexRow(const Addon &addon);

// Where file `index` lands in the download cache, finished. While it downloads it is this path
// plus ".part".
QString cachePath(const Addon &addon, int index);

// Checks every downloaded file against its size and SHA-256 and installs them into
// addonInstallDir(id) (staged beside it, then swapped in), with a manifest.json (licence, source,
// files) and the licence text, then records the add-on in installed.json. A file that fails the
// check is deleted so the next attempt downloads it again. keepGoing() returning false cancels.
// Blocking; runs on a worker thread.
bool installDownloaded(const Addon &addon, const std::function<bool()> &keepGoing, QString *error);

} // namespace drift::addon::direct
