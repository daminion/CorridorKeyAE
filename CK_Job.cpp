#include "CK_Job.h"
#include "CK_Platform.h"
#include "CK_Frames.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <mutex>

namespace
{
    unsigned long long Fnv1a(const std::string& s, unsigned long long h = 1469598103934665603ULL)
    {
        for (unsigned char c : s)
        {
            h ^= c;
            h *= 1099511628211ULL;
        }
        return h;
    }

    std::string FileNamePart(const std::string& path)
    {
        size_t p = path.find_last_of("\\/");
        return p == std::string::npos ? path : path.substr(p + 1);
    }

    std::string ParentDir(const std::string& path)
    {
        size_t p = path.find_last_of("\\/");
        return p == std::string::npos ? std::string() : path.substr(0, p);
    }

    std::string StripExt(const std::string& name)
    {
        size_t p = name.find_last_of('.');
        return p == std::string::npos ? name : name.substr(0, p);
    }

    std::string Lower(std::string s)
    {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        return s;
    }

    // Keep folder names short and filesystem-safe.
    std::string SafeName(const std::string& s)
    {
        std::string out;
        for (char c : s)
        {
            unsigned char u = (unsigned char)c;
            bool ok = (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') ||
                      (u >= 'a' && u <= 'z') || u == '-' || u == '_';
            out += ok ? c : '_';
        }
        if (out.size() > 48) out.resize(48);
        if (out.empty()) out = "clip";
        return out;
    }

    std::string Quote(const std::string& s) { return "\"" + s + "\""; }

    struct Cached
    {
        unsigned long tick = 0;
        CK_JobStatus status;
    };
    std::mutex g_mutex;
    std::map<std::string, Cached> g_cache;

    std::mutex g_track_mutex;
    std::map<std::string, std::string> g_tracked;   // job_dir -> last reported signature

    std::string Signature(const CK_JobStatus& st)
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%d:%d:%d:%d:%ld", (int)st.state, st.frames_done,
                      (int)st.phase, st.windows_done, st.elapsed_s);
        return buf;
    }
}

void CK_TrackJob(const std::string& job_dir)
{
    if (job_dir.empty())
        return;
    std::lock_guard<std::mutex> lock(g_track_mutex);
    g_tracked.emplace(job_dir, std::string());
}

bool CK_PollTrackedJobs()
{
    std::vector<std::string> dirs;
    {
        std::lock_guard<std::mutex> lock(g_track_mutex);
        for (auto& kv : g_tracked)
            dirs.push_back(kv.first);
    }

    bool changed = false;
    for (const std::string& dir : dirs)
    {
        CK_JobStatus st = CK_QueryJob(dir);
        std::string sig = Signature(st);

        std::lock_guard<std::mutex> lock(g_track_mutex);
        auto it = g_tracked.find(dir);
        if (it == g_tracked.end())
            continue;
        if (it->second != sig)
        {
            it->second = sig;
            changed = true;
        }
        else if (st.state != CK_JOB_RUNNING)
        {
            g_tracked.erase(it);   // final state already reported
        }
    }
    return changed;
}

std::string CK_JobLogPath(const std::string& job_dir)
{
    return job_dir + "\\log.txt";
}

