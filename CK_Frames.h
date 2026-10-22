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
