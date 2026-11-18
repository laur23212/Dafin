# Provide the ONNX Runtime headers (and, for a dev build that bundles one, a CPU runtime) when they
# are not installed locally. On success sets ONNXRUNTIME_ROOT (and DRIFT_ONNXRUNTIME_FETCHED).
#
# Drift links no ONNX Runtime: it compiles against the headers and dlopens the runtime the user
# installs (src/engine/ml/runtime/OrtRuntime.cpp). So:
#   - a build that bundles no runtime (DRIFT_BUNDLE_ONNXRUNTIME=OFF: every CI, package, Android and
#     wasm build) takes the headers vendored in third_party/onnxruntime and downloads nothing;
#   - a dev build that stages a CPU runtime into its build tree (DRIFT_BUNDLE_ONNXRUNTIME=ON)
#     downloads the official NuGet package, Microsoft.ML.OnnxRuntime, pinned by SHA-256, and takes
#     the headers and its platform's runtime from it.
#
# Dafin: until 2026-11-17 this downloaded onnxruntime-<platform>-<version>.tgz/.zip from the GitHub
# releases of microsoft/onnxruntime. That repository and every release asset have answered 404
# since about 22:20 UTC that day, which failed every configure. The NuGet package is Microsoft's
# own build of the same release (its headers are byte-identical to the GitHub tarball's once CRLF
# is turned into LF; see third_party/onnxruntime/README.md).

if(DRIFT_ONNXRUNTIME_FETCHED)
    return()
endif()

set(_onnxruntime_version "1.27.0")
set(_onnxruntime_vendored "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")

