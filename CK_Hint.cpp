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