std::string CK_JobDirFor(const std::string& cache_root, const CK_JobRequest& req)
{
    std::string key = Lower(req.source_path) + "|" + CK_FileStamp(req.source_path);
    char settings[128];
    // Low VRAM only takes effect with a fixed window (see CK_StartJob).
    const bool low_vram = req.low_vram && req.temporal_chunk > 0;
    std::snprintf(settings, sizeof(settings), "|q=%.3f|lv=%d|seq=%d|fps=%.4f",
                  req.hint_quality, low_vram ? 1 : 0, req.is_sequence ? 1 : 0, req.fps);
    key += settings;
    if (req.temporal_chunk > 0)
    {
        std::snprintf(settings, sizeof(settings), "|tw=%d", req.temporal_chunk);
        key += settings;
    }
    if ((!req.hint_png.empty() || !req.hint_frames.empty()) &&
        std::fabs(req.carry_quality - 0.95f) > 1e-4f)
    {
        std::snprintf(settings, sizeof(settings), "|cq=%.3f", req.carry_quality);
        key += settings;
    }

    // Optional parts are only added when used, so plain keys keep the id
    // they had in earlier versions and existing caches stay valid.
    if (!req.hint_png.empty())
    {
        std::snprintf(settings, sizeof(settings), "|hint=%016llx", Fnv1a(req.hint_png));
        key += settings;
    }
    if (!req.hint_frames.empty())
    {
        unsigned long long h = 1469598103934665603ULL;
        for (const std::string& f : req.hint_frames)
            h = Fnv1a(f, h);
        std::snprintf(settings, sizeof(settings), "|hv=%016llx", h);
        key += settings;
        if (req.exact_hint)
            key += "|nocarry";
    }
    if (req.rendered)
    {
        std::snprintf(settings, sizeof(settings), "|rend=%016llx|lin=%d",
                      req.render_hash, req.rendered_linear ? 1 : 0);
        key += settings;
    }
    if (!req.compact)
        key += "|full";   // full-size fg is a different key from a compacted one
    if (req.is_sequence)
    {
        // A re-rendered sequence can keep its first frame's size and date, so
        // also fingerprint the frame count and the middle and last frames.
        const std::string seq_dir = ParentDir(req.source_path);
        const std::string name = FileNamePart(req.source_path);
        const size_t dot = name.find_last_of('.');
        const std::vector<std::string> files =
            CK_ListFilesWithExt(seq_dir, dot == std::string::npos ? std::string() : name.substr(dot));
        if (!files.empty())
        {
            key += "|seqn=" + std::to_string(files.size());
            key += "|mid=" + CK_FileStamp(files[files.size() / 2]);
            key += "|last=" + CK_FileStamp(files.back());
        }
    }
    if (req.trimmed)
    {
        // Untrimmed keys keep the same id as before trimming existed.
        std::snprintf(settings, sizeof(settings), "|start=%d|n=%d", req.start_frame, req.frames_total);
        key += settings;
    }

    char hex[17];
    std::snprintf(hex, sizeof(hex), "%08llx", Fnv1a(key) & 0xffffffffULL);

    std::string clip = req.is_sequence ? FileNamePart(ParentDir(req.source_path))
                                       : StripExt(FileNamePart(req.source_path));
    return cache_root + "\\" + SafeName(clip) + "_" + hex;
}

