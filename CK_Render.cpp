#include "CK_Render.h"

#include <algorithm>
#include <cmath>

float CK_LinearToSRGB(float v)
{
    if (v < 0.0f) return -CK_LinearToSRGB(-v);
    if (v <= 0.0031308f) return v * 12.92f;
    return 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

float CK_SRGBToLinear(float v)
{
    if (v < 0.0f) return -CK_SRGBToLinear(-v);
    if (v <= 0.04045f) return v / 12.92f;
    return std::pow((v + 0.055f) / 1.055f, 2.4f);
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

    // Moves the screen colour's excess out of the pixel, keeping brightness
    // by handing half of it to each of the other two channels. Average on a
    // green screen is exactly CorridorKey's own despill (runtime.py).
    void DespillPixel(float& r, float& g, float& b, const CK_RenderSettings& s)
    {
        const float strength = s.despill;
        if (strength <= 0.0f) return;
        float c[3] = { r, g, b };
        float f[3] = { r, g, b };

        if (s.screen == CK_SCREEN_PICKED)
        {
            // Chroma direction of the picked colour; projecting onto it and
            // removing 1.5x reproduces the Average formula for pure screens.
            const float mean = (s.picked[0] + s.picked[1] + s.picked[2]) / 3.0f;
            const float k[3] = { s.picked[0] - mean, s.picked[1] - mean, s.picked[2] - mean };
            const float kk = k[0] * k[0] + k[1] * k[1] + k[2] * k[2];
            if (kk < 1e-8f) return;
            const float amt = std::max((c[0] * k[0] + c[1] * k[1] + c[2] * k[2]) / kk, 0.0f);
            for (int i = 0; i < 3; ++i)
                f[i] = c[i] - 1.5f * amt * k[i];
        }
        else
        {
            const int key = s.screen == CK_SCREEN_BLUE ? 2 : 1;
            const int o1 = 0, o2 = key == 1 ? 2 : 1;   // the two non-screen channels
            float limit;
            switch (s.method)
            {
                case CK_DESPILL_GENTLE: limit = std::max(c[o1], c[o2]); break;
                case CK_DESPILL_STRONG: limit = std::min(c[o1], c[o2]); break;
                default:                limit = (c[o1] + c[o2]) * 0.5f; break;
            }
            const float spill = std::max(c[key] - limit, 0.0f);
            f[key] = c[key] - spill;
            f[o1] = c[o1] + spill * 0.5f;
            f[o2] = c[o2] + spill * 0.5f;
        }

        const float t = std::min(strength, 1.0f);
        r = c[0] + (f[0] - c[0]) * t;
        g = c[1] + (f[1] - c[1]) * t;
        b = c[2] + (f[2] - c[2]) * t;
    }

    // Zeroes 8-connected islands of the matte (alpha > 0.05) smaller than
    // min_px pixels: specks of screen noise, tracking markers, dust.
    void Despeckle(std::vector<float>& a, int w, int h, int min_px)
    {
        if (min_px <= 0) return;
        std::vector<unsigned char> seen(a.size(), 0);
        std::vector<int> stack, island;
        for (int start = 0; start < w * h; ++start)
        {
            if (seen[start] || a[start] <= 0.05f) continue;
            island.clear();
            stack.assign(1, start);
            seen[start] = 1;
            bool big = false;
            while (!stack.empty())
            {
                const int i = stack.back();
                stack.pop_back();
                if (!big)
                {
                    island.push_back(i);
                    if ((int)island.size() >= min_px) { big = true; island.clear(); }
                }
                const int x = i % w, y = i / w;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int nx = x + dx, ny = y + dy;
                        if ((dx | dy) == 0 || nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        const int j = ny * w + nx;
                        if (!seen[j] && a[j] > 0.05f) { seen[j] = 1; stack.push_back(j); }
                    }
            }
            if (!big)
                for (int i : island) a[i] = 0.0f;
        }
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

    std::vector<float> base = frame.alpha;
    Despeckle(base, w, h, s.despeckle_px);

    if (rad < 1e-4f)
        matte.swap(base);
    else
    {
        std::vector<float> lo, hi;
        MinMax(base, lo, w, h, n, erode);
        if (f > 1e-4f)
        {
            MinMax(base, hi, w, h, n + 1, erode);
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

            // Compact Keys removed this fg pixel (solid or far from the
            // matte): the plate stands in, as Keep Original would show.
            if (CK_IsMissing(r))
            {
                if (s.plate)
                {
                    const float* p = s.plate + ((size_t)y * width + x) * 3;
                    r = p[0]; g = p[1]; b = p[2];
                }
                else
                    r = g = b = 0.0f;
            }

            // Keep Original: in solid areas use the plate's own colour (grain,
            // texture, full detail); fades in from alpha 0.90 to 0.98.
            if (s.keep_original && s.plate && s.mode != CK_MODE_ALPHA)
            {
                const float k = std::min(std::max((a - 0.90f) / 0.08f, 0.0f), 1.0f);
                if (k > 0.0f)
                {
                    const float* p = s.plate + ((size_t)y * width + x) * 3;
                    r += (p[0] - r) * k;
                    g += (p[1] - g) * k;
                    b += (p[2] - b) * k;
                }
            }

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
                    DespillPixel(r, g, b, s);
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
                    DespillPixel(r, g, b, s);
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
