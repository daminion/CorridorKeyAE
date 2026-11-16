#include "CK_Frames.h"

#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <mutex>

#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 1
#define TINYEXR_USE_THREAD 0
#include "tinyexr.h"

#ifdef _WIN32
#include "CK_Platform.h"
static const char* kSep = "\\";
#else
// Test builds on other platforms supply this.
std::string CK_FindSubdir(const std::string& dir, const char* suffix);
static const char* kSep = "/";
#endif

bool CK_ReadExr(const std::string& path,
                std::vector<std::string>& names,
                std::vector<std::vector<float>>& channels,
                int& width, int& height,
                std::string& error)
{
    names.clear();
    channels.clear();
    width = height = 0;

    EXRVersion version;
    if (ParseEXRVersionFromFile(&version, path.c_str()) != TINYEXR_SUCCESS)
    {
        error = "not an EXR: " + path;
        return false;
    }
    if (version.multipart || version.tiled)
    {
        error = "unsupported EXR layout (tiled/multipart): " + path;
        return false;
    }

    EXRHeader header;
    InitEXRHeader(&header);
    const char* msg = NULL;
    if (ParseEXRHeaderFromFile(&header, &version, path.c_str(), &msg) != TINYEXR_SUCCESS)
    {
        error = msg ? msg : "EXR header error";
        if (msg) FreeEXRErrorMessage(msg);
        return false;
    }

    for (int c = 0; c < header.num_channels; ++c)
        header.requested_pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;

    EXRImage image;
    InitEXRImage(&image);
    if (LoadEXRImageFromFile(&image, &header, path.c_str(), &msg) != TINYEXR_SUCCESS)
    {
        error = msg ? msg : "EXR read error";
        if (msg) FreeEXRErrorMessage(msg);
        FreeEXRHeader(&header);
        return false;
    }

    width = image.width;
    height = image.height;
    const size_t n = (size_t)width * (size_t)height;

    for (int c = 0; c < header.num_channels; ++c)
    {
        names.push_back(header.channels[c].name);
        const float* src = reinterpret_cast<const float*>(image.images[c]);
        channels.emplace_back(src, src + n);
    }

    FreeEXRImage(&image);
    FreeEXRHeader(&header);
    return true;
}

bool CK_WriteExrRGB(const std::string& path, int width, int height,
                    const float* rgb, std::string& error, bool piz)
{
    const size_t n = (size_t)width * height;
    std::vector<float> b(n), g(n), r(n);
    for (size_t i = 0; i < n; ++i)
    {
        r[i] = rgb[i * 3 + 0];
        g[i] = rgb[i * 3 + 1];
        b[i] = rgb[i * 3 + 2];
    }

    EXRHeader header;
    InitEXRHeader(&header);
    EXRImage image;
    InitEXRImage(&image);

    float* planes[3] = { b.data(), g.data(), r.data() };   // alphabetical: B, G, R
    image.images = reinterpret_cast<unsigned char**>(planes);
    image.num_channels = 3;
    image.width = width;
    image.height = height;

    EXRChannelInfo channels[3];
    std::memset(channels, 0, sizeof(channels));
    channels[0].name[0] = 'B';
    channels[1].name[0] = 'G';
    channels[2].name[0] = 'R';
    int pixel_types[3] = { TINYEXR_PIXELTYPE_FLOAT, TINYEXR_PIXELTYPE_FLOAT, TINYEXR_PIXELTYPE_FLOAT };
    int requested[3] = { TINYEXR_PIXELTYPE_HALF, TINYEXR_PIXELTYPE_HALF, TINYEXR_PIXELTYPE_HALF };
    header.num_channels = 3;
    header.channels = channels;
    header.pixel_types = pixel_types;
    header.requested_pixel_types = requested;
    header.compression_type = piz ? TINYEXR_COMPRESSIONTYPE_PIZ : TINYEXR_COMPRESSIONTYPE_ZIP;

    const char* msg = NULL;
    const int rc = SaveEXRImageToFile(&image, &header, path.c_str(), &msg);
    if (rc != TINYEXR_SUCCESS)
    {
        error = msg ? msg : "EXR write failed";
        if (msg) FreeEXRErrorMessage(msg);
        return false;
    }
    return true;
}

static int FindChannel(const std::vector<std::string>& names, const char* want)
{
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == want)
            return (int)i;
    return -1;
}

bool CK_LoadFramePair(const std::string& fg_path,
                      const std::string& alpha_path,
                      CK_Frame& out,
                      std::string& error)
{
    std::vector<std::string> fn, an;
    std::vector<std::vector<float>> fc, ac;
    int fw = 0, fh = 0, aw = 0, ah = 0;

    if (!CK_ReadExr(fg_path, fn, fc, fw, fh, error)) return false;
    if (!CK_ReadExr(alpha_path, an, ac, aw, ah, error)) return false;
    if (fw != aw || fh != ah)
    {
        error = "fg/alpha size mismatch";
        return false;
    }

    int r = FindChannel(fn, "R"), g = FindChannel(fn, "G"), b = FindChannel(fn, "B");
    if (r < 0 || g < 0 || b < 0)
    {
        error = "fg EXR has no R/G/B channels";
        return false;
    }

    int a = FindChannel(an, "Y");
    if (a < 0) a = FindChannel(an, "A");
    if (a < 0 && an.size() == 1) a = 0;
    if (a < 0)
    {
        error = "alpha EXR has no usable channel";
        return false;
    }

    out.width = fw;
    out.height = fh;
    const size_t n = (size_t)fw * (size_t)fh;
    out.rgb.resize(n * 3);
    for (size_t i = 0; i < n; ++i)
    {
        out.rgb[i * 3 + 0] = fc[r][i];
        out.rgb[i * 3 + 1] = fc[g][i];
        out.rgb[i * 3 + 2] = fc[b][i];
    }
    out.alpha.swap(ac[a]);
    return true;
}

