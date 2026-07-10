#include <iostream>
#include <string>
#include "icat_cli.h"
#include "image_preview.h"

int main(int argc, char** argv) {
    IcatCli::Options options;
    std::string error;
    if (!IcatCli::parse_options(argc, argv, options, error)) {
        std::cerr << "Error: " << error << "\n\n"
                  << IcatCli::usage(argv[0]) << std::endl;
        return 1;
    }
    if (options.help) {
        std::cout << IcatCli::usage(argv[0]) << std::endl;
        return 0;
    }

    int status = IcatCli::run(options);

    if (ImagePreview::perf_enabled) {
        std::cerr << "\n--- Performance Metrics ---\n";
        for (const auto& record : ImagePreview::perf_records) {
            std::cerr << "[Perf] " << record.name << ": " << record.duration_ms << "ms\n";
        }
        std::cerr << "---------------------------\n";
    }

    return status;
}
