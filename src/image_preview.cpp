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
#include <cctype>
#include <limits>

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

#ifdef HAVE_METAL
// vImage is a pure-C API – safe to include in a .cpp file when Accelerate is linked
#include <Accelerate/Accelerate.h>
#endif

#ifdef HAVE_OPENCV
#include <opencv2/imgproc.hpp>
#ifdef HAVE_OPENCV_CUDA
#include <opencv2/cudawarping.hpp>
#endif
#endif

#ifdef HAVE_CURL
#include <curl/curl.h>
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

static void get_terminal_max_pixels(double& max_width_px, double& max_height_px,
                                     double& px_per_col, double& px_per_row) {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int term_cols = w.ws_col > 0 ? w.ws_col : 80;
    int term_rows = w.ws_row > 0 ? w.ws_row : 24;
    int term_px_width = w.ws_xpixel;
    int term_px_height = w.ws_ypixel;
    px_per_col = (term_px_width > 0 && term_cols > 0) ? (double)term_px_width / term_cols : 10.0;
    px_per_row = (term_px_height > 0 && term_rows > 0) ? (double)term_px_height / term_rows : 24.0;
    max_width_px = term_cols * px_per_col;
    max_height_px = (term_rows > 2 ? term_rows - 2 : term_rows) * px_per_row;
}

#ifdef HAVE_METAL
// Defined in src/resize_metal.mm
extern std::vector<unsigned char> resize_pixels_metal(
    const unsigned char* src, int src_w, int src_h, int ch,
    int dst_w, int dst_h);
#endif

// Pixel count threshold for routing to GPU-accelerated resize on non-Apple platforms.
// Not used on macOS (vImage has no per-process init overhead).
static constexpr int RESIZE_GPU_THRESHOLD = 4000 * 3000;

// CPU area-averaging fallback – good quality, used for small images or when GPU unavailable
static std::vector<unsigned char> resize_pixels_cpu(
    const unsigned char* src, int src_w, int src_h, int ch,
    int dst_w, int dst_h)
{
    std::vector<unsigned char> dst(dst_w * dst_h * ch);
    double scale_x = (double)src_w / dst_w;
    double scale_y = (double)src_h / dst_h;
    #pragma omp parallel for
    for (int dy = 0; dy < dst_h; ++dy) {
        double sy0 = dy * scale_y;
        double sy1 = sy0 + scale_y;
        int iy0 = (int)sy0;
        int iy1 = std::min((int)std::ceil(sy1), src_h) - 1;
        for (int dx = 0; dx < dst_w; ++dx) {
            double sx0 = dx * scale_x;
            double sx1 = sx0 + scale_x;
            int ix0 = (int)sx0;
            int ix1 = std::min((int)std::ceil(sx1), src_w) - 1;
            for (int c = 0; c < ch; ++c) {
                double sum = 0.0, weight = 0.0;
                for (int sy = iy0; sy <= iy1; ++sy) {
                    double yw = std::min((double)(sy + 1), sy1) - std::max((double)sy, sy0);
                    for (int sx = ix0; sx <= ix1; ++sx) {
                        double xw = std::min((double)(sx + 1), sx1) - std::max((double)sx, sx0);
                        sum += src[(sy * src_w + sx) * ch + c] * xw * yw;
                        weight += xw * yw;
                    }
                }
                dst[(dy * dst_w + dx) * ch + c] =
                    weight > 0.0 ? (unsigned char)(sum / weight + 0.5) : 0;
            }
        }
    }
    return dst;
}

