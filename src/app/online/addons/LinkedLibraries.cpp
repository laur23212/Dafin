#include "app/online/addons/LinkedLibraries.h"

#include "app/online/addons/AddonManager.h"

#include <QDir>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLShader>
#include <QSettings>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QtConcurrent/QtConcurrentRun>

namespace {

const QString kFoldersKey = QStringLiteral("LinkedLibraries/folders");

QString normalized(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return normalized(a).compare(normalized(b), Qt::CaseInsensitive) == 0;
#else
    return normalized(a) == normalized(b);
#endif
}

// `inner` is `outer` or somewhere below it.
bool isInside(const QString &inner, const QString &outer)
{
    const QString o = normalized(outer) + QLatin1Char('/');
    const QString i = normalized(inner) + QLatin1Char('/');
#ifdef Q_OS_WIN
    return i.startsWith(o, Qt::CaseInsensitive);
#else
    return i.startsWith(o);
#endif
}

const QStringList kAllKinds = {QStringLiteral("effects"), QStringLiteral("transitions"),
                               QStringLiteral("stickers"), QStringLiteral("fonts"),
                               QStringLiteral("looks"), QStringLiteral("text-styles")};

} // namespace

LinkedLibraries::LinkedLibraries(AddonManager *addons, QObject *parent)
    : QObject(parent), m_addons(addons)
{
    m_paths = QSettings().value(kFoldersKey).toStringList();
    connect(&m_watcher, &QFutureWatcher<dafin::linked::ImportReport>::finished, this,
            &LinkedLibraries::finished);
}

LinkedLibraries::~LinkedLibraries()
{
    if (m_cancel)
        m_cancel->store(true);
    m_watcher.waitForFinished();
}

QVariantList LinkedLibraries::folders() const
{
    QVariantList out;
    for (const QString &path : m_paths) {
        const auto report = m_reports.constFind(path);
        const bool scanned = report != m_reports.cend();
        QVariantMap folder{
            {QStringLiteral("path"), QDir::toNativeSeparators(path)},
            {QStringLiteral("key"), path},
            {QStringLiteral("missing"), !QFileInfo(path).isDir()},
            {QStringLiteral("scanning"), samePath(path, m_current) || m_queue.contains(path)},
            {QStringLiteral("scanned"), scanned},
        };
        if (scanned) {
            folder.insert(QStringLiteral("effects"), report->effects);
            folder.insert(QStringLiteral("transitions"), report->transitions);
            folder.insert(QStringLiteral("stickers"), report->stickers);
            folder.insert(QStringLiteral("fonts"), report->fonts);
            folder.insert(QStringLiteral("looks"), report->looks);
            folder.insert(QStringLiteral("titles"), report->titles);
            folder.insert(QStringLiteral("skipped"), report->skipped());
            folder.insert(QStringLiteral("found"), int(report->items.size()));
            folder.insert(QStringLiteral("error"), report->error);
            folder.insert(QStringLiteral("note"), report->note);
        }
        out.append(folder);
    }
    return out;
}

QString LinkedLibraries::addFolder(const QUrl &url)
{
    const QString path = normalized(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return tr("That isn't a folder on this computer.");
    // Failsafes: a whole drive would be read file by file, and Dafin's own data folder would
    // read its own converted copies back in.
    if (QDir(path).isRoot())
        return tr("Pick a folder rather than a whole drive.");
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty() && (isInside(path, appData) || isInside(appData, path)))
        return tr("That folder holds Dafin's own data. Pick the folder with your effects.");
    for (const QString &existing : std::as_const(m_paths)) {
        if (samePath(existing, path))
            return tr("That folder is already linked.");
        if (isInside(path, existing))
            return tr("It is already included through %1.").arg(QDir::toNativeSeparators(existing));
    }
    // Linked folders inside the new one: it covers them, so they go (their items come back
    // through it, and nothing is listed twice).
    const QStringList covered = m_paths;
    for (const QString &existing : covered) {
        if (isInside(existing, path))
            removeFolder(existing);
    }
    m_paths.append(path);
    QSettings().setValue(kFoldersKey, m_paths);
    queue(path);
    emit foldersChanged();
    return {};
}

void LinkedLibraries::removeFolder(const QString &path)
{
    const int index = m_paths.indexOf(path);
    if (index < 0)
        return;
    m_paths.removeAt(index);
    QSettings().setValue(kFoldersKey, m_paths);
    m_queue.removeAll(path);
    m_reports.remove(path);

    const QString output = dafin::linked::outputDirFor(path);
    if (m_current == path) {
        // The worker is still writing there: stop it, and delete once it has.
        if (m_cancel)
            m_cancel->store(true);
        m_removeAfterScan.append(output);
    } else {
        QDir(output).removeRecursively();
        if (m_addons)
            m_addons->reloadForKinds(kAllKinds);
    }
    emit foldersChanged();
}

