#include "CK_Hint.h"

#include "miniz.h"

#include <algorithm>
#include <cmath>

namespace
{
    struct Pt { double x, y; };

    // Flattens each cubic segment into short straight edges.
    std::vector<Pt> Flatten(const std::vector<CK_MaskVertex>& v)
    {
        std::vector<Pt> poly;
        const size_t n = v.size();
        for (size_t i = 0; i < n; ++i)
        {
            const CK_MaskVertex& a = v[i];
            const CK_MaskVertex& b = v[(i + 1) % n];
            const Pt p0{a.x, a.y}, p1{a.out_x, a.out_y}, p2{b.in_x, b.in_y}, p3{b.x, b.y};

            // Steps from the control polygon length: ~2px per step, 1..256.
            double len = std::hypot(p1.x - p0.x, p1.y - p0.y) +
                         std::hypot(p2.x - p1.x, p2.y - p1.y) +
                         std::hypot(p3.x - p2.x, p3.y - p2.y);
            int steps = std::max(1, std::min(256, (int)(len / 2.0)));

            for (int s = 0; s < steps; ++s)
            {
                double t = (double)s / steps, u = 1.0 - t;
                double w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
                poly.push_back({w0 * p0.x + w1 * p1.x + w2 * p2.x + w3 * p3.x,
                                w0 * p0.y + w1 * p1.y + w2 * p2.y + w3 * p3.y});
            }
        }
        return poly;
    }

    struct Crossing { double x; int dir; };
}

void CK_RasterizeMask(const std::vector<CK_MaskVertex>& verts,
                      int width, int height,
                      std::vector<unsigned char>& coverage)
{
    coverage.assign((size_t)width * height, 0);
    if (verts.size() < 2 || width <= 0 || height <= 0)
        return;

    const std::vector<Pt> poly = Flatten(verts);
    const size_t n = poly.size();
    const int kSub = 4;   // vertical sub-scanlines per pixel; horizontal coverage is exact

    std::vector<float> row(width);
    std::vector<Crossing> xs;

    for (int y = 0; y < height; ++y)
    {
        std::fill(row.begin(), row.end(), 0.0f);

        for (int sub = 0; sub < kSub; ++sub)
        {
            const double sy = y + (sub + 0.5) / kSub;
            xs.clear();
            for (size_t i = 0; i < n; ++i)
            {
                const Pt& a = poly[i];
                const Pt& b = poly[(i + 1) % n];
                if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy))
                {
                    double t = (sy - a.y) / (b.y - a.y);
                    xs.push_back({a.x + t * (b.x - a.x), b.y > a.y ? 1 : -1});
                }
            }
            std::sort(xs.begin(), xs.end(),
                      [](const Crossing& p, const Crossing& q) { return p.x < q.x; });

            // Nonzero winding spans; add exact horizontal overlap per pixel.
            int wind = 0;
            for (size_t k = 0; k + 1 < xs.size(); ++k)
            {
                wind += xs[k].dir;
                if (wind == 0)
                    continue;
                double x0 = std::max(0.0, xs[k].x);
                double x1 = std::min((double)width, xs[k + 1].x);
                if (x1 <= x0)
                    continue;
                int px0 = (int)std::floor(x0), px1 = (int)std::ceil(x1);
                for (int px = px0; px < px1 && px < width; ++px)
                {
                    double ov = std::min(x1, (double)px + 1) - std::max(x0, (double)px);
                    if (ov > 0)
                        row[px] += (float)(ov / kSub);
                }
            }
        }

        unsigned char* dst = &coverage[(size_t)y * width];
        for (int x = 0; x < width; ++x)
            dst[x] = (unsigned char)(std::min(1.0f, row[x]) * 255.0f + 0.5f);
    }
}

std::string CK_EncodeGrayPng(const std::vector<unsigned char>& pixels,
                             int width, int height)
{
    size_t len = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(pixels.data(), width, height,
                                                           1, &len, 6, MZ_FALSE);
    if (!png)
        return std::string();
    std::string out(static_cast<const char*>(png), len);
    mz_free(png);
    return out;
}

// ---------------------------------------------------------------------------
// Hint Layer
// ---------------------------------------------------------------------------

namespace
{
    // Returns (alpha, luma) of pixel x,y in 0..1.
    void ReadPixel(const void* data, long rowbytes, int x, int y, CK_PixelKind kind,
                   float& a, float& l)
    {
        const char* row = static_cast<const char*>(data) + (size_t)y * rowbytes;
        float r, g, b;
        switch (kind)
        {
            case CK_PIX_32F:
            {
                const float* p = reinterpret_cast<const float*>(row) + (size_t)x * 4;
                a = p[0]; r = p[1]; g = p[2]; b = p[3];
                break;
            }
            case CK_PIX_16:
            {
                const unsigned short* p = reinterpret_cast<const unsigned short*>(row) + (size_t)x * 4;
                a = p[0] / 32768.0f; r = p[1] / 32768.0f; g = p[2] / 32768.0f; b = p[3] / 32768.0f;
                break;
            }
            default:
            {
                const unsigned char* p = reinterpret_cast<const unsigned char*>(row) + (size_t)x * 4;
                a = p[0] / 255.0f; r = p[1] / 255.0f; g = p[2] / 255.0f; b = p[3] / 255.0f;
                break;
            }
        }
        l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }
}

void CK_WorldToHint(const void* data, long rowbytes, int w, int h,
                    CK_PixelKind kind, bool use_luma,
                    int out_w, int out_h,
                    std::vector<unsigned char>& out,
                    bool* alpha_fully_opaque, bool* matte_empty)
{
    out.assign((size_t)out_w * out_h, 0);
    if (alpha_fully_opaque) *alpha_fully_opaque = false;
    if (matte_empty) *matte_empty = true;
    if (!data || w <= 0 || h <= 0 || out_w <= 0 || out_h <= 0)
        return;

    // Source matte at native size first, so opacity checks see every pixel.
    std::vector<float> m((size_t)w * h);
    bool opaque = true;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            float a, l;
            ReadPixel(data, rowbytes, x, y, kind, a, l);
            if (a < 0.999f) opaque = false;
            float v = use_luma ? l * a : a;
            m[(size_t)y * w + x] = std::min(1.0f, std::max(0.0f, v));
        }
    if (alpha_fully_opaque) *alpha_fully_opaque = opaque;

    const float sx = (float)w / out_w, sy = (float)h / out_h;
    bool empty = true;
    for (int y = 0; y < out_h; ++y)
    {
        float fy = std::min(std::max((y + 0.5f) * sy - 0.5f, 0.0f), (float)(h - 1));
        int y0 = (int)fy, y1 = std::min(y0 + 1, h - 1);
        float ty = fy - y0;
        for (int x = 0; x < out_w; ++x)
        {
            float fx = std::min(std::max((x + 0.5f) * sx - 0.5f, 0.0f), (float)(w - 1));
            int x0 = (int)fx, x1 = std::min(x0 + 1, w - 1);
            float tx = fx - x0;
            float top = m[(size_t)y0 * w + x0] * (1 - tx) + m[(size_t)y0 * w + x1] * tx;
            float bot = m[(size_t)y1 * w + x0] * (1 - tx) + m[(size_t)y1 * w + x1] * tx;
            float v = top * (1 - ty) + bot * ty;
            unsigned char c = (unsigned char)(v * 255.0f + 0.5f);
            out[(size_t)y * out_w + x] = c;
            if (c > 127) empty = false;
        }
    }
    if (matte_empty) *matte_empty = empty;
}
