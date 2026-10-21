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