bool CK_StartJob(const std::string& job_dir,
                 const CK_JobRequest& req,
                 const std::string& python_exe,
                 const std::string& corridorkey_dir,
                 const std::string& ffmpeg_exe,
                 std::string& error)
{
    if (!CK_MakeDirs(job_dir))
    {
        error = "Couldn't create the cache folder:\n  " + job_dir;
        return false;
    }

    CK_RemoveFile(job_dir + "\\done.flag");
    CK_RemoveFile(job_dir + "\\failed.flag");
    CK_RemoveFile(job_dir + "\\pid.txt");

    // Re-keying: stop compacting the old frames, start compact state afresh.
    CK_CancelCompact(job_dir);
    CK_RemoveFile(job_dir + "\\compact.txt");      // v0.15 markers
    CK_RemoveFile(job_dir + "\\compact.done");
    CK_RemoveFile(job_dir + "\\compact2.txt");
    CK_RemoveFile(job_dir + "\\compact2.done");
    if (req.compact)
        CK_WriteTextFile(job_dir + "\\compact.flag", "");
    else
        CK_RemoveFile(job_dir + "\\compact.flag");
    CK_WriteTextFile(job_dir + "\\source.txt", req.source_path + "\r\n");

    const std::string hint_path = job_dir + "\\hint_frame0.png";
    CK_RemoveFile(hint_path);
    if (!req.hint_png.empty() && !CK_WriteTextFile(hint_path, req.hint_png))
    {
        error = "Couldn't write the hint image:\n  " + hint_path;
        return false;
    }

    const std::string hint_dir = job_dir + "\\hint_frames";
    if (!req.hint_frames.empty())
    {
        if (!CK_MakeDirs(hint_dir))
        {
            error = "Couldn't create the hint folder:\n  " + hint_dir;
            return false;
        }
        for (size_t i = 0; i < req.hint_frames.size(); ++i)
        {
            char name[48];
            std::snprintf(name, sizeof(name), "\\hint_%06d.png", (int)i);
            if (!CK_WriteTextFile(hint_dir + name, req.hint_frames[i]))
            {
                error = "Couldn't write hint frames to:\n  " + hint_dir;
                return false;
            }
        }
    }

    std::string input = req.is_sequence ? ParentDir(req.source_path) : req.source_path;
    bool frame_dir = req.is_sequence;
    std::string trim_cmd;                         // runs before infer.py
    const std::string trim_dir = job_dir + "\\frames";
    char num[64];

    if (!req.prerendered_dir.empty())
    {
        // Frames AE already rendered (only the keyed range): just move them in.
        CK_RemoveTree(trim_dir);
        if (!CK_MoveDir(req.prerendered_dir, trim_dir))
        {
            error = "Couldn't move the rendered frames into:\n  " + trim_dir;
            return false;
        }
        input = trim_dir;
        frame_dir = true;
    }
    else if (req.trimmed)
    {
        if (!CK_MakeDirs(trim_dir))
        {
            error = "Couldn't create the trim folder:\n  " + trim_dir;
            return false;
        }

        if (req.is_sequence)
        {
            // Link (or copy) just the frames in range; nothing is re-encoded.
            std::string name = FileNamePart(req.source_path);
            size_t dot = name.find_last_of('.');
            std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
            std::vector<std::string> files = CK_ListFilesWithExt(ParentDir(req.source_path), ext);
            if ((int)files.size() < req.start_frame + req.frames_total)
            {
                error = "The image sequence has fewer frames than the layer's range.";
                return false;
            }
            for (int i = 0; i < req.frames_total; ++i)
            {
                const std::string& src = files[req.start_frame + i];
                if (!CK_LinkOrCopy(src, trim_dir + "\\" + FileNamePart(src)))
                {
                    error = "Couldn't link frames into:\n  " + trim_dir;
                    return false;
                }
            }
        }
        else
        {
            if (ffmpeg_exe.empty())
            {
                error = "ffmpeg wasn't found, so the clip can't be trimmed to the layer's in/out.\n\n"
                        "Set Key Range > Range to Whole Clip, or set the ffmpeg path in the setup menu.";
                return false;
            }
            // Seek half a frame early so the first decoded frame is exactly start_frame.
            double ss = (req.start_frame - 0.5) / req.fps;
            if (ss < 0) ss = 0;
            std::snprintf(num, sizeof(num), " -ss %.6f", ss);
            trim_cmd = Quote(ffmpeg_exe) + " -hide_banner -loglevel error -y" + num;
            trim_cmd += " -i " + Quote(req.source_path);
            std::snprintf(num, sizeof(num), " -frames:v %d", req.frames_total);
            trim_cmd += num;
            trim_cmd += " -pix_fmt rgb24 -start_number 0 " + Quote(trim_dir + "\\f_%06d.png");
        }
        input = trim_dir;
        frame_dir = true;
    }

    std::string args;
    args += " --input " + Quote(input);
    args += " --output_dir " + Quote(job_dir);
    args += " --output_format exr --exr_dtype half --outputs alpha fg";
    if (!req.hint_frames.empty())
    {
        args += " --hint_video " + Quote(hint_dir);
        std::snprintf(num, sizeof(num), " --hint_quality %.3f", req.hint_quality);
        args += num;
        // By default CorridorKey replaces the hint with its own carried matte on
        // the frames where its time windows overlap (about half of them). For an
        // authoritative matte like a Roto Brush pass, its README recommends
        // switching that off.
        if (req.exact_hint)
            args += " --no_carry_hint";
        else
        {
            std::snprintf(num, sizeof(num), " --carry_hint_quality %.3f", req.carry_quality);
            args += num;
        }
    }
    else if (!req.hint_png.empty())
    {
        args += " --hint_first_frame " + Quote(hint_path);
        std::snprintf(num, sizeof(num), " --hint_quality %.3f --carry_hint_quality %.3f",
                      req.hint_quality, req.carry_quality);
        args += num;
    }
    // Temporal window: leave it to CorridorKey (it sizes the window from free
    // VRAM) unless a fixed size is chosen. Its --low_vram flag also switches
    // the automatic choice off, so it's only passed with a fixed window.
    if (req.temporal_chunk > 0)
    {
        std::snprintf(num, sizeof(num), " --hann_chunk %d --hann_stride %d",
                      req.temporal_chunk, req.temporal_chunk > 1 ? req.temporal_chunk / 2 : 1);
        args += num;
        if (req.low_vram)
            args += " --low_vram --temp_dir " + Quote(job_dir + "\\tmp");
    }
    if (frame_dir)
    {
        std::snprintf(num, sizeof(num), " --frame_dir_fps %.6f", req.fps);
        args += num;
        const bool exr_source = !req.rendered && Lower(req.source_path).size() > 4 &&
            Lower(req.source_path).compare(req.source_path.size() - 4, 4, ".exr") == 0;
        if (exr_source || (req.rendered && req.rendered_linear))
            args += " --frame_dir_linear";
    }

    // Done/failed flags are written by cmd.exe itself, so they appear even
    // if After Effects has been closed in the meantime.
    std::string infer = Quote(python_exe) + " " + Quote(corridorkey_dir + "\\infer.py") + args;

    std::string cmd;
    cmd += "set PYTHONUNBUFFERED=1&& (";
    cmd += trim_cmd.empty() ? infer : trim_cmd + " && " + infer;
    cmd += ") > " + Quote(CK_JobLogPath(job_dir)) + " 2>&1";
    cmd += " && (type nul > " + Quote(job_dir + "\\done.flag") + ")";
    cmd += " || (type nul > " + Quote(job_dir + "\\failed.flag") + ")";
    if (req.trimmed || req.rendered)
        cmd += " & rmdir /s /q " + Quote(trim_dir) + " 2>nul";   // drop the temporary frames
    if (req.temporal_chunk > 0 && req.low_vram)
        cmd += " & rmdir /s /q " + Quote(job_dir + "\\tmp") + " 2>nul";   // low-VRAM scratch

    unsigned long pid = 0;
    if (!CK_LaunchHidden(cmd, corridorkey_dir, &pid))
    {
        error = "Couldn't start CorridorKey. Check the python path in the setup menu (tools\\CorridorKey_Setup.bat).";
        return false;
    }

    std::snprintf(num, sizeof(num), "%lu", pid);
    CK_WriteTextFile(job_dir + "\\pid.txt", num);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_cache.erase(job_dir);
    }
    CK_TrackJob(job_dir);
    return true;
}

