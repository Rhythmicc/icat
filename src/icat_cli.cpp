#include "icat_cli.h"

#include "image_preview.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace IcatCli {
namespace {

volatile std::sig_atomic_t terminal_resized = 0;
volatile std::sig_atomic_t interrupted = 0;

void handle_resize(int) {
    terminal_resized = 1;
}

void handle_interrupt(int) {
    interrupted = 1;
}

bool parse_positive_integer(const std::string& text, int& value) {
    if (text.empty()) return false;
    int parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end || parsed < 1) return false;
    value = parsed;
    return true;
}

class TerminalSession {
public:
    bool start(std::string& error) {
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
            error = "--tui requires an interactive terminal on both stdin and stdout.";
            return false;
        }
        if (tcgetattr(STDIN_FILENO, &original_) != 0) {
            error = "Failed to read terminal settings: " + std::string(std::strerror(errno));
            return false;
        }

        struct termios raw = original_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            error = "Failed to enable terminal input mode: " +
                    std::string(std::strerror(errno));
            return false;
        }
        active_ = true;

        install_signal(SIGWINCH, handle_resize, old_winch_);
        install_signal(SIGINT, handle_interrupt, old_int_);
        install_signal(SIGTERM, handle_interrupt, old_term_);

        std::cout << "\033[?1049h\033[?25l" << std::flush;
        return true;
    }

    ~TerminalSession() {
        if (!active_) return;
        std::cout << "\033[2J\033[H\033[?25h\033[?1049l" << std::flush;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
        sigaction(SIGWINCH, &old_winch_, nullptr);
        sigaction(SIGINT, &old_int_, nullptr);
        sigaction(SIGTERM, &old_term_, nullptr);
    }

private:
    static void install_signal(int signal_number, void (*handler)(int),
                               struct sigaction& old_action) {
        struct sigaction action {};
        action.sa_handler = handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        sigaction(signal_number, &action, &old_action);
    }

    bool active_ = false;
    struct termios original_ {};
    struct sigaction old_winch_ {};
    struct sigaction old_int_ {};
    struct sigaction old_term_ {};
};

enum class InputAction {
    None,
    Previous,
    Next,
    First,
    Last,
    Quit,
};

class KeyDecoder {
public:
    InputAction feed(char key) {
        if (state_ == State::Escape) {
            if (key == '[') {
                state_ = State::Csi;
                csi_.clear();
            } else {
                state_ = State::Normal;
            }
            return InputAction::None;
        }
        if (state_ == State::Csi) {
            csi_ += key;
            if (key < '@' || key > '~') return InputAction::None;
            state_ = State::Normal;
            if (key == 'D') return InputAction::Previous;
            if (key == 'C') return InputAction::Next;
            if (key == 'H' || csi_ == "1~" || csi_ == "7~") return InputAction::First;
            if (key == 'F' || csi_ == "4~" || csi_ == "8~") return InputAction::Last;
            return InputAction::None;
        }
        if (key == '\033') {
            state_ = State::Escape;
            return InputAction::None;
        }

        switch (key) {
            case 'h':
            case 'k':
            case 'p': return InputAction::Previous;
            case 'l':
            case 'j':
            case 'n':
            case ' ': return InputAction::Next;
            case 'g': return InputAction::First;
            case 'G': return InputAction::Last;
            case 'q':
            case 'Q': return InputAction::Quit;
            default: return InputAction::None;
        }
    }

private:
    enum class State { Normal, Escape, Csi };
    State state_ = State::Normal;
    std::string csi_;
};

bool redraw(ImagePreview::PdfDocument& document, int page, std::string& error) {
    std::cout << "\033[2J\033[H" << std::flush;
    if (!document.display_page(page, error)) return false;
    std::cout << "\033[7m PDF " << page << '/' << document.page_count()
              << "  [h/left] previous  [l/right/space] next  [g/G] first/last  [q] quit "
              << "\033[0m\n" << std::flush;
    return true;
}

