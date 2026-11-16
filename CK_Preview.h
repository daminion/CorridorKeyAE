#pragma once

// Preview helper: a background CorridorKey process that keeps the model in
// GPU memory and keys single frames for Preview Frame. Started on demand,
// stopped by Key Clip (to free VRAM) or after 10 idle minutes.

#include <string>

struct CK_GlobalSettings;

enum CK_PreviewState
{
    CK_PV_OFF = 0,      // not running
    CK_PV_LOADING,      // started, model still loading
    CK_PV_READY,        // accepting requests
    CK_PV_FAILED        // exited with an error (see CK_PreviewLogPath)
};

// Cached for ~0.5 s, safe to call from render and UI threads.
CK_PreviewState CK_PreviewQuery();

bool CK_PreviewStart(const CK_GlobalSettings& gs, std::string& error);
void CK_PreviewStop();
std::string CK_PreviewLogPath();

// Keys the single frame in input_dir into output_dir (blocks until done).
bool CK_PreviewKey(const std::string& input_dir, const std::string& output_dir,
                   bool linear, std::string& error);