void CK_CancelJob(const std::string& job_dir)
{
    unsigned long pid = std::strtoul(CK_ReadTextFile(job_dir + "\\pid.txt").c_str(), NULL, 10);
    if (CK_ProcessAlive(pid))
        CK_KillProcessTree(pid);
    CK_RemoveFile(job_dir + "\\pid.txt");

    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache.erase(job_dir);
}

// Reads CorridorKey's progress lines (runtime.py) from the end of its log:
//   "Loading CorridorKey v2 runtime weights: ..."
//   "    RVM continuous prepass: T=..."
//   "    frozen VFM prepass: T=..."
//   "    Hann stage1: chunk=.., stride=.., windows=N, ..."
//   "      window i/N [t0:t1]"
// The latest recognised line wins. Unknown output leaves the phase alone.
void CK_ParseJobLog(const std::string& log, CK_JobStatus& st)
{
    size_t pos = 0;
    while (pos < log.size())
    {
        size_t end = log.find_first_of("\r\n", pos);
        if (end == std::string::npos) end = log.size();
        const std::string line = log.substr(pos, end - pos);
        pos = end + 1;

        int a = 0, b = 0;
        const size_t lead = line.find_first_not_of(' ');
        if (lead == std::string::npos) continue;
        const char* l = line.c_str() + lead;

        if (std::strncmp(l, "window ", 7) == 0 && std::sscanf(l + 7, "%d/%d", &a, &b) == 2 && b > 0)
        {
            st.phase = CK_PHASE_WINDOWS;
            st.windows_done = a;
            st.windows_total = b;
        }
        else if (std::strncmp(l, "Hann stage1:", 12) == 0)
        {
            st.phase = CK_PHASE_WINDOWS;
            st.windows_done = 0;
            const char* w = std::strstr(l, "windows=");
            st.windows_total = (w && std::sscanf(w + 8, "%d", &b) == 1) ? b : 0;
        }
        else if (std::strncmp(l, "frozen VFM prepass", 18) == 0)
            st.phase = CK_PHASE_PREPASS_SCENE;
        else if (std::strncmp(l, "RVM continuous prepass", 22) == 0)
            st.phase = CK_PHASE_PREPASS_MOTION;
        else if (std::strncmp(l, "Loading CorridorKey", 19) == 0)
            st.phase = CK_PHASE_LOADING;
    }
}

