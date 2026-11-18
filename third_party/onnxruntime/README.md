# ONNX Runtime 1.27.0 headers (vendored)

The C and C++ API headers Dafin compiles against (`include/`), and ONNX Runtime's licence (MIT,
`LICENSE`). Dafin links no ONNX Runtime: it dlopens the runtime the user installs as an
Acceleration add-on (`src/engine/ml/runtime/OrtRuntime.cpp`), so builds that ship no runtime
(`DRIFT_BUNDLE_ONNXRUNTIME=OFF`: every CI, package, Android and wasm build) take these headers and
download nothing (`cmake/FetchOnnxRuntime.cmake`).

Why they are here: on 2026-11-17 at about 22:20 UTC github.com/microsoft/onnxruntime and all of
its release downloads started answering 404, and every configure that fetched
`onnxruntime-linux-x64-1.27.0.tgz` from there failed.

Source: NuGet `Microsoft.ML.OnnxRuntime` 1.27.0
(`https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/1.27.0/microsoft.ml.onnxruntime.1.27.0.nupkg`,
SHA-256 `c725e8cefdfe4de08befb9f79b3f218c162439cdcdb97cbbb5192888d58ea669`, upstream commit
`8f0278c77bf44b0cc83c098c6c722b92a36ac4b5`), `build/native/include/*.h` with CRLF turned into LF.
After that they are byte-identical to `include/*.h` of the GitHub release
`onnxruntime-linux-x64-1.27.0.tgz` (SHA-256 `547e40a4…246f`, the hash Dafin pinned before). That
tarball's two `include/core/providers/` headers are not used by Dafin and are left out.

To move to another version: change `_onnxruntime_version` and the NuGet SHA-256 in
`cmake/FetchOnnxRuntime.cmake`, and replace `include/` and `LICENSE` from that version's package.
