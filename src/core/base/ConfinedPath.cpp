#include "core/base/ConfinedPath.h"

#include <QDir>
#include <QFileInfo>

namespace drift {

namespace {

bool looksAbsolute(const QString &name)
{
    if (name.startsWith(QLatin1Char('/')) || name.startsWith(QLatin1Char('\\')) || name.startsWith(QLatin1Char(':')))
        return true;
    // "C:" and "C:\": a drive, which Windows reads relative to that drive's current folder too.
    return name.size() >= 2 && name.at(1) == QLatin1Char(':') && name.at(0).isLetter();
}

bool isUnder(const QString &path, const QString &root)
{
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    QString prefix = root;
    if (!prefix.endsWith(QLatin1Char('/')))
        prefix += QLatin1Char('/');
    return path.startsWith(prefix, cs);
}

} // namespace

ConfinedPath resolveConfined(const QString &root, const QString &relative)
{
    ConfinedPath out;
    if (relative.trimmed().isEmpty()) {
        out.error = ConfineError::Empty;
        return out;
    }
    if (root.isEmpty()) {
        out.error = ConfineError::RootInvalid;
        return out;
    }
    if (looksAbsolute(relative)) {
        out.error = ConfineError::Absolute;
        return out;
    }
    QString rel = relative;
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));

    // Lexically first: a name that climbs out of the folder is refused whether or not the file
    // it reaches exists.
    const QString cleanedRel = QDir::cleanPath(rel);
    if (cleanedRel == QLatin1String("..") || cleanedRel.startsWith(QLatin1String("../"))) {
        out.error = ConfineError::Escapes;
        return out;
    }

    // Then by where the file really is.
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (canonicalRoot.isEmpty()) {
        out.error = ConfineError::RootInvalid;
        return out;
    }
    const QFileInfo file(QDir(canonicalRoot).filePath(cleanedRel));
    const QString canonical = file.canonicalFilePath();
    if (canonical.isEmpty() || !QFileInfo(canonical).isFile()) {
        out.error = ConfineError::NotFound;
        return out;
    }
    if (!isUnder(canonical, canonicalRoot)) {
        out.error = ConfineError::SymlinkEscapes;
        return out;
    }
    out.path = canonical;
    return out;
}

} // namespace drift
