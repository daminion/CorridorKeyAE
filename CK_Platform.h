#pragma once

// Platform helpers for CorridorKeyAE. Everything is UTF-8 std::string so the
// effect code never touches windows.h. Windows implementation lives in
// CK_Platform_Win.cpp; a Mac implementation can be added later.

#include <string>

struct CK_GlobalSettings
{
    std::string settings_file;    // full path of settings.ini
    std::string cache_root;       // global default cache folder (may be empty)
    std::string corridorkey_dir;  // CorridorKey2.0 repo folder (contains infer.py)
    std::string python_exe;       // python.exe; empty = <corridorkey_dir>\venv\Scripts\python.exe
};

// Loads %APPDATA%\CorridorKeyAE\settings.ini, creating it with blank values
// the first time.
CK_GlobalSettings CK_LoadGlobalSettings();

// Fallback when settings.ini has no cache_root: %LOCALAPPDATA%\CorridorKeyAE\Cache
std::string CK_DefaultCacheRoot();

// Per-effect override wins, then settings.ini, then the built-in default.
std::string CK_ResolveCacheRoot(const char* effect_override_utf8,
                                const CK_GlobalSettings& gs);

std::string CK_ResolvePythonExe(const CK_GlobalSettings& gs);

bool CK_DirExists(const std::string& path_utf8);
bool CK_FileExists(const std::string& path_utf8);

// Shows a folder picker. Returns false if the user cancelled.
bool CK_ChooseFolder(const std::string& start_utf8, std::string& chosen_utf8);

// Opens settings.ini in the default text editor.
void CK_OpenSettingsFile(const CK_GlobalSettings& gs);

void CK_ShowMessage(const char* title_utf8, const std::string& body_utf8);

// ---- milestone 2: background jobs --------------------------------------

bool CK_MakeDirs(const std::string& path_utf8);
bool CK_RemoveFile(const std::string& path_utf8);
bool CK_WriteTextFile(const std::string& path_utf8, const std::string& text);
std::string CK_ReadTextFile(const std::string& path_utf8);   // small files only

// "<size>-<mtime>" for change detection; empty if the file is missing.
std::string CK_FileStamp(const std::string& path_utf8);

// Runs a cmd.exe command line hidden, in workdir. Returns false on failure.
bool CK_LaunchHidden(const std::string& cmd_line_utf8,
                     const std::string& workdir_utf8,
                     unsigned long* pid_out);
bool CK_ProcessAlive(unsigned long pid);
void CK_KillProcessTree(unsigned long pid);

// Full path of the first subfolder of dir whose name ends with suffix ("" if none).
std::string CK_FindSubdir(const std::string& dir_utf8, const char* suffix);

// Counts *.exr in the first subfolder of dir whose name ends with suffix.
int CK_CountExrInSubdir(const std::string& dir_utf8, const char* suffix);

unsigned long CK_TickMs();
bool CK_AskYesNo(const char* title_utf8, const std::string& body_utf8);
void CK_OpenInEditor(const std::string& file_utf8);
void CK_OpenFolder(const std::string& dir_utf8);

// AE hands file paths over as null-terminated UTF-16.
std::string CK_FromUtf16(const unsigned short* s);
