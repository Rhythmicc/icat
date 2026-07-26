# ImagePreview (icat) - C++ Image Preview Tool & Library

A high-performance (6.5x than Python implementation) terminal image viewer and library written in C++ that supports standard image formats, SVG, multi-page PDF, and EPS.

## Features

- **Library & CLI**: Build as a standalone tool (`icat`) or integrate `ImagePreview` as a library into your own projects.
- **Terminal Support**: Native support for iTerm2, Kitty, and Ghostty terminal image protocols.
- **Automatic Scaling**: Images are automatically scaled to fit terminal dimensions while preserving aspect ratio.
- **Centered Output**: Images are centered by default in the terminal.
- **Optional Format Support**:
  - **SVG**: High-quality vector rendering via `librsvg`.
  - **PDF/EPS**: PDF page rendering and EPS preview via `poppler-cpp`.
- **PDF Reader**: Lightweight full-screen mode with page navigation and resize-aware redraws.
- **Parallel Processing**: Uses OpenMP for faster image operations.

## Prerequisites

### macOS (Homebrew)

```bash
brew install pkg-config poppler librsvg cairo libomp jpeg
```

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libcurl4-openssl-dev libjpeg-dev libpoppler-cpp-dev librsvg2-dev libcairo2-dev libomp-dev
```

### Fedora

```bash
sudo dnf install cmake gcc-c++ pkgconfig libcurl-devel libjpeg-turbo-devel poppler-cpp-devel librsvg2-devel cairo-devel libomp-devel
```

## Compilation & Installation

### Prebuilt releases

Version tags produce GitHub Release archives for macOS 15 (Apple Silicon and
Intel) and Ubuntu 24.04 (ARM64 and x86_64). Download the archive matching your
system from the [Releases page](https://github.com/Rhythmicc/icat/releases),
extract it, and place `icat` somewhere on your `PATH`:

```bash
tar -xzf icat_VERSION_macos15_arm64.tar.gz
sudo install -m 755 icat /usr/local/bin/icat
```

Replace `VERSION` with the release number without its leading `v`. Release
archives contain a native dynamically linked executable, so install the
platform prerequisites listed above before running it. Each release also
contains `checksums.txt`. Compare the checksum of the archive you downloaded
with its corresponding entry:

```bash
shasum -a 256 icat_VERSION_macos15_arm64.tar.gz
grep 'icat_VERSION_macos15_arm64.tar.gz' checksums.txt
```

Windows is not currently published because the PDF TUI uses POSIX terminal
interfaces (`termios`, `poll`, and Unix signals).

### Build and Install

Standard build and installation:

```bash
cmake -S . -B build
cmake --build build -j
sudo cmake --install build
```

### Build Options

You can customize the build during the configuration step:

- `-DBUILD_SHARED_LIBS=ON`: Build as a shared library instead of static.
- `-DENABLE_PDF=OFF`: Disable PDF/EPS support.
- `-DENABLE_SVG=OFF`: Disable SVG support.
- `-DFORCE_KITTY_PROTOCOL=ON`: Always use the Kitty output path even if terminal detection would normally fail.

Example:

```bash
cmake -DBUILD_SHARED_LIBS=ON -DENABLE_PDF=OFF -DFORCE_KITTY_PROTOCOL=ON -S . -B build
```

## Releasing

After the release commit is on `main`, push an annotated version tag:

```bash
git tag -a v0.1.0 -m "v0.1.0"
git push origin v0.1.0
```

GitHub Actions builds and tests all four supported targets, generates SHA-256
checksums, and creates or updates the corresponding GitHub Release.

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

For explicit PDF page control, keep the document open and render 1-based page numbers:

```cpp
#include <image_preview.h>
#include <iostream>

int main() {
    ImagePreview::PdfDocument document;
    std::string error;
    if (!document.open("path/to/document.pdf", error) ||
        !document.display_page(2, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    return 0;
}
```

## CLI Usage (`icat`)

If you just want to use the included tool:

```bash
icat path/to/image.png
icat path/to/vector.svg
icat path/to/document.pdf
icat --page 3 path/to/document.pdf
icat --tui path/to/document.pdf
icat --tui --page 3 path/to/document.pdf
```

`--page` uses the page numbers shown by PDF readers, starting at 1. In `--tui`
mode, use `h`/left arrow for the previous page, `l`/right arrow/space for the
next page, `g` and `G` for the first and last page, and `q` to quit. The reader
redraws the active page when the terminal is resized. TUI mode requires a local
PDF and an interactive terminal; other image formats continue to use the normal
single-preview behavior.

Use the `--perf` flag to see performance metrics:

```bash
icat --perf path/to/image.jpg
```

## How it works

1. **Standard Images**: Uses `stb_image` to load PNG, JPG, BMP, etc.
2. **SVG**: Renders via `librsvg` to a Cairo ARGB surface (with white background).
3. **PDF/EPS**: Renders the selected PDF page (or the first EPS page) via `poppler-cpp`, choosing a DPI for the current terminal size.
4. **Output**: Detects terminal capabilities (Kitty/Ghostty or iTerm2) and sends base64-encoded image data using the appropriate escape sequences.
