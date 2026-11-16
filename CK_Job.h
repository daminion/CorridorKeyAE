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
//   _ref_in_fwd\ _ref_in_bwd\ _ref_out_fwd\ _ref_out_bwd\
//                 reference-frame keys while running (removed when joined)
//   compact.flag   Compact Keys was on: compact the fg once the key is done
//   compact3.txt   next fg frame to compact (resume point)
//   compact3.done  every fg frame has been compacted (edges-only fg with
//                  noise/solid thresholds, v1.1). Older compact.* and
//                  compact2.* markers are ignored, so those keys are
//                  compacted again the new way.

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
    float       hint_quality = 0.95f;     // Hint Strength
    float       carry_quality = 0.95f;    // Carry Strength (CorridorKey default 0.95)
    bool        exact_hint = false;       // every-frame hint: don't replace it with the
                                          // model's carried matte (--no_carry_hint)
    int         temporal_chunk = 0;       // frames per window; 0 = CorridorKey's automatic choice
    bool        low_vram = false;         // only used with a fixed temporal_chunk
    int         width = 0;           // source resolution
    int         height = 0;
    std::string hint_png;            // first-frame hint PNG bytes ("" = no hint)
    std::vector<std::string> hint_frames;  // per-frame hint PNGs (empty = none)

    // Rendered Key Source: frames already rendered by AE (no source file).
    bool        rendered = false;
    bool        rendered_linear = false;     // EXRs hold scene-linear values
    unsigned long long render_hash = 0;      // fingerprint of the rendered pixels
    std::string prerendered_dir;             // moved into <job>\frames by CK_StartJob

    bool        compact = true;              // Compact Keys (off = full-size fg, own key id)

    // Hint at a frame other than the first (Matte Layer, current frame): its
    // index in the keyed range. > 0 keys outward from it in two passes
    // (tools/ck_infer_ref.py); the input is always a frame folder then.
    int         ref_index = 0;
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

// What a running job is doing, from the tail of its log.txt
enum CK_JobPhase
{
    CK_PHASE_STARTING = 0,  // nothing recognisable yet (python starting, trimming)
    CK_PHASE_LOADING,       // loading model weights
    CK_PHASE_PREPASS_MOTION,// RVM continuous prepass
    CK_PHASE_PREPASS_SCENE, // frozen VFM prepass
    CK_PHASE_WINDOWS,       // main pass, window by window
    CK_PHASE_JOINING        // reference-frame key: joining the two passes
};

struct CK_JobStatus
{
    CK_JobState state = CK_JOB_NONE;
    int frames_done = 0;
    // Running jobs only:
    CK_JobPhase phase = CK_PHASE_STARTING;
    int windows_done = 0;
    int windows_total = 0;
    long elapsed_s = -1;    // since the job started (-1 = unknown)
    int pass = 0;           // reference-frame keys: pass i of pass_total (0 = single pass)
    int pass_total = 0;
    bool pass_backward = false;
    bool compacted = false; // done keys: Compact Keys has finished (compact3.done)
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

// Fills phase / windows from log text (exposed for testing).
void CK_ParseJobLog(const std::string& log, CK_JobStatus& st);

// ---- Project folder keys ------------------------------------------------

// Folder created next to the .aep for "Save Keys In: Project Folder"
#define CK_PROJECT_KEYS_DIR "CorridorKeys"

// Folder of the open project ("" = unsaved). Set from the main thread;
// returns true if it changed. Safe to read from any thread.
bool CK_SetProjectDir(const std::string& dir);
std::string CK_GetProjectDir();

// A stored key folder inside some project's CorridorKeys folder resolves to
// the same key next to the open project when that copy exists (the project
// was moved, copied or archived). Anything else is returned unchanged.
std::string CK_ResolveJobDir(const std::string& stored);

// ---- Compact Keys -------------------------------------------------------
// Once a key with compact.flag is done, a background thread rewrites each fg
// frame with the unused background cleared (CK_CompactFgFile). Started from
// CK_QueryJob, so keys that finished while AE was closed are picked up too.

// Queues a done key if it has compact.flag and no compact3.done.
void CK_RequestCompact(const std::string& job_dir);
// Drops job_dir from the queue; if it's being compacted, stops and waits.
void CK_CancelCompact(const std::string& job_dir);

// Marks a finished key for compacting (Key Clip reusing an existing key).
void CK_EnableCompact(const std::string& job_dir);

// True while job_dir is queued or being compacted; done/total are frames
// (total = 0 while queued).
bool CK_CompactProgress(const std::string& job_dir, int& done, int& total);

// Bumps whenever compacting progresses (time to redraw the Status line).
unsigned long CK_CompactVersion();

// Stops the worker (GLOBAL_SETDOWN). Waits for the current frame.
void CK_StopCompactor();

// One-line text for the Status control.
std::string CK_StatusText(const CK_JobStatus& st, int frames_total);
