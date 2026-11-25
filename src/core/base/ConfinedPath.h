#pragma once

#include <QString>

// Dafin (C20 / D-006): a file an imported effect names — a texture, a default image or file — may
// only be inside the folder the effect came from (the folder the user authorised by linking or
// installing it). The check is on the real location: `..` is resolved, symlinks are resolved, and
// the result must still be under the folder's own resolved location.
namespace drift {

enum class ConfineError {
    None,
    Empty,         // no file name
    RootInvalid,   // no folder to be confined to
    Absolute,      // drive, UNC, rooted or resource path: only relative names are allowed
    Escapes,       // leaves the folder by `..`
    SymlinkEscapes,// looks inside, but a link on the way leads outside
    NotFound,      // inside the folder as far as the name goes, but not a file
};

struct ConfinedPath
{
    QString path; // the resolved file (symlinks followed), set when ok()
    ConfineError error = ConfineError::None;
    bool ok() const { return error == ConfineError::None; }
};

// `relative` is read with / or \ as separators. The file must exist.
ConfinedPath resolveConfined(const QString &root, const QString &relative);

} // namespace drift
