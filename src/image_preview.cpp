#include "image_preview.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cmath>
#include <omp.h>
#include <chrono>
#include <mutex>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef HAVE_POPPLER
#include <poppler-document.h>
#include <poppler-page.h>
#include <poppler-page-renderer.h>
#include <poppler-image.h>
#endif

#ifdef HAVE_RSVG
#include <librsvg/rsvg.h>
#include <cairo.h>
#endif

using namespace std::chrono;

namespace ImagePreview {

bool perf_enabled = false;
std::vector<PerfRecord> perf_records;
std::mutex perf_mutex;

struct Timer {
    const char* name;
    high_resolution_clock::time_point start;
    Timer(const char* n) : name(n) {
        if (perf_enabled) start = high_resolution_clock::now();
    }
    ~Timer() {
        if (perf_enabled) {
            auto end = high_resolution_clock::now();
            auto dur = duration_cast<milliseconds>(end - start).count();
            std::lock_guard<std::mutex> lock(perf_mutex);
            perf_records.push_back({name, (long long)dur});
        }
    }
};

static const char base64_chars[] = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

std::string base64_encode(const unsigned char* bytes_to_encode, unsigned int in_len) {
    Timer t("Base64 Encoding");
    if (in_len == 0) return "";
    unsigned int out_len = 4 * ((in_len + 2) / 3);
    std::string ret(out_len, '=');

    #pragma omp parallel for
    for (unsigned int i = 0; i < in_len; i += 3) {
        unsigned int j = (i / 3) * 4;
        unsigned int n = std::min(3u, in_len - i);
        uint32_t val = 0;
        for (unsigned int k = 0; k < n; ++k) val = (val << 8) | bytes_to_encode[i + k];
        if (n == 3) {
            ret[j] = base64_chars[(val >> 18) & 0x3f];
            ret[j+1] = base64_chars[(val >> 12) & 0x3f];
            ret[j+2] = base64_chars[(val >> 6) & 0x3f];
            ret[j+3] = base64_chars[val & 0x3f];
        } else if (n == 2) {
            val <<= 8;
            ret[j] = base64_chars[(val >> 16) & 0x3f];
            ret[j+1] = base64_chars[(val >> 10) & 0x3f];
            ret[j+2] = base64_chars[(val >> 4) & 0x3f];
        } else if (n == 1) {
            val <<= 16;
            ret[j] = base64_chars[(val >> 16) & 0x3f];
            ret[j+1] = base64_chars[(val >> 10) & 0x3f];
        }
    }
    return ret;
}

static void my_stbi_write_func(void* context, void* data, int size) {
    std::vector<unsigned char>* vec = (std::vector<unsigned char>*)context;
    unsigned char* p = (unsigned char*)data;
    vec->insert(vec->end(), p, p + size);
}

void render_and_display(unsigned char* img, int width, int height, int channels, bool free_img) {
    if (!img) return;

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int term_cols = w.ws_col;
    int term_rows = w.ws_row;
    int term_px_width = w.ws_xpixel;
    int term_px_height = w.ws_ypixel;

    double px_per_col = (term_px_width > 0 && term_cols > 0) ? (double)term_px_width / term_cols : 10.0;
    double px_per_row = (term_px_height > 0 && term_rows > 0) ? (double)term_px_height / term_rows : 24.0;

    double max_width_px = term_cols * px_per_col;
    double max_height_px = (term_rows > 2 ? term_rows - 2 : term_rows) * px_per_row;

    double scale = 1.0;
    if (width > max_width_px) scale = std::min(scale, max_width_px / width);
    if (height > max_height_px) scale = std::min(scale, max_height_px / height);

    int display_width = (int)(width * scale);
    int display_height = (int)(height * scale);
    int img_cols = (int)std::ceil(display_width / px_per_col);
    int img_rows = (int)std::ceil(display_height / px_per_row);

    int leading_spaces = (term_cols - img_cols) / 2;
    if (leading_spaces > 0) {
        for(int i = 0; i < leading_spaces; ++i) std::cout << " ";
    }

    char* term_env = std::getenv("TERM");
    char* term_prog_env = std::getenv("TERM_PROGRAM");
    std::string term = term_env ? term_env : "";
    std::string term_program = term_prog_env ? term_prog_env : "";

#ifdef FORCE_KITTY_PROTOCOL
    bool is_kitty = true;
#else
    bool is_kitty = (term.find("kitty") != std::string::npos || 
                     term_program.find("ghostty") != std::string::npos || 
                     term_program.find("Ghostty") != std::string::npos);
#endif

    if (is_kitty) {
        unsigned char* raw_data = img;
        std::vector<unsigned char> rgba_buffer;
        int target_channels = channels;
        if (channels != 3 && channels != 4) {
            Timer t("Channel Conv");
            rgba_buffer.resize(width * height * 3);
            #pragma omp parallel for
            for (int i = 0; i < width * height; ++i) {
                rgba_buffer[i*3+0] = img[i*channels+0];
                rgba_buffer[i*3+1] = (channels > 1) ? img[i*channels+1] : img[i*channels+0];
                rgba_buffer[i*3+2] = (channels > 2) ? img[i*channels+2] : img[i*channels+0];
            }
            raw_data = rgba_buffer.data();
            target_channels = 3;
        }

        std::string b64 = base64_encode(raw_data, width * height * target_channels);
        
        Timer t_io("Terminal Output (IO)");
        size_t current_pos = 0;
        size_t total_len = b64.length();
        while (current_pos < total_len) {
            size_t chunk_size = std::min((size_t)4096, total_len - current_pos);
            std::string chunk = b64.substr(current_pos, chunk_size);
            bool is_last = (current_pos + chunk_size >= total_len);
            std::cout << "\033_G";
            if (current_pos == 0) {
                int format = (target_channels == 4) ? 32 : 24;
                std::cout << "a=T,f=" << format << ",s=" << width << ",v=" << height << ",c=" << img_cols << ",r=" << img_rows << ",";
            }
            std::cout << "m=" << (is_last ? "0" : "1") << ";" << chunk << "\033\\";
            current_pos += chunk_size;
        }
        std::cout << "\n";
    } else {
        std::vector<unsigned char> encoded_data;
        if (channels == 4) {
            Timer t("PNG Encoding");
            stbi_write_png_to_func(my_stbi_write_func, &encoded_data, width, height, channels, img, width * channels);
        } else {
            Timer t("JPEG Encoding");
            stbi_write_jpg_to_func(my_stbi_write_func, &encoded_data, width, height, channels, img, 85);
        }
        std::string b64 = base64_encode(encoded_data.data(), encoded_data.size());
        
        Timer t_io("Terminal Output (IO)");
        std::cout << "\033]1337;File=inline=1;size=" << encoded_data.size() 
                  << ";width=" << display_width << "px"
                  << ";height=" << display_height << "px"
                  << ";preserveAspectRatio=1"
                  << ":" << b64 << "\a\n";
    }
    std::cout.flush();

    if (free_img) stbi_image_free(img);
}

static bool ends_with(const std::string& value, const std::string& ending) {
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin(), [](char a, char b) {
        return std::tolower(a) == std::tolower(b);
    });
}