// Dispatcher: routes to accelerated (vImage / OpenCV CUDA / OpenCV CPU) or built-in CPU
static std::vector<unsigned char> resize_pixels(
    const unsigned char* src, int src_w, int src_h, int ch,
    int dst_w, int dst_h)
{
#ifdef HAVE_METAL
    // vImage (Accelerate) has no per-process init overhead – always prefer it on Apple platforms
    {
        Timer t("Image Resize (vImage)");
        auto result = resize_pixels_metal(src, src_w, src_h, ch, dst_w, dst_h);
        if (!result.empty()) return result;
    }
#elif defined(HAVE_OPENCV)
    if (src_w * src_h > RESIZE_GPU_THRESHOLD) {
#ifdef HAVE_OPENCV_CUDA
        try {
            Timer t("Image Resize (CUDA)");
            cv::Mat src_mat(src_h, src_w, CV_8UC(ch), const_cast<unsigned char*>(src));
            cv::cuda::GpuMat gpu_src, gpu_dst;
            gpu_src.upload(src_mat);
            int interp = (dst_w < src_w) ? cv::INTER_AREA : cv::INTER_LINEAR;
            cv::cuda::resize(gpu_src, gpu_dst, cv::Size(dst_w, dst_h), 0, 0, interp);
            cv::Mat dst_mat;
            gpu_dst.download(dst_mat);
            return std::vector<unsigned char>(dst_mat.data,
                                             dst_mat.data + dst_w * dst_h * ch);
        } catch (...) {}
        // Fall through to OpenCV CPU on CUDA error
#endif
        {
            Timer t("Image Resize (OpenCV)");
            cv::Mat src_mat(src_h, src_w, CV_8UC(ch), const_cast<unsigned char*>(src));
            cv::Mat dst_mat;
            int interp = (dst_w < src_w) ? cv::INTER_AREA : cv::INTER_LINEAR;
            cv::resize(src_mat, dst_mat, cv::Size(dst_w, dst_h), 0, 0, interp);
            return std::vector<unsigned char>(dst_mat.data,
                                             dst_mat.data + dst_w * dst_h * ch);
        }
    }
#endif
    Timer t("Image Resize (CPU)");
    return resize_pixels_cpu(src, src_w, src_h, ch, dst_w, dst_h);
}

void render_and_display(unsigned char* img, int width, int height, int channels, bool free_img) {
    if (!img) return;

    double max_width_px, max_height_px, px_per_col, px_per_row;
    get_terminal_max_pixels(max_width_px, max_height_px, px_per_col, px_per_row);
    int term_cols = (int)(max_width_px / px_per_col);

    double scale = 1.0;
    if (width > max_width_px) scale = std::min(scale, max_width_px / width);
    if (height > max_height_px) scale = std::min(scale, max_height_px / height);

    int display_width = std::max(1, (int)(width * scale));
    int display_height = std::max(1, (int)(height * scale));
    int img_cols = (int)std::ceil((double)display_width / px_per_col);
    int img_rows = (int)std::ceil((double)display_height / px_per_row);

    int leading_spaces = std::max(0, (term_cols - img_cols) / 2);

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

    // Resize image to actual display pixel dimensions to reduce transmitted data
    std::vector<unsigned char> resized_buf;
    const unsigned char* src_ptr = img;
    int src_w = width, src_h = height;
    if (display_width != width || display_height != height) {
        Timer t("Image Resize");
        resized_buf = resize_pixels(img, width, height, channels, display_width, display_height);
        src_ptr = resized_buf.data();
        src_w = display_width;
        src_h = display_height;
    }

    if (is_kitty) {
        // conv_buf must outlive raw_data – keep it at this scope level
        std::vector<unsigned char> conv_buf;
        const unsigned char* raw_data = src_ptr;
        int target_channels = channels;
        if (channels != 3 && channels != 4) {
            Timer t("Channel Conv");
            conv_buf.resize(src_w * src_h * 3);
            #pragma omp parallel for
            for (int i = 0; i < src_w * src_h; ++i) {
                conv_buf[i*3+0] = src_ptr[i*channels+0];
                conv_buf[i*3+1] = (channels > 1) ? src_ptr[i*channels+1] : src_ptr[i*channels+0];
                conv_buf[i*3+2] = (channels > 2) ? src_ptr[i*channels+2] : src_ptr[i*channels+0];
            }
            raw_data = conv_buf.data();
            target_channels = 3;
        }

        std::string b64 = base64_encode(raw_data, src_w * src_h * target_channels);

        // Build entire escape sequence into one buffer, then write in a single syscall
        const size_t total_len = b64.size();
        const size_t n_chunks  = (total_len + 4095) / 4096;
        std::string out_buf;
        out_buf.reserve(leading_spaces + total_len + n_chunks * 48);
        if (leading_spaces > 0) out_buf.append(leading_spaces, ' ');

        Timer t_io("Terminal Output (IO)");
        size_t pos = 0;
        while (pos < total_len) {
            const size_t n    = std::min((size_t)4096, total_len - pos);
            const bool   last = (pos + n >= total_len);
            if (pos == 0) {
                const int fmt = (target_channels == 4) ? 32 : 24;
                char hdr[128];
                snprintf(hdr, sizeof(hdr), "\033_Ga=T,f=%d,s=%d,v=%d,c=%d,r=%d,m=%c;",
                         fmt, src_w, src_h, img_cols, img_rows, last ? '0' : '1');
                out_buf += hdr;
            } else {
                out_buf += "\033_Gm=";
                out_buf += (last ? '0' : '1');
                out_buf += ';';
            }
            out_buf.append(b64, pos, n);
            out_buf += "\033\\";
            pos += n;
        }
        out_buf += '\n';
        fwrite(out_buf.data(), 1, out_buf.size(), stdout);
    } else {
        std::vector<unsigned char> encoded_data;
        if (channels == 4) {
            Timer t("PNG Encoding");
            stbi_write_png_to_func(my_stbi_write_func, &encoded_data, src_w, src_h, channels, src_ptr, src_w * channels);
        } else {
            Timer t("JPEG Encoding");
            stbi_write_jpg_to_func(my_stbi_write_func, &encoded_data, src_w, src_h, channels, src_ptr, 85);
        }
        std::string b64 = base64_encode(encoded_data.data(), encoded_data.size());

        std::string out_buf;
        out_buf.reserve(leading_spaces + b64.size() + 128);
        if (leading_spaces > 0) out_buf.append(leading_spaces, ' ');
        out_buf += "\033]1337;File=inline=1;size=";
        out_buf += std::to_string(encoded_data.size());
        out_buf += ";width=";
        out_buf += std::to_string(display_width);
        out_buf += "px;height=";
        out_buf += std::to_string(display_height);
        out_buf += "px;preserveAspectRatio=1:";
        out_buf += b64;
        out_buf += "\a\n";

        Timer t_io("Terminal Output (IO)");
        fwrite(out_buf.data(), 1, out_buf.size(), stdout);
    }
    fflush(stdout);

    if (free_img) stbi_image_free(img);
}

