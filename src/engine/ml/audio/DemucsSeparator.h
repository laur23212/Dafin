#pragma once

#include <QString>

#include <memory>

namespace drift {

// Dafin: HTDemucs (Hybrid Transformer Demucs v4, Meta, MIT) music source separation on ONNX
// Runtime, for the AI › Vocal remover tool.
//
// The model is the ONNX conversion published as smank/htdemucs-onnx (MIT): the STFT and inverse
// STFT are convolutions inside the graph, so it takes the raw waveform and returns one waveform per
// source, [1, 2, frames] -> [1, 4, 2, frames] in the order drums, bass, other, vocals. It was
// trained on 7.8 s segments at 44.1 kHz stereo, which is what it is fed; stems::separateVocals
// stitches the segments of a longer clip together.
//
// It is an add-on (kind "separation-model", see app/online/addons/DirectAddons.cpp), looked up
// like every other model: DAFIN_VOCALS_MODEL_DIR, then the installed add-on, then models/htdemucs
// beside the app or in the app data folder.
//
// Everything is synchronous on the calling thread; one run at a time.
class DemucsSeparator
{
public:
    static DemucsSeparator &instance();

    // File check only, no session: what UI gating uses.
    static bool modelPresent();

    static int sampleRate();    // 44100
    static int segmentFrames(); // 343980 (7.8 s)
    // Index of the vocals in the model's sources.
    static int vocalsSource();

    // Loads the session on first use (CPU, or the GPU the AI engine picks). False with
    // lastError() when the engine or the model is missing or fails to load. Blocks for a moment.
    bool available();
    QString lastError() const;

    // One segment: planarIn is [2][segmentFrames()], planarVocalsOut receives the vocals in the
    // same layout. False with *error on failure.
    bool separateSegment(const float *planarIn, float *planarVocalsOut, QString *error);

    // Frees the session (about 1.3 GB), so the files can be deleted and the memory is back while
    // editing. The next available() loads it again.
    void unload();

    DemucsSeparator(const DemucsSeparator &) = delete;
    DemucsSeparator &operator=(const DemucsSeparator &) = delete;

private:
    DemucsSeparator();
    ~DemucsSeparator();

    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace drift