void display_image(const std::string& filename) {
    Timer total("Total Time");

#ifdef HAVE_RSVG
    if (ends_with(filename, ".svg")) {
        Timer t_svg("SVG Processing");
        GError* error = NULL;
        RsvgHandle* handle = rsvg_handle_new_from_file(filename.c_str(), &error);
        if (handle) {
            double width, height;
            if (!rsvg_handle_get_intrinsic_size_in_pixels(handle, &width, &height)) {
                RsvgDimensionData dim;
                rsvg_handle_get_dimensions(handle, &dim);
                width = dim.width;
                height = dim.height;
            }

            int i_width = (int)width;
            int i_height = (int)height;
            if (i_width > 2048 || i_height > 2048) {
                double scale = std::min(2048.0 / i_width, 2048.0 / i_height);
                i_width = (int)(i_width * scale);
                i_height = (int)(i_height * scale);
            }

            cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, i_width, i_height);
            cairo_t* cr = cairo_create(surface);
            cairo_scale(cr, (double)i_width / width, (double)i_height / height);
            
            {
                Timer t_render("SVG Render");
                RsvgRectangle viewport = {0, 0, width, height};
                rsvg_handle_render_document(handle, cr, &viewport, &error);
            }
            
            unsigned char* data = cairo_image_surface_get_data(surface);
            int stride = cairo_image_surface_get_stride(surface);
            std::vector<unsigned char> rgba_data(i_width * i_height * 4);
            {
                Timer t_conv("Pixel Conv");
                #pragma omp parallel for
                for (int y = 0; y < i_height; ++y) {
                    uint32_t* row = (uint32_t*)(data + y * stride);
                    for (int x = 0; x < i_width; ++x) {
                        uint32_t pixel = row[x];
                        rgba_data[(y * i_width + x) * 4 + 0] = (pixel >> 16) & 0xff;
                        rgba_data[(y * i_width + x) * 4 + 1] = (pixel >> 8) & 0xff;
                        rgba_data[(y * i_width + x) * 4 + 2] = (pixel >> 0) & 0xff;
                        rgba_data[(y * i_width + x) * 4 + 3] = (pixel >> 24) & 0xff;
                    }
                }
            }
            render_and_display(rgba_data.data(), i_width, i_height, 4, false);
            cairo_destroy(cr);
            cairo_surface_destroy(surface);
            g_object_unref(handle);
            return;
        }
    }