static bool ends_with(const std::string& value, const std::string& ending) {
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin(), [](char a, char b) {
        return std::tolower(a) == std::tolower(b);
    });
}

bool is_pdf_filename(const std::string& filename) {
    return ends_with(filename, ".pdf");
}

struct PdfDocument::Impl {
#ifdef HAVE_POPPLER
    std::unique_ptr<poppler::document> document;
#endif
};

PdfDocument::PdfDocument() : impl_(new Impl) {}
PdfDocument::~PdfDocument() = default;
PdfDocument::PdfDocument(PdfDocument&&) noexcept = default;
PdfDocument& PdfDocument::operator=(PdfDocument&&) noexcept = default;

bool PdfDocument::open(const std::string& filename, std::string& error) {
    error.clear();
#ifdef HAVE_POPPLER
    Timer t_load("PDF Load");
    impl_->document.reset(poppler::document::load_from_file(filename));
    if (!impl_->document) {
        error = "Failed to open PDF document: " + filename;
        return false;
    }
    if (impl_->document->pages() <= 0) {
        impl_->document.reset();
        error = "PDF document contains no pages: " + filename;
        return false;
    }
    return true;
#else
    (void)filename;
    error = "This build does not include Poppler, so PDF support is unavailable.";
    return false;
#endif
}

bool PdfDocument::is_open() const {
#ifdef HAVE_POPPLER
    return impl_ && impl_->document != nullptr;
#else
    return false;
#endif
}

int PdfDocument::page_count() const {
#ifdef HAVE_POPPLER
    return is_open() ? impl_->document->pages() : 0;
#else
    return 0;
#endif
}

