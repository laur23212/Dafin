#pragma once

#include "core/interchange/CompatibilityReport.h"
#include "core/interchange/ImportedTimeline.h"
#include "core/interchange/MediaRelink.h"
#include "core/project/Project.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace drift::interchange {

// One media file the search could not settle: several different files of its name fit. The
// importer never picks one; the caller asks the person and calls toProject again with the answer.
struct ImportQuestion
{
    QString key;                      // relinkKey() of the stored path: the answer is stored under it
    QString clipName;
    QList<MediaCandidate> candidates; // best first, with what is known about each
};

// What the build may use besides the timeline itself.
struct BuildOptions
{
    MediaProbeFn probe;                       // lets the search check durations; none = size and hash only
    CancelPtr cancel;                         // polled while the folder is searched
    QHash<QString, QString> relinkChoices;    // answers to earlier ImportQuestions (empty path = leave unlinked)
};

struct ProjectBuildResult
{
    Project project;
    // Media files the project names that are not on this computer (also not next to the project
    // file under the same name). Their clips are kept and show as missing until relinked.
    QStringList missingMedia;
    int clipCount = 0;
    // What building the project found out that reading the file could not: each clip whose media
    // is not on this computer (missing_dependency). The reader's own entries stay on the
    // ImportedTimeline; summary() reads both.
    CompatibilityReport report;
    // Files that need the person's choice (see ImportQuestion); their clips are built as missing.
    QList<ImportQuestion> questions;
    bool cancelled = false;
};

// The Dafin project for an imported timeline. `projectFile` is the .prproj / .wfp it came from:
// media that moved is looked for beside it (its relative path, then its name in that folder tree).
//
// Video and audio go on tracks of their own kind in the same order; still images go on a picture
// track directly above the video track they were on (Dafin keeps pictures and video on separate
// tracks). A video clip and the audio clip that came with it are linked, and the video's own sound
// stays off when the audio clip carries it. Clips are laid out to fill the canvas; the app fits
// each to its media's shape once it knows it (see ProjectFileController).
ProjectBuildResult toProject(const ImportedTimeline &timeline, const QString &projectFile,
                             const BuildOptions &options = {});

// One line for the editor's status message: what came across and what did not.
QString summary(const ImportedTimeline &timeline, const ProjectBuildResult &result, const QString &appName);

} // namespace drift::interchange
