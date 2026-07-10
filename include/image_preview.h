#ifndef IMAGE_PREVIEW_H
#define IMAGE_PREVIEW_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ImagePreview {

struct PerfRecord {
    std::string name;
    long long duration_ms;
};

extern bool perf_enabled;
extern std::vector<PerfRecord> perf_records;
extern std::mutex perf_mutex;

std::string base64_encode(const unsigned char* bytes_to_encode, unsigned int in_len);

void render_and_display(unsigned char* img, int width, int height, int channels, bool free_img);

// A PDF document that can render individual pages without reopening the file.
// Page numbers in this API are 1-based, matching the CLI and common PDF readers.
class PdfDocument {
public:
    PdfDocument();
    ~PdfDocument();

    PdfDocument(PdfDocument&&) noexcept;
    PdfDocument& operator=(PdfDocument&&) noexcept;

    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    bool open(const std::string& filename, std::string& error);
    bool is_open() const;
    int page_count() const;
    bool display_page(int page_number, std::string& error) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool is_pdf_filename(const std::string& filename);

// Backwards-compatible convenience function. PDFs are displayed at page 1.
void display_image(const std::string& filename);

} // namespace ImagePreview

#endif // IMAGE_PREVIEW_H