# Upstream publishes no x86_64 (or universal2) macOS runtime, on GitHub or NuGet; an Intel Mac can
# only take the headers. Bundling is forced off rather than stage an arm64 runtime an x86_64
# process would fail to dlopen with no explanation.
if(APPLE AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64" AND DRIFT_BUNDLE_ONNXRUNTIME)
    message(STATUS
        "ONNX Runtime publishes no x86_64 macOS build; using the vendored headers and disabling "
        "DRIFT_BUNDLE_ONNXRUNTIME. Install an Acceleration addon, or point DRIFT_ONNXRUNTIME_DIR "
        "at an x86_64 runtime.")
    set(DRIFT_BUNDLE_ONNXRUNTIME OFF CACHE BOOL
        "Stage a CPU ONNX Runtime into the build tree for development" FORCE)
endif()

# find_path / find_library cache their results, so a build tree configured against a previous
# prefix would keep resolving to it.
unset(OnnxRuntime_INCLUDE_DIR CACHE)
unset(OnnxRuntime_LIBRARY CACHE)

# ---- headers only: vendored, no network ---------------------------------------------------------
# Android and wasm never bundle a runtime (Android gets one as an Acceleration addon; a browser has
# none to dlopen), whatever DRIFT_BUNDLE_ONNXRUNTIME says.
if(ANDROID OR EMSCRIPTEN OR NOT DRIFT_BUNDLE_ONNXRUNTIME)
    if(NOT EXISTS "${_onnxruntime_vendored}/include/onnxruntime_cxx_api.h")
        message(FATAL_ERROR
            "The vendored ONNX Runtime headers are missing (${_onnxruntime_vendored}/include). "
            "Restore them from git, or set ONNXRUNTIME_ROOT to an ONNX Runtime install.")
    endif()
    set(ONNXRUNTIME_ROOT "${_onnxruntime_vendored}" CACHE PATH
        "ONNX Runtime install prefix (vendored headers)" FORCE)
    set(OnnxRuntime_ROOT "${_onnxruntime_vendored}" CACHE PATH
        "ONNX Runtime install prefix (vendored headers)" FORCE)
    set(DRIFT_ONNXRUNTIME_VERSION "${_onnxruntime_version}" CACHE STRING
        "Version of the ONNX Runtime headers" FORCE)
    set(DRIFT_ONNXRUNTIME_FETCHED TRUE)
    message(STATUS "ONNX Runtime ${_onnxruntime_version} headers: vendored (${_onnxruntime_vendored})")
    return()
endif()

# ---- a runtime to bundle: the NuGet package -----------------------------------------------------
if(APPLE)
    set(_onnxruntime_rid "osx-arm64")
elseif(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(_onnxruntime_rid "win-arm64")
    else()
        set(_onnxruntime_rid "win-x64")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
        set(_onnxruntime_rid "linux-arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(_onnxruntime_rid "linux-x64")
    endif()
endif()

if(NOT _onnxruntime_rid)
    message(FATAL_ERROR
        "No prebuilt ONNX Runtime to bundle for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}. "
        "Configure with -DDRIFT_BUNDLE_ONNXRUNTIME=OFF (the vendored headers are enough to build), "
        "or install ONNX Runtime and set ONNXRUNTIME_ROOT.")
endif()

# One package holds every platform's runtime (about 135 MB); it is fetched once per build tree.
set(_onnxruntime_archive "microsoft.ml.onnxruntime.${_onnxruntime_version}.nupkg")
set(_onnxruntime_sha256 "c725e8cefdfe4de08befb9f79b3f218c162439cdcdb97cbbb5192888d58ea669")
set(_onnxruntime_url
    "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${_onnxruntime_version}/${_onnxruntime_archive}")

set(_onnxruntime_staging "${CMAKE_BINARY_DIR}/_deps/onnxruntime/nuget")
set(_onnxruntime_prefix "${_onnxruntime_staging}/onnxruntime-${_onnxruntime_rid}-${_onnxruntime_version}")
file(GLOB _onnxruntime_have_lib "${_onnxruntime_prefix}/lib/*onnxruntime*")

if(NOT EXISTS "${_onnxruntime_prefix}/include/onnxruntime_cxx_api.h" OR NOT _onnxruntime_have_lib)
    file(MAKE_DIRECTORY "${_onnxruntime_staging}")
    set(_onnxruntime_archive_path "${_onnxruntime_staging}/${_onnxruntime_archive}")

    if(EXISTS "${_onnxruntime_archive_path}")
        file(SHA256 "${_onnxruntime_archive_path}" _onnxruntime_have_sha)
        if(NOT _onnxruntime_have_sha STREQUAL _onnxruntime_sha256)
            # A partial or failed earlier download: never extract it, fetch it again.
            file(REMOVE "${_onnxruntime_archive_path}")
        endif()
    endif()

    if(NOT EXISTS "${_onnxruntime_archive_path}")
        message(STATUS "Downloading ONNX Runtime ${_onnxruntime_version} (NuGet): ${_onnxruntime_url}")
        file(DOWNLOAD "${_onnxruntime_url}" "${_onnxruntime_archive_path}"
            EXPECTED_HASH SHA256=${_onnxruntime_sha256}
            STATUS _onnxruntime_status
            SHOW_PROGRESS)
        list(GET _onnxruntime_status 0 _onnxruntime_code)
        if(NOT _onnxruntime_code EQUAL 0)
            file(REMOVE "${_onnxruntime_archive_path}")
            message(FATAL_ERROR
                "Could not download ONNX Runtime ${_onnxruntime_version} from ${_onnxruntime_url}: "
                "${_onnxruntime_status}. Configure with -DDRIFT_BUNDLE_ONNXRUNTIME=OFF to build with "
                "the vendored headers and no bundled runtime.")
        endif()
    endif()

    set(_onnxruntime_unpack "${_onnxruntime_staging}/unpack")
    file(REMOVE_RECURSE "${_onnxruntime_unpack}" "${_onnxruntime_prefix}")
    file(ARCHIVE_EXTRACT
        INPUT "${_onnxruntime_archive_path}"
        DESTINATION "${_onnxruntime_unpack}"
        PATTERNS "build/native/include/*" "runtimes/${_onnxruntime_rid}/native/*")
    # The layout FindOnnxRuntime expects: <prefix>/include and <prefix>/lib.
    file(COPY "${_onnxruntime_unpack}/build/native/include/" DESTINATION "${_onnxruntime_prefix}/include")
    file(COPY "${_onnxruntime_unpack}/runtimes/${_onnxruntime_rid}/native/" DESTINATION "${_onnxruntime_prefix}/lib")
    file(REMOVE_RECURSE "${_onnxruntime_unpack}")
endif()

if(NOT EXISTS "${_onnxruntime_prefix}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR
        "Failed to extract ONNX Runtime from ${_onnxruntime_archive}")
endif()

set(ONNXRUNTIME_ROOT "${_onnxruntime_prefix}" CACHE PATH
    "ONNX Runtime install prefix (auto-downloaded)" FORCE)
set(OnnxRuntime_ROOT "${_onnxruntime_prefix}" CACHE PATH
    "ONNX Runtime install prefix (auto-downloaded)" FORCE)
set(DRIFT_ONNXRUNTIME_VERSION "${_onnxruntime_version}" CACHE STRING
    "Version of the fetched ONNX Runtime" FORCE)
set(DRIFT_ONNXRUNTIME_FETCHED TRUE)
message(STATUS "ONNX Runtime ${_onnxruntime_version} (${_onnxruntime_rid}) ready at ${ONNXRUNTIME_ROOT}")
