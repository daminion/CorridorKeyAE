#pragma once

// Loading CorridorKey's cached EXR results.

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

struct CK_Frame
{
    int width = 0;
    int height = 0;
    std::vector<float> rgb;    // linear RGB, 3 floats per pixel, row 0 = top
    std::vector<float> alpha;  // 1 float per pixel
    bool sparse = false;       // some fg pixels are NaN (compacted away): use the plate there
};

// A compacted-away fg value (NaN). Bit test, so /fp:fast can't optimise it out.
inline bool CK_IsMissing(float v)
{
    std::uint32_t b;
    std::memcpy(&b, &v, sizeof(b));
    return (b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu) != 0;
}

// Reads an EXR into named float channels. Portable (tinyexr), no AE deps.
bool CK_ReadExr(const std::string& path,
                std::vector<std::string>& names,
                std::vector<std::vector<float>>& channels,
                int& width, int& height,
                std::string& error);

// Writes interleaved RGB floats (row 0 = top) as a half-float ZIP EXR with
// channels B,G,R, the layout CorridorKey's own EXRs use.
// piz: PIZ compression instead of ZIP (smaller for grainy footage).
bool CK_WriteExrRGB(const std::string& path, int width, int height,
                    const float* rgb, std::string& error, bool piz = false);

// ---- Compact Keys -------------------------------------------------------

// Matte Choke's slider is limited to +/-20 px, and the processed matte at a
// pixel only depends on the original alpha within choke + 1 pixels of it.
// Keep this at least (largest choke) + 1 if the slider range ever grows.
#define CK_COMPACT_BORDER 21

// Alpha at or above this counts as solid. Even at the lowest Matte Gamma
// (0.1, i.e. a^10) it stays >= 0.98, where Keep Original shows only the plate.
#define CK_COMPACT_SOLID 0.999f

// Keeps CorridorKey's fg only where it can show: pixels with both a non-zero
// and a non-solid alpha within `border` pixels (square). Everywhere else the
// fg is set to NaN, meaning "use the plate" (CK_RenderKey). With Keep
// Original on, every output mode except Foreground is unchanged.
// Returns the number of pixels removed.
size_t CK_CompactFg(std::vector<float>& rgb, const std::vector<float>& alpha,
                    int width, int height, int border);

// Reads a fg/alpha pair, runs CK_CompactFg and writes the result to
// out_path as a half-float PIZ EXR.
bool CK_CompactFgFile(const std::string& fg_path, const std::string& alpha_path,
                      const std::string& out_path, std::string& error);

// Builds a frame from fg (B,G,R) and alpha (Y / A / single channel) EXRs.
bool CK_LoadFramePair(const std::string& fg_path,
                      const std::string& alpha_path,
                      CK_Frame& out,
                      std::string& error);

// Cached load of frame `index` from a job folder. Returns null if the frame
// isn't there (yet). `generation` is part of the cache key so a re-key
// never shows stale frames.
std::shared_ptr<const CK_Frame> CK_GetKeyedFrame(const std::string& job_dir,
                                                 int index,
                                                 long generation);