bool PdfDocument::display_page(int page_number, std::string& error) const {
    error.clear();
#ifdef HAVE_POPPLER
    if (!is_open()) {
        error = "No PDF document is open.";
        return false;
    }
    if (page_number < 1 || page_number > page_count()) {
        error = "Page " + std::to_string(page_number) + " is outside the valid range 1-" +
                std::to_string(page_count()) + ".";
        return false;
    }

    Timer t_pdf("PDF Processing");
    std::unique_ptr<poppler::page> page(impl_->document->create_page(page_number - 1));
    if (!page) {
        error = "Failed to load PDF page " + std::to_string(page_number) + ".";
        return false;
    }

    // Compute an ideal DPI for the current terminal size. This is repeated on
    // each render so TUI redraws react to terminal resizing.
    double max_w_pdf, max_h_pdf, px_per_col_pdf, px_per_row_pdf;
    get_terminal_max_pixels(max_w_pdf, max_h_pdf, px_per_col_pdf, px_per_row_pdf);
    poppler::rectf page_rect = page->page_rect();
    double page_w_pts = page_rect.width();
    double page_h_pts = page_rect.height();
    double dpi_x = (page_w_pts > 0) ? max_w_pdf * 72.0 / page_w_pts : 150.0;
    double dpi_y = (page_h_pts > 0) ? max_h_pdf * 72.0 / page_h_pts : 150.0;
    double dpi = std::max(72.0, std::min({dpi_x, dpi_y, 300.0}));

    poppler::image image;
    {
        Timer t_render("PDF Render");
        poppler::page_renderer renderer;
        renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
        renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);
        image = renderer.render_page(page.get(), dpi, dpi);
    }
    if (!image.is_valid()) {
        error = "Failed to render PDF page " + std::to_string(page_number) + ".";
        return false;
    }

    int width = image.width();
    int height = image.height();
    std::vector<unsigned char> rgba_data(width * height * 4);
    {
        Timer t_conv("Pixel Conv");
        // Poppler ARGB32 has the same byte layout as Cairo ARGB32 (BGRA on LE).
#ifdef HAVE_METAL
        const uint8_t perm[4] = {2, 1, 0, 3}; // BGRA -> RGBA
        vImage_Buffer src_vimg = { (void*)image.const_data(),
            (vImagePixelCount)height, (vImagePixelCount)width,
            (size_t)image.bytes_per_row() };
        vImage_Buffer dst_vimg = { rgba_data.data(),
            (vImagePixelCount)height, (vImagePixelCount)width,
            (size_t)(width * 4) };
        vImagePermuteChannels_ARGB8888(&src_vimg, &dst_vimg, perm, kvImageNoFlags);
#else
        const char* data = image.const_data();
        int stride = image.bytes_per_row();
        #pragma omp parallel for
        for (int y = 0; y < height; ++y) {
            const uint32_t* row = (const uint32_t*)(data + y * stride);
            for (int x = 0; x < width; ++x) {
                uint32_t pixel = row[x];
                rgba_data[(y * width + x) * 4 + 0] = (pixel >> 16) & 0xff;
                rgba_data[(y * width + x) * 4 + 1] = (pixel >> 8)  & 0xff;
                rgba_data[(y * width + x) * 4 + 2] = (pixel >> 0)  & 0xff;
                rgba_data[(y * width + x) * 4 + 3] = (pixel >> 24) & 0xff;
            }
        }
#endif
    }

    render_and_display(rgba_data.data(), width, height, 4, false);
    return true;
#else
    (void)page_number;
    error = "This build does not include Poppler, so PDF support is unavailable.";
    return false;
#endif
}

static bool starts_with_scheme(const std::string& value, const std::string& scheme) {
    if (scheme.size() > value.size()) return false;
    return std::equal(scheme.begin(), scheme.end(), value.begin(), [](char a, char b) {
        return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
    });
}

static bool is_http_url(const std::string& value) {
    return starts_with_scheme(value, "http://") || starts_with_scheme(value, "https://");
}

#ifdef HAVE_CURL
static size_t write_http_response(void* ptr, size_t size, size_t nmemb, void* userdata) {
    constexpr size_t max_download_size = 100 * 1024 * 1024;
    size_t bytes = size * nmemb;
    auto* data = static_cast<std::vector<unsigned char>*>(userdata);
    if (bytes > max_download_size || data->size() > max_download_size - bytes) {
        return 0;
    }
    const auto* begin = static_cast<unsigned char*>(ptr);
    data->insert(data->end(), begin, begin + bytes);
    return bytes;
}

static bool download_url(const std::string& url, std::vector<unsigned char>& data) {
    Timer t("HTTP Download");
    data.clear();

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Error: Failed to initialize libcurl." << std::endl;
        return false;
    }

    char error[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "icat/0.1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_http_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);

    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        std::cerr << "Error: Failed to download URL: "
                  << (error[0] ? error : curl_easy_strerror(result)) << std::endl;
        return false;
    }

    if (status < 200 || status >= 300) {
        std::cerr << "Error: HTTP request failed with status " << status << "." << std::endl;
        return false;
    }

    if (data.empty()) {
        std::cerr << "Error: URL returned an empty response." << std::endl;
        return false;
    }

    return true;
}
#endif

#ifdef HAVE_LIBJPEG
// Defined in src/jpeg_loader.cpp
unsigned char* load_jpeg_prescaled(const char* filename,
                                    int& out_w, int& out_h, int& out_ch,
                                    int max_w, int max_h);
#endif

