#pragma once

// Key jobs: one CorridorKey run per (source clip, key settings), stored in
// <cache_root>\<clip>_<hash>\ with these files:
//   <clip>_alpha_exr\alpha_000000.exr ...   written by infer.py
//   <clip>_fg_exr\fg_000000.exr ...         written by infer.py
//   log.txt      infer.py stdout + stderr
//   pid.txt      process id of the running job
//   done.flag    created when infer.py exits OK
//   failed.flag  created when infer.py exits with an error
//   source.txt   the source path, for reference
//   hint_frame0.png  first-frame hint (Hint Mask, or Hint Layer > First Frame)
//   hint_frames\hint_000000.png ...  per-frame hint (Hint Layer > Every Frame)
//   frames\ ...   trimmed source frames (removed when the key finishes)

#include <string>
#include <vector>

struct CK_JobRequest
{
    std::string source_path;     // movie file, or first frame of a sequence
    bool        is_sequence = false;
    double      fps = 24.0;
    int         frames_total = 0;    // frames to key (after trimming)
    bool        trimmed = false;     // key only start_frame .. start_frame + frames_total - 1
    int         start_frame = 0;     // first source frame keyed
    float       hint_quality = 0.95f;
    bool        low_vram = false;
    int         width = 0;           // source resolution
    int         height = 0;
    std::string hint_png;            // first-frame hint PNG bytes ("" = no hint)
    std::vector<std::string> hint_frames;  // per-frame hint PNGs (empty = none)
};

enum CK_JobState
{
    CK_JOB_NONE = 0,       // never keyed
    CK_JOB_RUNNING,
    CK_JOB_DONE,
    CK_JOB_FAILED,
    CK_JOB_INTERRUPTED,    // process gone without a done/failed flag
    CK_JOB_OFFLINE         // job folder unreachable (drive disconnected)
};

struct CK_JobStatus
{
    CK_JobState state = CK_JOB_NONE;
    int frames_done = 0;
};

// <cache_root>\<clip>_<8 hex>. Hash covers the source file identity and the
// settings that change the result, so changed settings get a fresh folder.
std::string CK_JobDirFor(const std::string& cache_root, const CK_JobRequest& req);

// Starts infer.py in the background (after trimming, if requested).
// On failure returns false and sets error.
bool CK_StartJob(const std::string& job_dir,
                 const CK_JobRequest& req,
                 const std::string& python_exe,
                 const std::string& corridorkey_dir,
                 const std::string& ffmpeg_exe,
                 std::string& error);

void CK_CancelJob(const std::string& job_dir);

// Cheap enough to call from UI idle: results are cached ~half a second.
CK_JobStatus CK_QueryJob(const std::string& job_dir);

std::string CK_JobLogPath(const std::string& job_dir);

// Jobs whose progress the UI should follow. A job is tracked when it starts
// or when its Status line is drawn while running.
void CK_TrackJob(const std::string& job_dir);

// Re-checks tracked jobs. True if any job's state or frame count changed
// since the last poll (time to refresh the panels). Finished jobs drop off
// after their final change has been reported.
bool CK_PollTrackedJobs();

// One-line text for the Status control.
std::string CK_StatusText(const CK_JobStatus& st, int frames_total);
