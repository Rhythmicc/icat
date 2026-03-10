#include <iostream>
#include <string>
#include "image_preview.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [--perf] <filename>" << std::endl;
        return 1;
    }

    std::string filename;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--perf") {
            ImagePreview::perf_enabled = true;
        } else {
            filename = arg;
        }
    }

    if (filename.empty()) {
        std::cerr << "Error: No filename provided." << std::endl;
        return 1;
    }

    ImagePreview::display_image(filename);

    if (ImagePreview::perf_enabled) {
        std::cerr << "\n--- Performance Metrics ---\n";
        for (const auto& record : ImagePreview::perf_records) {
            std::cerr << "[Perf] " << record.name << ": " << record.duration_ms << "ms\n";
        }
        std::cerr << "---------------------------\n";
    }

    return 0;
}
