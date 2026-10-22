#include "CK_Job.h"
#include "CK_Platform.h"

#include <cstdio>
#include <cstdlib>
#include <map>
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
}

std::string CK_JobLogPath(const std::string& job_dir)
{
    return job_dir + "\\log.txt";
}

std::string CK_JobDirFor(const std::string& cache_root, const CK_JobRequest& req)
{
    std::string key = Lower(req.source_path) + "|" + CK_FileStamp(req.source_path);
    char settings[128];
    std::snprintf(settings, sizeof(settings), "|q=%.3f|lv=%d|seq=%d|fps=%.4f|hint=%016llx",
                  req.hint_quality, req.low_vram ? 1 : 0, req.is_sequence ? 1 : 0, req.fps,
                  req.hint_png.empty() ? 0ULL : Fnv1a(req.hint_png));
    key += settings;

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
    CK_WriteTextFile(job_dir + "\\source.txt", req.source_path + "\r\n");

    const std::string hint_path = job_dir + "\\hint_frame0.png";
    CK_RemoveFile(hint_path);
    if (!req.hint_png.empty() && !CK_WriteTextFile(hint_path, req.hint_png))
    {
        error = "Couldn't write the hint image:\n  " + hint_path;
        return false;
    }

    std::string input = req.is_sequence ? ParentDir(req.source_path) : req.source_path;

    char num[64];
    std::string args;
    args += " --input " + Quote(input);
    args += " --output_dir " + Quote(job_dir);
    args += " --output_format exr --exr_dtype half --outputs alpha fg";
    if (!req.hint_png.empty())
    {
        args += " --hint_first_frame " + Quote(hint_path);
        std::snprintf(num, sizeof(num), " --hint_quality %.3f", req.hint_quality);
        args += num;
    }
    if (req.low_vram)
        args += " --low_vram --temp_dir " + Quote(job_dir + "\\tmp");
    if (req.is_sequence)
    {
        std::snprintf(num, sizeof(num), " --frame_dir_fps %.6f", req.fps);
        args += num;
        if (Lower(req.source_path).size() > 4 &&
            Lower(req.source_path).compare(req.source_path.size() - 4, 4, ".exr") == 0)
            args += " --frame_dir_linear";
    }

    // Done/failed flags are written by cmd.exe itself, so they appear even
    // if After Effects has been closed in the meantime.
    std::string cmd;
    cmd += "set PYTHONUNBUFFERED=1&& ";
    cmd += Quote(python_exe) + " " + Quote(corridorkey_dir + "\\infer.py") + args;
    cmd += " > " + Quote(CK_JobLogPath(job_dir)) + " 2>&1";
    cmd += " && (type nul > " + Quote(job_dir + "\\done.flag") + ")";
    cmd += " || (type nul > " + Quote(job_dir + "\\failed.flag") + ")";

    unsigned long pid = 0;
    if (!CK_LaunchHidden(cmd, corridorkey_dir, &pid))
    {
        error = "Couldn't start CorridorKey. Check the python path in Global Settings.";
        return false;
    }

    std::snprintf(num, sizeof(num), "%lu", pid);
    CK_WriteTextFile(job_dir + "\\pid.txt", num);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache.erase(job_dir);
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
            st.state = CK_JOB_DONE;
        else if (CK_FileExists(job_dir + "\\failed.flag"))
            st.state = CK_JOB_FAILED;
        else
        {
            unsigned long pid = std::strtoul(
                CK_ReadTextFile(job_dir + "\\pid.txt").c_str(), NULL, 10);
            st.state = CK_ProcessAlive(pid) ? CK_JOB_RUNNING : CK_JOB_INTERRUPTED;
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
            if (st.frames_done <= 0)
                std::snprintf(buf, sizeof(buf), "Keying... analysing clip");
            else if (frames_total > 0)
                std::snprintf(buf, sizeof(buf), "Keying... %d / %d frames (%d%%)",
                              st.frames_done, frames_total,
                              (int)(100.0 * st.frames_done / frames_total));
            else
                std::snprintf(buf, sizeof(buf), "Keying... %d frames", st.frames_done);
            break;
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
