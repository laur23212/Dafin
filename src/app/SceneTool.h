#pragma once

namespace drift {

// Dafin: Scene Title command line (docs/scene-titles.md). No window opens.
//
//   dafin --render-scene <file.scene.json> --out <dir> [--frames 0,10,20|all] [--fps N]
//         [--duration S] [--param name=value]... [--font-dir <dir>]... [--matte none|base|reference]
//         [--size WxH]
//       Renders frames with the native renderer: <dir>/alpha/NNNN.png (straight RGBA, like
//       tools/scene-ref/render.py) and, with a matte, <dir>/matte/NNNN.png. "reference" is the
//       scene's own preview background; a video background is left to
//       tools/scene-ref/native_compare.py, which lays the alpha frames over it exactly as the
//       reference does. Prints a JSON summary. Exit code 0 = rendered, 1 = scene or font error,
//       2 = bad arguments.
int runSceneTool(int argc, char *argv[]);

} // namespace drift