#endif

#ifdef HAVE_POPPLER
    if (ends_with(filename, ".pdf") || ends_with(filename, ".eps") || ends_with(filename, ".ps")) {
        Timer t_pdf("PDF Processing");
        poppler::document* doc;
        {
            Timer t_load("PDF Load");
            doc = poppler::document::load_from_file(filename);
        }
        if (doc && doc->pages() > 0) {
            poppler::page* p = doc->create_page(0);
            if (p) {
                poppler::image img;
                {
                    Timer t_render("PDF Render");
                    poppler::page_renderer renderer;
                    renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
                    renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);
                    img = renderer.render_page(p, 150.0, 150.0);
                }
                if (img.is_valid()) {
                    int width = img.width();
                    int height = img.height();
                    std::vector<unsigned char> rgba_data(width * height * 4);
                    {
                        Timer t_conv("Pixel Conv");
                        const char* data = img.const_data();
                        int stride = img.bytes_per_row();
                        #pragma omp parallel for
                        for (int y = 0; y < height; ++y) {
                            const uint32_t* row = (const uint32_t*)(data + y * stride);
                            for (int x = 0; x < width; ++x) {
                                uint32_t pixel = row[x];
                                rgba_data[(y * width + x) * 4 + 0] = (pixel >> 16) & 0xff;
                                rgba_data[(y * width + x) * 4 + 1] = (pixel >> 8) & 0xff;
                                rgba_data[(y * width + x) * 4 + 2] = (pixel >> 0) & 0xff;
                                rgba_data[(y * width + x) * 4 + 3] = (pixel >> 24) & 0xff;
                            }
                        }
                    }
                    render_and_display(rgba_data.data(), width, height, 4, false);
                }
                delete p;
            }
            delete doc;
            return;
        }
    }
#endif

    int width, height, channels;
    unsigned char* img;
    {
        Timer t_load("Image Load");
        img = stbi_load(filename.c_str(), &width, &height, &channels, 0);
    }
    if (!img) return;
    render_and_display(img, width, height, channels, true);
}

} // namespace ImagePreview
