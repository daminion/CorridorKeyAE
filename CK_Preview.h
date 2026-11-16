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

// ---- per-request status (for the Status line) --------------------------

enum CK_PreviewReqState
{
    CK_PR_NONE = 0,
    CK_PR_QUEUED,       // clicked; waiting for AE to render that frame
    CK_PR_RUNNING,      // the helper is keying it
    CK_PR_DONE,
    CK_PR_FAILED
};

struct CK_PreviewReqInfo
{
    long token = 0;
    CK_PreviewReqState state = CK_PR_NONE;
    double seconds = 0.0;           // elapsed (running) or total (done)
};

// owner: a per-effect id stored in the effect's data.
void CK_PreviewNote(unsigned long owner, long token, CK_PreviewReqState state);
bool CK_PreviewInfo(unsigned long owner, CK_PreviewReqInfo& info);

// Bumps whenever any request changes state, and every second while one runs.
unsigned long CK_PreviewInfoVersion();

// Only one render keys a given preview folder at a time. Returns true if the
// caller should key it; false after waiting for another render that did.
bool CK_PreviewBeginDir(const std::string& dir);
void CK_PreviewEndDir(const std::string& dir);