CK_JobStatus CK_QueryJob(const std::string& job_dir)
{
    CK_JobStatus st;
    if (job_dir.empty())
        return st;

    unsigned long now = CK_TickMs();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_cache.find(job_dir);
        if (it != g_cache.end() && now - it->second.tick < 500)
            return it->second.status;
    }

    if (!CK_DirExists(job_dir))
    {
        // Drive gone vs. folder deleted
        std::string root = job_dir.size() >= 3 && job_dir[1] == ':' ? job_dir.substr(0, 3) : "";
        st.state = (!root.empty() && !CK_DirExists(root)) ? CK_JOB_OFFLINE : CK_JOB_NONE;
    }
    else
    {
        st.frames_done = CK_CountExrInSubdir(job_dir, "_fg_exr");

        if (CK_FileExists(job_dir + "\\done.flag"))
        {
            st.state = CK_JOB_DONE;
            st.compacted = CK_FileExists(job_dir + "\\compact2.done");
            if (!st.compacted)
                CK_RequestCompact(job_dir);
        }
        else if (CK_FileExists(job_dir + "\\failed.flag"))
            st.state = CK_JOB_FAILED;
        else
        {
            unsigned long pid = std::strtoul(
                CK_ReadTextFile(job_dir + "\\pid.txt").c_str(), NULL, 10);
            st.state = CK_ProcessAlive(pid) ? CK_JOB_RUNNING : CK_JOB_INTERRUPTED;
            if (st.state == CK_JOB_RUNNING)
            {
                st.elapsed_s = CK_FileAgeSeconds(job_dir + "\\pid.txt");
                CK_ParseJobLog(CK_ReadFileTail(CK_JobLogPath(job_dir), 16384), st);
            }
        }
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    Cached& c = g_cache[job_dir];
    c.tick = now;
    c.status = st;
    return st;
}