void LinkedLibraries::rescan()
{
    for (const QString &path : std::as_const(m_paths))
        queue(path);
    emit foldersChanged();
}

QVariantList LinkedLibraries::skippedItems(const QString &path) const
{
    QVariantList out;
    const auto report = m_reports.constFind(path);
    if (report == m_reports.cend())
        return out;
    for (const dafin::linked::ImportedItem &item : report->items) {
        if (item.skipReason.isEmpty())
            continue;
        out.append(QVariantMap{{QStringLiteral("name"), item.name},
                               {QStringLiteral("reason"), item.skipReason},
                               {QStringLiteral("partly"), !item.kind.isEmpty()}});
    }
    return out;
}

void LinkedLibraries::start()
{
    // Converted folders whose source was unlinked while a scan ran, or by hand in the settings.
    QDir root(dafin::linked::linkedLibrariesRoot());
    QStringList keep;
    for (const QString &path : std::as_const(m_paths))
        keep.append(QFileInfo(dafin::linked::outputDirFor(path)).fileName());
    bool removed = false;
    for (const QString &dir : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!keep.contains(dir)) {
            QDir(root.filePath(dir)).removeRecursively();
            removed = true;
        }
    }
    if (removed && m_addons)
        m_addons->reloadForKinds(kAllKinds);
    rescan();
}

void LinkedLibraries::queue(const QString &path)
{
    if (samePath(path, m_current) || m_queue.contains(path))
        return;
    m_queue.append(path);
    if (m_current.isEmpty())
        runNext();
}

void LinkedLibraries::ensureSurface()
{
    if (m_surface)
        return;
    // Made here on the GUI thread, used by the worker's own context (Qt allows that for an
    // offscreen surface; only create() has to be on the GUI thread).
    m_surface = std::make_unique<QOffscreenSurface>();
    m_surface->setFormat(QSurfaceFormat::defaultFormat());
    m_surface->create();
}

void LinkedLibraries::runNext()
{
    if (m_queue.isEmpty()) {
        m_current.clear();
        emit busyChanged();
        return;
    }
    m_current = m_queue.takeFirst();
    emit busyChanged();
    ensureSurface();

    const QString source = m_current;
    const QString output = dafin::linked::outputDirFor(source);
    QOffscreenSurface *surface = m_surface->isValid() ? m_surface.get() : nullptr;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<std::atomic_bool> cancel = m_cancel;

    m_watcher.setFuture(QtConcurrent::run([source, output, surface, cancel]() {
        // A context of the worker's own, to compile each converted shader once on this
        // computer's driver. Without one (no OpenGL here) every shader that converts is kept.
        std::unique_ptr<QOpenGLContext> context;
        if (surface) {
            context = std::make_unique<QOpenGLContext>();
            context->setFormat(surface->format());
            if (!context->create() || !context->makeCurrent(surface))
                context.reset();
        }
        dafin::linked::ShaderCheck check;
        if (context) {
            check = [](const QString &fragment, QString *error) {
                QOpenGLShader shader(QOpenGLShader::Fragment);
                if (shader.compileSourceCode(fragment))
                    return true;
                if (error)
                    *error = shader.log();
                return false;
            };
        }
        dafin::linked::ImportReport report =
            dafin::linked::importFilmoraFolder(source, output, check, cancel.get());
        if (context)
            context->doneCurrent();
        return report;
    }));
    emit foldersChanged();
}

void LinkedLibraries::finished()
{
    const QString path = m_current;
    const dafin::linked::ImportReport report = m_watcher.result();

    for (const QString &dir : std::as_const(m_removeAfterScan))
        QDir(dir).removeRecursively();
    const bool removedAny = !m_removeAfterScan.isEmpty();
    m_removeAfterScan.clear();

    if (m_paths.contains(path) && !report.cancelled) {
        m_reports.insert(path, report);
        emit scanFinished(QDir::toNativeSeparators(path), report.imported(), report.skipped(), report.error);
    }
    // Reloading on the GUI thread: the font catalog touches QFontDatabase.
    if (m_addons) {
        if (removedAny)
            m_addons->reloadForKinds(kAllKinds);
        else if (!report.changedKinds.isEmpty())
            m_addons->reloadForKinds(report.changedKinds);
    }
    m_current.clear();
    runNext();
    emit foldersChanged();
}