// ---------------------------------------------------------------------------
// Small LRU cache shared by all render threads
// ---------------------------------------------------------------------------

namespace
{
    const size_t kMaxFrames = 8;

    std::mutex g_mutex;
    std::list<std::string> g_order;   // front = most recent
    std::map<std::string, std::shared_ptr<const CK_Frame>> g_frames;
    std::map<std::string, std::pair<std::string, std::string>> g_subdirs;  // job -> (fg, alpha)

    void Touch(const std::string& key)
    {
        g_order.remove(key);
        g_order.push_front(key);
        while (g_order.size() > kMaxFrames)
        {
            g_frames.erase(g_order.back());
            g_order.pop_back();
        }
    }
}

std::shared_ptr<const CK_Frame> CK_GetKeyedFrame(const std::string& job_dir,
                                                 int index,
                                                 long generation)
{
    if (job_dir.empty() || index < 0)
        return nullptr;

    char key_buf[64];
    std::snprintf(key_buf, sizeof(key_buf), "|%ld|%d", generation, index);
    const std::string key = job_dir + key_buf;

    std::pair<std::string, std::string> subs;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_frames.find(key);
        if (it != g_frames.end())
        {
            Touch(key);
            return it->second;
        }
        auto s = g_subdirs.find(job_dir);
        if (s != g_subdirs.end())
            subs = s->second;
    }

    if (subs.first.empty() || subs.second.empty())
    {
        subs.first = CK_FindSubdir(job_dir, "_fg_exr");
        subs.second = CK_FindSubdir(job_dir, "_alpha_exr");
        if (subs.first.empty() || subs.second.empty())
            return nullptr;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_subdirs[job_dir] = subs;
    }

    char name[64];
    std::snprintf(name, sizeof(name), "%sfg_%06d.exr", kSep, index);
    std::string fg = subs.first + name;
    std::snprintf(name, sizeof(name), "%salpha_%06d.exr", kSep, index);
    std::string al = subs.second + name;

    auto frame = std::make_shared<CK_Frame>();
    std::string error;
    if (!CK_LoadFramePair(fg, al, *frame, error))
        return nullptr;   // not written yet, or mid-write

    std::lock_guard<std::mutex> lock(g_mutex);
    g_frames[key] = frame;
    Touch(key);
    return frame;
}

// ---------------------------------------------------------------------------
// Compact Keys
// ---------------------------------------------------------------------------

size_t CK_ClearUnusedFg(std::vector<float>& rgb, const std::vector<float>& alpha,
                        int width, int height, int border)
{
    const int w = width, h = height, r = border < 0 ? 0 : border;
    if (w <= 0 || h <= 0 || alpha.size() < (size_t)w * h || rgb.size() < (size_t)w * h * 3)
        return 0;

    // Pass 1 (rows): near[i] = some alpha > 0 within r pixels horizontally.
    std::vector<unsigned char> near((size_t)w * h, 0);
    for (int y = 0; y < h; ++y)
    {
        const float* a = alpha.data() + (size_t)y * w;
        unsigned char* n = near.data() + (size_t)y * w;
        int count = 0;
        for (int x = 0; x < r && x < w; ++x)          // prime the window [x - r, x + r]
            count += a[x] > 0.0f;
        for (int x = 0; x < w; ++x)
        {
            if (x + r < w) count += a[x + r] > 0.0f;
            if (x - r - 1 >= 0) count -= a[x - r - 1] > 0.0f;
            n[x] = count > 0;
        }
    }

    // Pass 2 (columns) on the row result, clearing as it goes.
    size_t cleared = 0;
    for (int x = 0; x < w; ++x)
    {
        int count = 0;
        for (int y = 0; y < r && y < h; ++y)
            count += near[(size_t)y * w + x];
        for (int y = 0; y < h; ++y)
        {
            if (y + r < h) count += near[(size_t)(y + r) * w + x];
            if (y - r - 1 >= 0) count -= near[(size_t)(y - r - 1) * w + x];
            if (count == 0)
            {
                float* p = rgb.data() + ((size_t)y * w + x) * 3;
                p[0] = p[1] = p[2] = 0.0f;
                ++cleared;
            }
        }
    }
    return cleared;
}

bool CK_CompactFgFile(const std::string& fg_path, const std::string& alpha_path,
                      const std::string& out_path, std::string& error)
{
    CK_Frame f;
    if (!CK_LoadFramePair(fg_path, alpha_path, f, error))
        return false;
    CK_ClearUnusedFg(f.rgb, f.alpha, f.width, f.height, CK_COMPACT_BORDER);
    return CK_WriteExrRGB(out_path, f.width, f.height, f.rgb.data(), error, true);
}
