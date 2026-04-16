// Accelerate/vImage – SIMD-accelerated image resize on Apple platforms.
// Replaces the Metal MPS approach: per-process GPU pipeline init (~200-400ms)
// makes Metal unsuitable for a CLI tool that exits after one image.
// vImage (part of Accelerate) uses AMX/NEON SIMD with zero startup cost.
#include <Accelerate/Accelerate.h>
#include <vector>

namespace ImagePreview {

std::vector<unsigned char> resize_pixels_metal(
    const unsigned char* src, int src_w, int src_h, int ch,
    int dst_w, int dst_h)
{
    std::vector<unsigned char> dst(dst_w * dst_h * ch);

    if (ch == 1) {
        vImage_Buffer s = { const_cast<void*>(static_cast<const void*>(src)),
                            (vImagePixelCount)src_h, (vImagePixelCount)src_w,
                            (size_t)src_w };
        vImage_Buffer d = { dst.data(),
                            (vImagePixelCount)dst_h, (vImagePixelCount)dst_w,
                            (size_t)dst_w };
        vImageScale_Planar8(&s, &d, NULL, kvImageNoFlags);
        return dst;
    }

    if (ch == 4) {
        // vImageScale_ARGB8888 works for any 4-byte-per-pixel format (including RGBA)
        vImage_Buffer s = { const_cast<void*>(static_cast<const void*>(src)),
                            (vImagePixelCount)src_h, (vImagePixelCount)src_w,
                            (size_t)(src_w * 4) };
        vImage_Buffer d = { dst.data(),
                            (vImagePixelCount)dst_h, (vImagePixelCount)dst_w,
                            (size_t)(dst_w * 4) };
        vImageScale_ARGB8888(&s, &d, NULL, kvImageNoFlags);
        return dst;
    }

    if (ch == 3) {
        // vImage has no native 24bpp scaler; convert RGB→ARGB, scale, strip alpha
        std::vector<unsigned char> argb_src(src_w * src_h * 4);
        vImage_Buffer srcBuf  = { const_cast<void*>(static_cast<const void*>(src)),
                                  (vImagePixelCount)src_h, (vImagePixelCount)src_w,
                                  (size_t)(src_w * 3) };
        vImage_Buffer argbBuf = { argb_src.data(),
                                  (vImagePixelCount)src_h, (vImagePixelCount)src_w,
                                  (size_t)(src_w * 4) };
        vImageConvert_RGB888toARGB8888(&srcBuf, NULL, 255, &argbBuf, false, kvImageNoFlags);

        std::vector<unsigned char> argb_dst(dst_w * dst_h * 4);
        vImage_Buffer argbSrc = { argb_src.data(),
                                  (vImagePixelCount)src_h, (vImagePixelCount)src_w,
                                  (size_t)(src_w * 4) };
        vImage_Buffer argbDst = { argb_dst.data(),
                                  (vImagePixelCount)dst_h, (vImagePixelCount)dst_w,
                                  (size_t)(dst_w * 4) };
        vImageScale_ARGB8888(&argbSrc, &argbDst, NULL, kvImageNoFlags);

        vImage_Buffer dstBuf  = { dst.data(),
                                  (vImagePixelCount)dst_h, (vImagePixelCount)dst_w,
                                  (size_t)(dst_w * 3) };
        vImageConvert_ARGB8888toRGB888(&argbDst, &dstBuf, kvImageNoFlags);
        return dst;
    }

    if (ch == 2) {
        // Grayscale+Alpha: deinterleave and scale each plane separately
        std::vector<unsigned char> c0s(src_w * src_h), c1s(src_w * src_h);
        for (int i = 0; i < src_w * src_h; ++i) { c0s[i] = src[i*2]; c1s[i] = src[i*2+1]; }
        std::vector<unsigned char> c0d(dst_w * dst_h), c1d(dst_w * dst_h);
        vImage_Buffer s0 = { c0s.data(), (vImagePixelCount)src_h, (vImagePixelCount)src_w, (size_t)src_w };
        vImage_Buffer s1 = { c1s.data(), (vImagePixelCount)src_h, (vImagePixelCount)src_w, (size_t)src_w };
        vImage_Buffer d0 = { c0d.data(), (vImagePixelCount)dst_h, (vImagePixelCount)dst_w, (size_t)dst_w };
        vImage_Buffer d1 = { c1d.data(), (vImagePixelCount)dst_h, (vImagePixelCount)dst_w, (size_t)dst_w };
        vImageScale_Planar8(&s0, &d0, NULL, kvImageNoFlags);
        vImageScale_Planar8(&s1, &d1, NULL, kvImageNoFlags);
        for (int i = 0; i < dst_w * dst_h; ++i) { dst[i*2] = c0d[i]; dst[i*2+1] = c1d[i]; }
        return dst;
    }

    return dst; // unreachable for valid channel counts
}

} // namespace ImagePreview

