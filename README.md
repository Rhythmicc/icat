# icat - C++ Image Preview Tool

A high-performance terminal image viewer written in C++ that supports standard image formats, SVG, PDF (first page), and EPS.

## Features
- Displays images directly in your terminal (iTerm2, Kitty, Ghostty support).
- Automatic scaling to fit terminal dimensions.
- Centered image output.
- **Optional Support** for:
  - **SVG**: High-quality vector rendering via `librsvg`.
  - **PDF/EPS**: First page preview via `poppler-cpp`.

## Prerequisites

### macOS (Homebrew)
```bash
brew install pkg-config poppler librsvg cairo
```

### Ubuntu / Debian
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libpoppler-cpp-dev librsvg2-dev libcairo2-dev
```

### Fedora
```bash
sudo dnf install cmake gcc-c++ pkgconfig poppler-cpp-devel librsvg2-devel cairo-devel
```

## Compilation

Standard build:
```bash
mkdir -p build && cd build
cmake ..
make
```

### Optional Build Flags
You can disable specific format support during configuration:
```bash
cmake -DENABLE_PDF=OFF -DENABLE_SVG=OFF ..
```

## Usage
```bash
./build/icat path/to/image.png
./build/icat path/to/vector.svg
./build/icat path/to/document.pdf
```

## How it works
1. **Standard Images**: Uses `stb_image` to load PNG, JPG, BMP, etc.
2. **SVG**: Renders via `librsvg` to a Cairo ARGB surface (with white background).
3. **PDF/EPS**: Renders the first page via `poppler-cpp` at 300 DPI.
4. **Output**: Encodes as PNG (via `stb_image_write`), base64 encodes the data, and prints the appropriate terminal escape sequence (iTerm2 or Kitty protocols).