std::string CK_StatusText(const CK_JobStatus& st, int frames_total)
{
    char buf[160];
    switch (st.state)
    {
        case CK_JOB_RUNNING:
        {
            char t[32] = "";
            if (st.elapsed_s >= 0)
                std::snprintf(t, sizeof(t), "  %ld:%02ld", st.elapsed_s / 60, st.elapsed_s % 60);

            if (st.frames_done <= 0)
            {
                switch (st.phase)
                {
                    case CK_PHASE_LOADING:
                        std::snprintf(buf, sizeof(buf), "Keying... loading model%s", t); break;
                    case CK_PHASE_PREPASS_MOTION:
                        std::snprintf(buf, sizeof(buf), "Keying... analysing motion%s", t); break;
                    case CK_PHASE_PREPASS_SCENE:
                        std::snprintf(buf, sizeof(buf), "Keying... analysing scene%s", t); break;
                    case CK_PHASE_WINDOWS:
                        if (st.windows_total > 0)
                            std::snprintf(buf, sizeof(buf), "Keying... analysing clip, window %d / %d%s",
                                          st.windows_done, st.windows_total, t);
                        else
                            std::snprintf(buf, sizeof(buf), "Keying... analysing clip%s", t);
                        break;
                    default:
                        std::snprintf(buf, sizeof(buf), "Keying... starting%s", t); break;
                }
            }
            else if (frames_total > 0)
                std::snprintf(buf, sizeof(buf), "Keying... %d / %d frames (%d%%)%s",
                              st.frames_done, frames_total,
                              (int)(100.0 * st.frames_done / frames_total), t);
            else
                std::snprintf(buf, sizeof(buf), "Keying... %d frames%s", st.frames_done, t);
            break;
        }
        case CK_JOB_DONE:
            std::snprintf(buf, sizeof(buf), "Keyed - %d frames cached", st.frames_done);
            break;
        case CK_JOB_FAILED:
            std::snprintf(buf, sizeof(buf), "Key failed - click Key Clip to see the log");
            break;
        case CK_JOB_INTERRUPTED:
            std::snprintf(buf, sizeof(buf), "Interrupted - click Key Clip to restart");
            break;
        case CK_JOB_OFFLINE:
            std::snprintf(buf, sizeof(buf), "Cache offline - reconnect the drive");
            break;
        default:
            std::snprintf(buf, sizeof(buf), "Not keyed - click Key Clip");
            break;
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Project folder keys
// ---------------------------------------------------------------------------

namespace
{
    std::mutex g_proj_mutex;
    std::string g_project_dir;

    struct Resolved
    {
        std::string dir;
        unsigned long tick = 0;
    };
    std::map<std::string, Resolved> g_resolved;   // "<stored>|<project>" -> folder
}

bool CK_SetProjectDir(const std::string& dir)
{
    std::lock_guard<std::mutex> lock(g_proj_mutex);
    if (dir == g_project_dir)
        return false;
    g_project_dir = dir;
    g_resolved.clear();
    return true;
}

std::string CK_GetProjectDir()
{
    std::lock_guard<std::mutex> lock(g_proj_mutex);
    return g_project_dir;
}

std::string CK_ResolveJobDir(const std::string& stored)
{
    static const std::string marker = "\\" + Lower(CK_PROJECT_KEYS_DIR) + "\\";
    const std::string low = Lower(stored);
    const size_t pos = low.rfind(marker);
    if (pos == std::string::npos)
        return stored;

    const std::string proj = CK_GetProjectDir();
    if (proj.empty())
        return stored;
    const std::string candidate = proj + "\\" CK_PROJECT_KEYS_DIR "\\" + stored.substr(pos + marker.size());
    if (Lower(candidate) == low)
        return stored;

    const std::string key = stored + "|" + proj;
    const unsigned long now = CK_TickMs();
    {
        std::lock_guard<std::mutex> lock(g_proj_mutex);
        auto it = g_resolved.find(key);
        if (it != g_resolved.end() && now - it->second.tick < 2000)
            return it->second.dir;
    }
    // The open project's own copy wins, so an archived project uses its keys.
    const std::string dir = CK_DirExists(candidate) ? candidate : stored;
    std::lock_guard<std::mutex> lock(g_proj_mutex);
    Resolved& r = g_resolved[key];
    r.dir = dir;
    r.tick = now ? now : 1;
    return dir;
}

// ---------------------------------------------------------------------------
// Compact Keys worker
// ---------------------------------------------------------------------------

namespace
{
    std::mutex g_cmp_mutex;
    std::condition_variable g_cmp_cv;
    std::deque<std::string> g_cmp_queue;
    std::string g_cmp_active;           // folder being compacted ("" = idle)
    bool g_cmp_cancel = false;          // abandon g_cmp_active
    bool g_cmp_quit = false;
    int g_cmp_done = 0, g_cmp_total = 0;
    unsigned long g_cmp_version = 1;

    // Detached (not joined) if the DLL unloads without GLOBAL_SETDOWN:
    // joining from a static destructor can deadlock on the loader lock.
    struct WorkerThread
    {
        std::thread t;
        ~WorkerThread() { if (t.joinable()) t.detach(); }
    } g_cmp_thread;

    bool Stopping()
    {
        std::lock_guard<std::mutex> lock(g_cmp_mutex);
        return g_cmp_cancel || g_cmp_quit;
    }

    void CompactJob(const std::string& dir)
    {
        const std::string fg_dir = CK_FindSubdir(dir, "_fg_exr");
        const std::string al_dir = CK_FindSubdir(dir, "_alpha_exr");
        if (fg_dir.empty() || al_dir.empty())
            return;
        const int total = CK_CountExrInSubdir(dir, "_fg_exr");
        int i = std::atoi(CK_ReadTextFile(dir + "\\compact2.txt").c_str());
        if (i < 0 || i > total) i = 0;
        {
            std::lock_guard<std::mutex> lock(g_cmp_mutex);
            g_cmp_done = i;
            g_cmp_total = total;
            ++g_cmp_version;
        }

        for (; i < total; ++i)
        {
            if (Stopping())
                return;
            char name[64];
            std::snprintf(name, sizeof(name), "\\fg_%06d.exr", i);
            const std::string fg = fg_dir + name;
            std::snprintf(name, sizeof(name), "\\alpha_%06d.exr", i);
            const std::string al = al_dir + name;
            std::snprintf(name, sizeof(name), "\\fg_%06d.tmp", i);
            const std::string tmp = fg_dir + name;

            std::string error;
            if (CK_CompactFgFile(fg, al, tmp, error))
            {
                // A render may have the frame open for a moment: retry briefly,
                // and leave this one full-size if it stays busy.
                bool replaced = false;
                for (int attempt = 0; attempt < 10 && !replaced; ++attempt)
                {
                    replaced = CK_ReplaceFile(tmp, fg);
                    if (!replaced)
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                if (!replaced)
                    CK_RemoveFile(tmp);
            }
            else
                CK_RemoveFile(tmp);

            CK_WriteTextFile(dir + "\\compact2.txt", std::to_string(i + 1));
            std::lock_guard<std::mutex> lock(g_cmp_mutex);
            g_cmp_done = i + 1;
            ++g_cmp_version;
        }
        if (!Stopping())
        {
            CK_WriteTextFile(dir + "\\compact2.done", "");
            CK_RemoveFile(dir + "\\compact2.txt");
        }
    }

    void Worker()
    {
        std::unique_lock<std::mutex> lock(g_cmp_mutex);
        for (;;)
        {
            g_cmp_cv.wait(lock, [] { return g_cmp_quit || !g_cmp_queue.empty(); });
            if (g_cmp_quit)
                break;
            g_cmp_active = g_cmp_queue.front();
            g_cmp_queue.pop_front();
            g_cmp_cancel = false;
            g_cmp_done = g_cmp_total = 0;
            ++g_cmp_version;
            const std::string dir = g_cmp_active;

            lock.unlock();
            CompactJob(dir);
            lock.lock();

            g_cmp_active.clear();
            ++g_cmp_version;
            g_cmp_cv.notify_all();      // CK_CancelCompact may be waiting
        }
        g_cmp_active.clear();
        g_cmp_cv.notify_all();
    }
}

void CK_RequestCompact(const std::string& job_dir)
{
    if (!CK_FileExists(job_dir + "\\compact.flag") || CK_FileExists(job_dir + "\\compact2.done"))
        return;
    std::lock_guard<std::mutex> lock(g_cmp_mutex);
    if (g_cmp_quit || g_cmp_active == job_dir)
        return;
    for (const std::string& q : g_cmp_queue)
        if (q == job_dir)
            return;
    g_cmp_queue.push_back(job_dir);
    ++g_cmp_version;
    if (!g_cmp_thread.t.joinable())
        g_cmp_thread.t = std::thread(Worker);
    g_cmp_cv.notify_all();
}

void CK_CancelCompact(const std::string& job_dir)
{
    std::unique_lock<std::mutex> lock(g_cmp_mutex);
    for (auto it = g_cmp_queue.begin(); it != g_cmp_queue.end();)
        it = *it == job_dir ? g_cmp_queue.erase(it) : it + 1;
    if (g_cmp_active == job_dir)
    {
        g_cmp_cancel = true;
        g_cmp_cv.wait_for(lock, std::chrono::seconds(30),
                          [&] { return g_cmp_active != job_dir; });
    }
    ++g_cmp_version;
}

void CK_EnableCompact(const std::string& job_dir)
{
    if (!CK_FileExists(job_dir + "\\compact.flag"))
        CK_WriteTextFile(job_dir + "\\compact.flag", "");
    CK_RequestCompact(job_dir);
}

bool CK_CompactProgress(const std::string& job_dir, int& done, int& total)
{
    std::lock_guard<std::mutex> lock(g_cmp_mutex);
    done = total = 0;
    if (g_cmp_active == job_dir)
    {
        done = g_cmp_done;
        total = g_cmp_total;
        return true;
    }
    for (const std::string& q : g_cmp_queue)
        if (q == job_dir)
            return true;
    return false;
}

unsigned long CK_CompactVersion()
{
    std::lock_guard<std::mutex> lock(g_cmp_mutex);
    return g_cmp_version;
}

void CK_StopCompactor()
{
    {
        std::lock_guard<std::mutex> lock(g_cmp_mutex);
        g_cmp_quit = true;
        g_cmp_queue.clear();
    }
    g_cmp_cv.notify_all();
    if (g_cmp_thread.t.joinable())
        g_cmp_thread.t.join();
}