void display_image(const std::string& filename) {
    Timer total("Total Time");
    bool url_input = is_http_url(filename);

    if (url_input) {
#ifndef HAVE_CURL
        std::cerr << "Error: This build does not include libcurl, so HTTP(S) URLs are not supported." << std::endl;
        return;
#endif
    }

#ifdef HAVE_RSVG
    if (!url_input && ends_with(filename, ".svg")) {
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

            // Scale SVG to fit terminal (up-scale small SVGs, down-scale large ones)
            double max_w_svg, max_h_svg, px_per_col_svg, px_per_row_svg;
            get_terminal_max_pixels(max_w_svg, max_h_svg, px_per_col_svg, px_per_row_svg);
            double svg_scale = std::min(max_w_svg / (width > 0.0 ? width : 1.0),
                                        max_h_svg / (height > 0.0 ? height : 1.0));
            int i_width = std::max(1, (int)(width * svg_scale));
            int i_height = std::max(1, (int)(height * svg_scale));

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
                // Cairo ARGB32 on little-endian = BGRA in memory.
                // vImagePermuteChannels reorders to RGBA with a single SIMD pass.
                // Falls back to a scalar loop on non-Apple builds.
#ifdef HAVE_METAL
                const uint8_t perm[4] = {2, 1, 0, 3}; // BGRA → RGBA
                vImage_Buffer src_vimg = { (void*)data,
                    (vImagePixelCount)i_height, (vImagePixelCount)i_width,
                    (size_t)stride };
                vImage_Buffer dst_vimg = { rgba_data.data(),
                    (vImagePixelCount)i_height, (vImagePixelCount)i_width,
                    (size_t)(i_width * 4) };
                vImagePermuteChannels_ARGB8888(&src_vimg, &dst_vimg, perm, kvImageNoFlags);
#else
                #pragma omp parallel for
                for (int y = 0; y < i_height; ++y) {
                    const uint32_t* row = (const uint32_t*)(data + y * stride);
                    for (int x = 0; x < i_width; ++x) {
                        uint32_t pixel = row[x];
                        rgba_data[(y * i_width + x) * 4 + 0] = (pixel >> 16) & 0xff;
                        rgba_data[(y * i_width + x) * 4 + 1] = (pixel >> 8)  & 0xff;
                        rgba_data[(y * i_width + x) * 4 + 2] = (pixel >> 0)  & 0xff;
                        rgba_data[(y * i_width + x) * 4 + 3] = (pixel >> 24) & 0xff;
                    }
                }
#endif
            }
            render_and_display(rgba_data.data(), i_width, i_height, 4, false);
            cairo_destroy(cr);
            cairo_surface_destroy(surface);
            g_object_unref(handle);
            return;
        }
    }
#endif

    if (!url_input && (ends_with(filename, ".pdf") || ends_with(filename, ".eps") || ends_with(filename, ".ps"))) {
        PdfDocument document;
        std::string error;
        if (!document.open(filename, error) || !document.display_page(1, error)) {
            std::cerr << "Error: " << error << std::endl;
        }
        return;
    }

    int width, height, channels;
    unsigned char* img;
    std::vector<unsigned char> url_data;
    {
        Timer t_load("Image Load");
        img = nullptr;
        if (url_input) {
#ifdef HAVE_CURL
            if (!download_url(filename, url_data)) return;
            if (url_data.size() > (size_t)std::numeric_limits<int>::max()) {
                std::cerr << "Error: Downloaded image is too large to decode." << std::endl;
                return;
            }
            img = stbi_load_from_memory(url_data.data(), (int)url_data.size(),
                                        &width, &height, &channels, 0);
#endif
        } else {
#ifdef HAVE_LIBJPEG
            if (ends_with(filename, ".jpg") || ends_with(filename, ".jpeg")) {
                // Get terminal limits before decode so we can prescale in the DCT domain.
                // This mirrors what PIL/libjpeg does and avoids full-res decode + software resize.
                double max_w_px, max_h_px, ppc, ppr;
                get_terminal_max_pixels(max_w_px, max_h_px, ppc, ppr);
                img = load_jpeg_prescaled(filename.c_str(), width, height, channels,
                                           (int)max_w_px, (int)max_h_px);
            }
#endif
            if (!img)
                img = stbi_load(filename.c_str(), &width, &height, &channels, 0);
        }
    }
    if (!img) {
        std::cerr << "Error: Failed to load image." << std::endl;
        return;
    }
    render_and_display(img, width, height, channels, true);
}

} // namespace ImagePreview
