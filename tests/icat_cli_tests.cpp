#include "icat_cli.h"
#include "image_preview.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_default_options() {
    const char* argv[] = {"icat", "document.pdf"};
    IcatCli::Options options;
    std::string error;
    expect(IcatCli::parse_options(2, argv, options, error), "default options parse");
    expect(options.filename == "document.pdf", "filename is captured");
    expect(options.page == 1, "PDF defaults to page 1");
    expect(!options.page_explicit, "default page is not explicit");
    expect(!options.tui, "TUI defaults to off");
}

void test_pdf_options() {
    const char* argv[] = {"icat", "--perf", "--page", "12", "--tui", "document.pdf"};
    IcatCli::Options options;
    std::string error;
    expect(IcatCli::parse_options(6, argv, options, error), "PDF options parse");
    expect(options.perf, "performance flag is captured");
    expect(options.tui, "TUI flag is captured");
    expect(options.page == 12 && options.page_explicit, "1-based page is captured");

    const char* equals_argv[] = {"icat", "--page=3", "document.pdf"};
    expect(IcatCli::parse_options(3, equals_argv, options, error), "--page=N parses");
    expect(options.page == 3, "--page=N value is captured");
}

void test_invalid_options() {
    IcatCli::Options options;
    std::string error;

    const char* zero[] = {"icat", "--page", "0", "document.pdf"};
    expect(!IcatCli::parse_options(4, zero, options, error), "page zero is rejected");

    const char* text[] = {"icat", "--page=two", "document.pdf"};
    expect(!IcatCli::parse_options(3, text, options, error), "non-numeric page is rejected");

    const char* unknown[] = {"icat", "--unknown", "document.pdf"};
    expect(!IcatCli::parse_options(3, unknown, options, error), "unknown option is rejected");

    const char* multiple[] = {"icat", "one.pdf", "two.pdf"};
    expect(!IcatCli::parse_options(3, multiple, options, error), "multiple inputs are rejected");
}

void test_navigation() {
    using IcatCli::Navigation;
    expect(IcatCli::navigate_page(1, 5, Navigation::Previous) == 1,
           "previous clamps at first page");
    expect(IcatCli::navigate_page(2, 5, Navigation::Previous) == 1,
           "previous decrements page");
    expect(IcatCli::navigate_page(4, 5, Navigation::Next) == 5,
           "next increments page");
    expect(IcatCli::navigate_page(5, 5, Navigation::Next) == 5,
           "next clamps at final page");
    expect(IcatCli::navigate_page(3, 5, Navigation::First) == 1,
           "first jumps to page 1");
    expect(IcatCli::navigate_page(3, 5, Navigation::Last) == 5,
           "last jumps to final page");
    expect(IcatCli::navigate_page(1, 0, Navigation::Next) == 0,
           "empty document navigation is safe");
}

void test_pdf_filename_detection() {
    expect(ImagePreview::is_pdf_filename("document.pdf"), "PDF extension is detected");
    expect(ImagePreview::is_pdf_filename("DOCUMENT.PDF"), "PDF extension is case-insensitive");
    expect(!ImagePreview::is_pdf_filename("document.pdf.png"), "non-PDF suffix is rejected");
}

} // namespace

int main() {
    test_default_options();
    test_pdf_options();
    test_invalid_options();
    test_navigation();
    test_pdf_filename_detection();
    if (failures == 0) std::cout << "All CLI tests passed.\n";
    return failures == 0 ? 0 : 1;
}
