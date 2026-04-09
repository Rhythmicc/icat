# Agents Guide for cpp_image_preview

This document provides context and instructions for AI agents working in this repository.

## Project Overview

`cpp_image_preview` is a C++17 library and command-line executable (`icat`) that displays images natively in terminal emulators supporting inline image protocols (like Kitty and iTerm2). It supports standard image formats via `stb_image`, as well as PDF/EPS (via Poppler) and SVG (via librsvg).

## Code Organization

- `main.cpp`: The entry point for the `icat` command-line utility. Handles CLI arguments (like `--perf`) and invokes the library.
- `include/`: Contains public headers, including the main `image_preview.h` and the bundled header-only `stb` libraries (`stb_image.h`, etc.).
- `src/`: Contains the implementation of the `ImagePreview` library.
  - `src/image_preview.cpp`: Core logic for image loading, format conversion, SVG/PDF rendering, base64 encoding, and terminal output.
- `CMakeLists.txt`: Defines the build configuration, dependency resolution (OpenMP, Poppler, librsvg), library, and executable targets.

## Build System & Dependencies

The project uses **CMake** (minimum version 3.10) and requires **C++17**.

### Dependencies

- **Included/Vendored**: `stb_image`, `stb_image_write`, `stb_image_resize`
- **System Requirements**: 
  - `pkg-config`
  - OpenMP (optional, but automatically searched. Uses Homebrew's `libomp` on macOS).
  - `poppler-cpp` (optional, for PDF/EPS support).
  - `librsvg-2.0` and `cairo` (optional, for SVG support).

### CMake Options

- `BUILD_SHARED_LIBS` (Default: `OFF`): Build as a shared library instead of static.
- `ENABLE_PDF` (Default: `ON`): Enable PDF/EPS support using Poppler.
- `ENABLE_SVG` (Default: `ON`): Enable SVG support using librsvg.

### Standard Build Commands

To build the project:

```bash
mkdir build
cd build
cmake ..
# To disable optional dependencies: cmake -DENABLE_PDF=OFF -DENABLE_SVG=OFF ..
make
```

### Running the application

```bash
./build/icat path/to/image.png
# To see performance metrics
./build/icat --perf path/to/image.png
```

## Architecture & Code Conventions

- **Namespace**: The library logic is contained within the `ImagePreview` namespace.
- **Header-only libraries**: `stb` libraries are instantiated exactly once in `src/image_preview.cpp` by defining their respective `_IMPLEMENTATION` macros before including them. Do not define these macros elsewhere.
- **Parallelism**: OpenMP is used in computationally heavy loops (e.g., base64 encoding, pixel conversions). Use `#pragma omp parallel for` when iterating over large image buffers.
- **Performance Profiling**: The codebase contains a custom `Timer` struct in `src/image_preview.cpp` that records execution time of different blocks when `perf_enabled` is true. Use `Timer t("Block Name");` at the start of a scope to measure its duration.
- **Terminal Protocols**: Detection for the Kitty graphics protocol is based on the `TERM` and `TERM_PROGRAM` environment variables. If not Kitty (or Ghostty), it falls back to the iTerm2 inline image protocol using base64 encoded PNGs/JPEGs.

## Development Gotchas

- **Header Includes**: When editing `src/image_preview.cpp`, note that it disables some clang deprecation warnings around `stb_image_write.h`. Ensure any edits maintain this `#pragma` structure if necessary.
- **Platform Specifics**: On macOS, OpenMP is explicitly searched via `brew --prefix libomp`. Keep this in mind if modifying OpenMP build configurations.
