#pragma once

// Loading CorridorKey's cached EXR results.

#include <memory>
#include <string>
#include <vector>

struct CK_Frame
{
    int width = 0;
    int height = 0;
    std::vector<float> rgb;    // linear RGB, 3 floats per pixel, row 0 = top
    std::vector<float> alpha;  // 1 float per pixel
};

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

// Pixels CK_ClearUnusedFg keeps around the matte: Matte Choke can grow it by
// up to 50 px, and the grown edge shows the fg colour underneath.
#define CK_COMPACT_BORDER 51

// Sets the fg colour to black wherever no alpha > 0 lies within `border`
// pixels (square neighbourhood). Those pixels are invisible in every output
// mode except Foreground. Returns the number of pixels cleared.
size_t CK_ClearUnusedFg(std::vector<float>& rgb, const std::vector<float>& alpha,
                        int width, int height, int border);

// Reads a fg/alpha pair, clears the unused fg and writes the result to
// out_path as a half-float PIZ EXR. Lossless for every visible pixel.
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
