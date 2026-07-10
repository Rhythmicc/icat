#ifndef ICAT_CLI_H
#define ICAT_CLI_H

#include <string>

namespace IcatCli {

struct Options {
    bool perf = false;
    bool tui = false;
    bool help = false;
    bool page_explicit = false;
    int page = 1;
    std::string filename;
};

enum class Navigation {
    Previous,
    Next,
    First,
    Last,
};

bool parse_options(int argc, const char* const argv[], Options& options,
                   std::string& error);
int navigate_page(int current_page, int page_count, Navigation navigation);
std::string usage(const std::string& program_name);
int run(const Options& options);

} // namespace IcatCli

#endif // ICAT_CLI_H
