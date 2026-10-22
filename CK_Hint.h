#pragma once

// Turns an AE mask shape into CorridorKey's first-frame hint: a grayscale
// PNG at source resolution, white = subject. Portable, no AE dependencies.

#include <string>
#include <vector>

struct CK_MaskVertex
{
    double x, y;            // vertex, full-res layer pixels
    double in_x, in_y;      // absolute incoming control point
    double out_x, out_y;    // absolute outgoing control point
};

// Anti-aliased fill (nonzero winding) of a closed Bezier shape.
// `coverage` gets width*height bytes, 0..255, row 0 = top.
void CK_RasterizeMask(const std::vector<CK_MaskVertex>& verts,
                      int width, int height,
                      std::vector<unsigned char>& coverage);

// Encodes 8-bit grayscale pixels as PNG bytes. Empty on failure.
std::string CK_EncodeGrayPng(const std::vector<unsigned char>& pixels,
                             int width, int height);

// ---- Hint Layer --------------------------------------------------------

enum CK_PixelKind
{
    CK_PIX_8 = 0,     // PF_Pixel8:     A,R,G,B bytes, 0..255
    CK_PIX_16,        // PF_Pixel16:    A,R,G,B shorts, 0..32768
    CK_PIX_32F        // PF_PixelFloat: A,R,G,B floats, 0..1
};

// Matte from an AE layer buffer, resampled (bilinear) to out_w x out_h.
// use_luma: take luminance instead of alpha (for black/white matte layers).
// Reports whether the source alpha was fully opaque / fully transparent.
void CK_WorldToHint(const void* data, long rowbytes, int w, int h,
                    CK_PixelKind kind, bool use_luma,
                    int out_w, int out_h,
                    std::vector<unsigned char>& out,
                    bool* alpha_fully_opaque, bool* matte_empty);
