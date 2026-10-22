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
