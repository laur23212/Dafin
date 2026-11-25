#pragma once

// Dafin: `dafin --bench` — the reproducible export / preview / scrub benchmark that decides whether
// the CUDA changes C4 (direct NVENC export), C5 (preview sync) and C6 (NVDEC pool) are worth
// building. See docs/cuda-benchmark.md.
//
//   dafin --bench --media <video>[,<video2>] [options]
//   dafin --bench --project <file.drift|file.json> [options]
//
// The driver runs every variant in its own child process (`dafin --bench-run ...`), paired and in
// alternating order, then writes results.jsonl, summary.md and summary.json to the output folder.
namespace drift::worker {

int runBenchTool(int argc, char *argv[]);    // --bench (the driver)
int runBenchChild(int argc, char *argv[]);   // --bench-run (one measured run)

} // namespace drift::worker