int run_pdf_tui(ImagePreview::PdfDocument& document, int initial_page) {
    std::string error;
    TerminalSession terminal;
    if (!terminal.start(error)) {
        std::cerr << "Error: " << error << std::endl;
        return 1;
    }

    terminal_resized = 1;
    interrupted = 0;
    int current_page = initial_page;
    KeyDecoder decoder;

    while (!interrupted) {
        if (terminal_resized) {
            terminal_resized = 0;
            if (!redraw(document, current_page, error)) {
                std::cerr << "Error: " << error << std::endl;
                return 1;
            }
        }

        struct pollfd input {STDIN_FILENO, POLLIN, 0};
        int ready = poll(&input, 1, 250);
        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "Error: Failed to read terminal input: "
                      << std::strerror(errno) << std::endl;
            return 1;
        }
        if (ready == 0 || !(input.revents & POLLIN)) continue;

        char buffer[32];
        ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count <= 0) continue;
        for (ssize_t i = 0; i < count; ++i) {
            InputAction action = decoder.feed(buffer[i]);
            if (action == InputAction::Quit) return 0;

            int next_page = current_page;
            switch (action) {
                case InputAction::Previous:
                    next_page = navigate_page(current_page, document.page_count(),
                                              Navigation::Previous);
                    break;
                case InputAction::Next:
                    next_page = navigate_page(current_page, document.page_count(),
                                              Navigation::Next);
                    break;
                case InputAction::First:
                    next_page = navigate_page(current_page, document.page_count(),
                                              Navigation::First);
                    break;
                case InputAction::Last:
                    next_page = navigate_page(current_page, document.page_count(),
                                              Navigation::Last);
                    break;
                default: break;
            }
            if (next_page != current_page) {
                current_page = next_page;
                terminal_resized = 1;
            }
        }
    }
    return 0;
}

} // namespace

bool parse_options(int argc, const char* const argv[], Options& options,
                   std::string& error) {
    options = Options{};
    error.clear();
    bool positional_only = false;

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (!positional_only && argument == "--") {
            positional_only = true;
        } else if (!positional_only && (argument == "--help" || argument == "-h")) {
            options.help = true;
        } else if (!positional_only && argument == "--perf") {
            options.perf = true;
        } else if (!positional_only && argument == "--tui") {
            options.tui = true;
        } else if (!positional_only && (argument == "--page" || argument.rfind("--page=", 0) == 0)) {
            std::string value;
            if (argument == "--page") {
                if (++i >= argc) {
                    error = "--page requires a positive page number.";
                    return false;
                }
                value = argv[i];
            } else {
                value = argument.substr(7);
            }
            if (!parse_positive_integer(value, options.page)) {
                error = "Invalid page number '" + value + "'; expected a positive integer.";
                return false;
            }
            options.page_explicit = true;
        } else if (!positional_only && !argument.empty() && argument[0] == '-') {
            error = "Unknown option: " + argument;
            return false;
        } else if (!options.filename.empty()) {
            error = "Only one input file can be displayed at a time.";
            return false;
        } else {
            options.filename = argument;
        }
    }

    if (!options.help && options.filename.empty()) {
        error = "No filename provided.";
        return false;
    }
    return true;
}

int navigate_page(int current_page, int page_count, Navigation navigation) {
    if (page_count < 1) return 0;
    current_page = std::max(1, std::min(current_page, page_count));
    switch (navigation) {
        case Navigation::Previous: return std::max(1, current_page - 1);
        case Navigation::Next: return std::min(page_count, current_page + 1);
        case Navigation::First: return 1;
        case Navigation::Last: return page_count;
    }
    return current_page;
}

std::string usage(const std::string& program_name) {
    return "Usage: " + program_name + " [--perf] [--page N] [--tui] <filename>\n"
           "\n"
           "  --page N  Display PDF page N (1-based)\n"
           "  --tui     Open a local PDF in interactive reading mode\n"
           "  --perf    Print performance metrics\n"
           "  -h, --help  Show this help";
}

int run(const Options& options) {
    ImagePreview::perf_enabled = options.perf;

    const bool pdf = ImagePreview::is_pdf_filename(options.filename);
    const bool remote = options.filename.find("://") != std::string::npos;
    if (options.tui && (!pdf || remote)) {
        std::cerr << "Error: --tui is only available for local PDF files." << std::endl;
        return 1;
    }
    if (options.page_explicit && (!pdf || remote)) {
        std::cerr << "Error: --page is only available for local PDF files." << std::endl;
        return 1;
    }

    if (!pdf || remote) {
        ImagePreview::display_image(options.filename);
        return 0;
    }

    ImagePreview::PdfDocument document;
    std::string error;
    if (!document.open(options.filename, error)) {
        std::cerr << "Error: " << error << std::endl;
        return 1;
    }
    if (options.page > document.page_count()) {
        std::cerr << "Error: Page " << options.page << " is outside the valid range 1-"
                  << document.page_count() << "." << std::endl;
        return 1;
    }

    if (options.tui) return run_pdf_tui(document, options.page);
    if (!document.display_page(options.page, error)) {
        std::cerr << "Error: " << error << std::endl;
        return 1;
    }
    return 0;
}

} // namespace IcatCli
