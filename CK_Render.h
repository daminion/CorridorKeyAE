#pragma once

// Pixel processing for the cached key. Portable, no AE dependencies, so it
// can be unit-tested outside After Effects.

#include "CK_Frames.h"

enum CK_OutputMode
{
    CK_MODE_CUTOUT = 1,     // despilled foreground, straight alpha
    CK_MODE_ALPHA,          // matte as greyscale, opaque
    CK_MODE_FOREGROUND,     // raw predicted foreground, opaque
    CK_MODE_CHECKER,        // despilled foreground over a checkerboard
    CK_MODE_ORIGINAL        // caller passes the input through
};

struct CK_RenderSettings
{
    int   mode = CK_MODE_CUTOUT;
    float despill = 0.5f;      // 0..1
    float choke_px = 0.0f;     // full-res pixels; + shrinks, - grows
    float gamma = 1.0f;        // matte gamma; > 1 makes soft edges more opaque
    bool  srgb_out = true;     // encode linear results to sRGB for a non-linear project

    // Output pixel (x, y) shows source pixel ((x + origin_x) * scale_x, (y + origin_y) * scale_y)
    int   origin_x = 0;
    int   origin_y = 0;
    float scale_x = 1.0f;      // 1 / downsample
    float scale_y = 1.0f;
};

// Fills `out` (width*height*4 floats, R,G,B,A straight alpha, row 0 = top).
// Pixels outside the source come out transparent.
void CK_RenderKey(const CK_Frame& frame,
                  const CK_RenderSettings& s,
                  int width, int height,
                  float* out);

// Matte only (choke + gamma applied), full source resolution.
void CK_ProcessMatte(const CK_Frame& frame, const CK_RenderSettings& s,
                     std::vector<float>& matte);

float CK_LinearToSRGB(float v);
