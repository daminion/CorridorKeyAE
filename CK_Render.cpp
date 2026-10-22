#include "CK_Render.h"

#include <algorithm>
#include <cmath>

float CK_LinearToSRGB(float v)
{
    if (v < 0.0f) return -CK_LinearToSRGB(-v);
    if (v <= 0.0031308f) return v * 12.92f;
    return 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

namespace
{
    // One separable pass of a square min (erode) or max (dilate) filter.
    void MinMaxPass(const std::vector<float>& src, std::vector<float>& dst,
                    int w, int h, int r, bool horizontal, bool erode)
    {
        dst.resize(src.size());
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                float v = erode ? 1e30f : -1e30f;
                for (int k = -r; k <= r; ++k)
                {
                    int sx = horizontal ? std::min(std::max(x + k, 0), w - 1) : x;
                    int sy = horizontal ? y : std::min(std::max(y + k, 0), h - 1);
                    float s = src[(size_t)sy * w + sx];
                    v = erode ? std::min(v, s) : std::max(v, s);
                }
                dst[(size_t)y * w + x] = v;
            }
        }
    }

    void MinMax(const std::vector<float>& src, std::vector<float>& dst,
                int w, int h, int r, bool erode)
    {
        if (r <= 0) { dst = src; return; }
        std::vector<float> tmp;
        MinMaxPass(src, tmp, w, h, r, true, erode);
        MinMaxPass(tmp, dst, w, h, r, false, erode);
    }

    void Despill(float& r, float& g, float& b, float strength)
    {
        // Same formula as CorridorKey's own despill (runtime.py).
        if (strength <= 0.0f) return;
        float spill = std::max(g - (r + b) * 0.5f, 0.0f);
        float fr = r + spill * 0.5f, fg = g - spill, fb = b + spill * 0.5f;
        if (strength >= 1.0f) { r = fr; g = fg; b = fb; return; }
        r += (fr - r) * strength;
        g += (fg - g) * strength;
        b += (fb - b) * strength;
    }
}

void CK_ProcessMatte(const CK_Frame& frame, const CK_RenderSettings& s,
                     std::vector<float>& matte)
{
    const int w = frame.width, h = frame.height;

    // Choke: fractional radius blends between the two nearest integer radii.
    float c = std::min(std::max(s.choke_px, -50.0f), 50.0f);
    float rad = std::fabs(c);
    int n = (int)std::floor(rad);
    float f = rad - (float)n;
    bool erode = c > 0.0f;

    if (rad < 1e-4f)
        matte = frame.alpha;
    else
    {
        std::vector<float> lo, hi;
        MinMax(frame.alpha, lo, w, h, n, erode);
        if (f > 1e-4f)
        {
            MinMax(frame.alpha, hi, w, h, n + 1, erode);
            for (size_t i = 0; i < lo.size(); ++i)
                lo[i] += (hi[i] - lo[i]) * f;
        }
        matte.swap(lo);
    }

    const float inv_g = 1.0f / std::max(s.gamma, 0.01f);
    const bool do_gamma = std::fabs(s.gamma - 1.0f) > 1e-4f;
    for (float& a : matte)
    {
        a = std::min(std::max(a, 0.0f), 1.0f);
        if (do_gamma && a > 0.0f && a < 1.0f)
            a = std::pow(a, inv_g);
    }
}

void CK_RenderKey(const CK_Frame& frame,
                  const CK_RenderSettings& s,
                  int width, int height,
                  float* out)
{
    std::vector<float> matte;
    CK_ProcessMatte(frame, s, matte);

    const int fw = frame.width, fh = frame.height;
    const float checker_size = 16.0f / std::max(s.scale_x, 1e-3f);  // stays 16px on screen

    for (int y = 0; y < height; ++y)
    {
        int sy = (int)std::floor(((float)(y + s.origin_y) + 0.5f) * s.scale_y);
        for (int x = 0; x < width; ++x)
        {
            float* o = out + ((size_t)y * width + x) * 4;
            int sx = (int)std::floor(((float)(x + s.origin_x) + 0.5f) * s.scale_x);

            if (sx < 0 || sy < 0 || sx >= fw || sy >= fh)
            {
                o[0] = o[1] = o[2] = o[3] = 0.0f;
                continue;
            }

            const size_t i = (size_t)sy * fw + sx;
            float r = frame.rgb[i * 3 + 0];
            float g = frame.rgb[i * 3 + 1];
            float b = frame.rgb[i * 3 + 2];
            float a = matte[i];

            switch (s.mode)
            {
                case CK_MODE_ALPHA:
                    r = g = b = a;
                    a = 1.0f;
                    break;

                case CK_MODE_FOREGROUND:
                    a = 1.0f;
                    break;

                case CK_MODE_CHECKER:
                {
                    Despill(r, g, b, s.despill);
                    int cx = (int)std::floor((x + s.origin_x) / checker_size);
                    int cy = (int)std::floor((y + s.origin_y) / checker_size);
                    float bg = ((cx + cy) & 1) ? 0.18f : 0.05f;   // linear greys
                    r = r * a + bg * (1.0f - a);
                    g = g * a + bg * (1.0f - a);
                    b = b * a + bg * (1.0f - a);
                    a = 1.0f;
                    break;
                }

                case CK_MODE_CUTOUT:
                default:
                    Despill(r, g, b, s.despill);
                    break;
            }

            if (s.srgb_out && s.mode != CK_MODE_ALPHA)
            {
                r = CK_LinearToSRGB(r);
                g = CK_LinearToSRGB(g);
                b = CK_LinearToSRGB(b);
            }

            o[0] = r; o[1] = g; o[2] = b; o[3] = a;
        }
    }
}
