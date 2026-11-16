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

enum CK_ScreenColour
{
    CK_SCREEN_GREEN = 0,
    CK_SCREEN_BLUE,
    CK_SCREEN_PICKED
};

enum CK_DespillMethod
{
    CK_DESPILL_AVERAGE = 0,    // key channel limited to the average of the other two (CorridorKey's)
    CK_DESPILL_GENTLE,         // limited to the larger of the other two: keeps yellows/cyans
    CK_DESPILL_STRONG          // limited to the smaller of the other two: removes the most
};

struct CK_RenderSettings
{
    int   mode = CK_MODE_CUTOUT;
    float despill = 0.5f;      // 0..1
    int   screen = CK_SCREEN_GREEN;
    float picked[3] = { 0.0f, 1.0f, 0.0f };   // linear RGB, for CK_SCREEN_PICKED
    int   method = CK_DESPILL_AVERAGE;        // Green/Blue only; Picked always averages
    int   despeckle_px = 0;    // remove matte islands smaller than this many full-res pixels
    bool  keep_original = false;   // plate colour where the matte is solid
    const float* plate = nullptr;  // linear RGB, width*height*3 in OUTPUT pixels
                                   // (Keep Original, and compacted fg pixels)
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
float CK_SRGBToLinear(float v);
