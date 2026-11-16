#include "CK_Preview.h"
#include "CK_Platform.h"
#include "CK_PreviewScript.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace
{
    const unsigned long kIdleSeconds = 600;

    std::string Dir()           { return CK_LocalAppDataDir() + "\\preview"; }
    std::string PidFile()       { return Dir() + "\\pid.txt"; }
    std::string PortFile()      { return Dir() + "\\port.txt"; }
    std::string StoppedFlag()   { return Dir() + "\\stopped.flag"; }
    std::string ScriptFile()    { return Dir() + "\\ck_preview_server.py"; }

    std::string Quote(const std::string& s) { return "\"" + s + "\""; }

    std::string JsonEscape(const std::string& s)
    {
        std::string o;
        for (char c : s)
        {
            if (c == '\\' || c == '"') { o += '\\'; o += c; }
            else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
            else o += c;
        }
        return o;
    }

    unsigned long ReadNumber(const std::string& file)
    {
        return std::strtoul(CK_ReadTextFile(file).c_str(), NULL, 10);
    }

    std::mutex g_mutex;
    unsigned long g_tick = 0;
    CK_PreviewState g_state = CK_PV_OFF;
    int g_port = 0;

    CK_PreviewState QueryNow(int& port)
    {
        port = 0;
        const unsigned long pid = ReadNumber(PidFile());
        if (pid && CK_ProcessAlive(pid))
        {
            port = (int)ReadNumber(PortFile());
            return port > 0 ? CK_PV_READY : CK_PV_LOADING;
        }
        if (pid && !CK_FileExists(StoppedFlag()))
            return CK_PV_FAILED;
        return CK_PV_OFF;
    }

    void Invalidate()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_tick = 0;
    }
}

std::string CK_PreviewLogPath()
{
    return Dir() + "\\server_log.txt";
}

CK_PreviewState CK_PreviewQuery()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const unsigned long now = CK_TickMs();
    if (g_tick == 0 || now - g_tick > 500)
    {
        g_state = QueryNow(g_port);
        g_tick = now ? now : 1;
    }
    return g_state;
}

bool CK_PreviewStart(const CK_GlobalSettings& gs, std::string& error)
{
    int port = 0;
    const CK_PreviewState st = QueryNow(port);
    if (st == CK_PV_LOADING || st == CK_PV_READY)
        return true;

    const std::string python = CK_ResolvePythonExe(gs);
    if (!CK_MakeDirs(Dir()) || !CK_WriteTextFile(ScriptFile(), kCK_PreviewScript))
    {
        error = "Couldn't write the preview helper to:\n  " + Dir();
        return false;
    }
    CK_RemoveFile(PortFile());
    CK_RemoveFile(StoppedFlag());

    char idle[32];
    std::snprintf(idle, sizeof(idle), "%lu", kIdleSeconds);
    std::string cmd = "set PYTHONUNBUFFERED=1&& " + Quote(python) + " " + Quote(ScriptFile()) +
                      " --ck_dir " + Quote(gs.corridorkey_dir) +
                      " --state_dir " + Quote(Dir()) +
                      " --idle " + idle +
                      " > " + Quote(CK_PreviewLogPath()) + " 2>&1";

    unsigned long pid = 0;
    if (!CK_LaunchHidden(cmd, gs.corridorkey_dir, &pid))
    {
        error = "Couldn't start the preview helper. Check the python path in Global Settings.";
        return false;
    }
    char num[32];
    std::snprintf(num, sizeof(num), "%lu", pid);
    CK_WriteTextFile(PidFile(), num);
    Invalidate();
    return true;
}

void CK_PreviewStop()
{
    int port = 0;
    const CK_PreviewState st = QueryNow(port);
    if (st == CK_PV_READY)
    {
        std::string reply;
        CK_TcpRequest(port, "{\"cmd\": \"quit\"}", 3000, reply);
    }
    const unsigned long pid = ReadNumber(PidFile());
    if (pid && CK_ProcessAlive(pid))
        CK_KillProcessTree(pid);
    CK_RemoveFile(PortFile());
    if (pid)
        CK_WriteTextFile(StoppedFlag(), "");
    Invalidate();
}

bool CK_PreviewKey(const std::string& input_dir, const std::string& output_dir,
                   bool linear, std::string& error)
{
    int port = 0;
    if (QueryNow(port) != CK_PV_READY)
    {
        error = "preview helper isn't ready";
        return false;
    }
    const std::string req = "{\"cmd\": \"key\", \"input_dir\": \"" + JsonEscape(input_dir) +
                            "\", \"output_dir\": \"" + JsonEscape(output_dir) +
                            "\", \"linear\": " + (linear ? "true" : "false") + "}";
    std::string reply;
    // Generous: the first frame after loading includes GPU warm-up.
    if (!CK_TcpRequest(port, req, 180000, reply))
    {
        error = "no reply from the preview helper";
        return false;
    }
    if (reply.find("\"ok\": true") == std::string::npos)
    {
        error = reply;
        return false;
    }
    return true;
}
