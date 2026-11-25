#pragma once

#include "core/interchange/CancelToken.h"
#include "core/interchange/ImportedTimeline.h"
#include "core/interchange/ImportedTimelineProject.h"
#include "core/interchange/MediaRelink.h"

#include <QHash>
#include <QString>

#include <optional>

// Dafin (C20 / D-003): a Filmora import as one job a worker thread can run and the person can
// cancel. It reads the .wfp, looks for the media, probes what it needs and lays the clips out, and
// hands back ONE finished Project (or says why not). It never touches the open document: the caller
// applies the result once, on the GUI thread, when the status is Ok. A failure or a cancel leaves
// the project exactly as it was, because nothing of it was ever in play.
//
// A tie between different files of the same name (D-005) ends the job at NeedsChoice with the
// questions; the caller asks the person and runs the job again with `relinkChoices` filled in.
namespace drift::interchange {

struct ImportRequest
{
    QString wfpPath;
    MediaProbeFn probe;                    // media facts: duration (to tell candidates apart), picture size
    CancelPtr cancel;
    QHash<QString, QString> relinkChoices; // answers to the questions of an earlier NeedsChoice
};

struct ImportOutcome
{
    enum class Status { Failed, Cancelled, NeedsChoice, Ok };
    Status status = Status::Failed;
    QString error;                         // Failed: a sentence for the person
    std::optional<ImportedTimeline> timeline;
    ProjectBuildResult built;              // Ok: the finished project; NeedsChoice: its questions
};

ImportOutcome runFilmoraImport(const ImportRequest &request);

// Every picture and video clip of `project` fitted to its media's shape, as adding it from the bin
// would. `probe` gives the size; false when `cancel` was set part-way (the project is then
// half-fitted and must be dropped).
bool fitImportedLayouts(Project &project, const MediaProbeFn &probe, const CancelPtr &cancel);

} // namespace drift::interchange
