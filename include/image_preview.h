#ifndef IMAGE_PREVIEW_H
#define IMAGE_PREVIEW_H

#include <string>
#include <vector>
#include <mutex>

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

void display_image(const std::string& filename);

} // namespace ImagePreview

#endif // IMAGE_PREVIEW_H
