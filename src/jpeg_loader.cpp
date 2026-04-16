// JPEG DCT prescaling loader.
// The key insight: libjpeg can decode a JPEG at 1/2, 1/4 or 1/8 resolution
// directly in the DCT domain, avoiding full-res decode + software resize.
// This is the same optimization PIL/Pillow uses internally.
#ifdef HAVE_LIBJPEG

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <setjmp.h>
#include <jpeglib.h>

namespace ImagePreview {

struct JpegErrorMgr {
    struct jpeg_error_mgr pub;
    jmp_buf               setjmp_buf;
};

static void jpeg_error_exit_fn(j_common_ptr cinfo) {
    auto* err = reinterpret_cast<JpegErrorMgr*>(cinfo->err);
    longjmp(err->setjmp_buf, 1);
}

// Load a JPEG file with DCT prescaling.
// Decodes at the largest 1/N (N ∈ {1,2,4,8}) that still gives dims >= the
// final display size, so the subsequent vImage resize is a small operation.
// Returns a malloc'd RGB buffer; caller must free() it.
// Returns nullptr on failure (caller should fall back to stb_image).
unsigned char* load_jpeg_prescaled(const char* filename,
                                    int& out_w, int& out_h, int& out_ch,
                                    int max_w, int max_h)
{
    FILE* fp = fopen(filename, "rb");
    if (!fp) return nullptr;

    struct jpeg_decompress_struct cinfo;
    JpegErrorMgr jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit_fn;

    if (setjmp(jerr.setjmp_buf)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return nullptr;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);

    // CMYK/YCCK can't be auto-converted to RGB by libjpeg – fall back to stb
    if (cinfo.jpeg_color_space == JCS_CMYK ||
        cinfo.jpeg_color_space == JCS_YCCK) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return nullptr;
    }

    const int full_w = (int)cinfo.image_width;
    const int full_h = (int)cinfo.image_height;

    // Determine prescale denominator: largest N in {8,4,2,1} such that
    //   decoded_w >= display_w  AND  decoded_h >= display_h
    // i.e. N <= min(full_w/display_w, full_h/display_h)
    // This ensures the post-decode vImage resize is always a downscale.
    int best_denom = 1;
    if (max_w > 0 && max_h > 0 && (full_w > max_w || full_h > max_h)) {
        double inv_scale = std::max((double)full_w / max_w,
                                    (double)full_h / max_h);
        for (int d : {8, 4, 2}) {
            if ((double)d <= inv_scale) { best_denom = d; break; }
        }
    }

    cinfo.scale_num       = 1;
    cinfo.scale_denom     = (unsigned int)best_denom;
    cinfo.out_color_space = JCS_RGB;   // libjpeg upconverts grayscale→RGB too

    jpeg_start_decompress(&cinfo);

    out_w  = (int)cinfo.output_width;
    out_h  = (int)cinfo.output_height;
    out_ch = (int)cinfo.output_components;  // always 3 with JCS_RGB

    auto* buf = static_cast<unsigned char*>(
        malloc((size_t)out_w * out_h * out_ch));
    if (!buf) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return nullptr;
    }

    while ((int)cinfo.output_scanline < out_h) {
        unsigned char* row = buf + (size_t)cinfo.output_scanline * out_w * out_ch;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    return buf;
}

} // namespace ImagePreview

#endif // HAVE_LIBJPEG
