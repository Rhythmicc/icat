# ImagePreview (icat) - C++ Image Preview Tool & Library

A high-performance (6.5x than Python implementation) terminal image viewer and library written in C++ that supports standard image formats, SVG, PDF (first page), and EPS.

## Features

- **Library & CLI**: Build as a standalone tool (`icat`) or integrate `ImagePreview` as a library into your own projects.
- **Terminal Support**: Native support for iTerm2, Kitty, and Ghostty terminal image protocols.
- **Automatic Scaling**: Images are automatically scaled to fit terminal dimensions while preserving aspect ratio.
- **Centered Output**: Images are centered by default in the terminal.
- **Optional Format Support**:
  - **SVG**: High-quality vector rendering via `librsvg`.
  - **PDF/EPS**: First page preview via `poppler-cpp`.
- **Parallel Processing**: Uses OpenMP for faster image operations.

## Prerequisites

### macOS (Homebrew)

```bash
brew install pkg-config poppler librsvg cairo libomp
```

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libpoppler-cpp-dev librsvg2-dev libcairo2-dev libomp-dev
```

### Fedora

```bash
sudo dnf install cmake gcc-c++ pkgconfig poppler-cpp-devel librsvg2-devel cairo-devel libomp-devel
```

## Compilation & Installation

### Build and Install

Standard build and installation:

```bash
mkdir -p build && cd build
cmake ..
make
sudo cmake --install .
```

### Build Options

You can customize the build during the configuration step:

- `-DBUILD_SHARED_LIBS=ON`: Build as a shared library instead of static.
- `-DENABLE_PDF=OFF`: Disable PDF/EPS support.
- `-DENABLE_SVG=OFF`: Disable SVG support.
- `-DFORCE_KITTY_PROTOCOL=ON`: Always use the Kitty output path even if terminal detection would normally fail.

Example:

```bash
cmake -DBUILD_SHARED_LIBS=ON -DENABLE_PDF=OFF -DFORCE_KITTY_PROTOCOL=ON ..
```

## Using as a Library

Once installed, you can use `ImagePreview` in your own CMake projects:

```cmake
find_package(ImagePreview REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ImagePreview::ImagePreview)
```

**Example code (`main.cpp`):**

```cpp
#include <image_preview.h>

int main() {
    ImagePreview::display_image("path/to/image.png");
    return 0;
}
```

## CLI Usage (`icat`)

If you just want to use the included tool:

```bash
icat path/to/image.png
icat path/to/vector.svg
icat path/to/document.pdf
```

Use the `--perf` flag to see performance metrics:

```bash
icat --perf path/to/image.jpg
```

## How it works

1. **Standard Images**: Uses `stb_image` to load PNG, JPG, BMP, etc.
2. **SVG**: Renders via `librsvg` to a Cairo ARGB surface (with white background).
3. **PDF/EPS**: Renders the first page via `poppler-cpp` at 150 DPI.
4. **Output**: Detects terminal capabilities (Kitty/Ghostty or iTerm2) and sends base64-encoded image data using the appropriate escape sequences.
