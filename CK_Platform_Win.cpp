// Windows implementation of CK_Platform.h

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstring>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>

#include "CK_Platform.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
    std::wstring Widen(const std::string& s)
    {
        if (s.empty()) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
        return w;
    }

    std::string Narrow(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
        return s;
    }

    std::wstring KnownFolder(REFKNOWNFOLDERID id)
    {
        PWSTR p = NULL;
        std::wstring out;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, NULL, &p)) && p)
        {
            out = p;
        }
        if (p) CoTaskMemFree(p);
        return out;
    }

    std::wstring ReadIni(const std::wstring& file, const wchar_t* key)
    {
        wchar_t buf[2048] = { 0 };
        GetPrivateProfileStringW(L"paths", key, L"", buf, 2048, file.c_str());
        std::wstring v(buf);
        // trim whitespace and surrounding quotes
        while (!v.empty() && (v.back() == L' ' || v.back() == L'\t')) v.pop_back();
        size_t a = v.find_first_not_of(L" \t");
        v = (a == std::wstring::npos) ? std::wstring() : v.substr(a);
        if (v.size() >= 2 && v.front() == L'"' && v.back() == L'"')
            v = v.substr(1, v.size() - 2);
        return v;
    }

    std::wstring StripTrailingSlash(std::wstring p)
    {
        while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
        return p;
    }
}

CK_GlobalSettings CK_LoadGlobalSettings()
{
    CK_GlobalSettings gs;

    std::wstring dir = KnownFolder(FOLDERID_RoamingAppData) + L"\\CorridorKeyAE";
    std::wstring file = dir + L"\\settings.ini";
    gs.settings_file = Narrow(file);

    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        CreateDirectoryW(dir.c_str(), NULL);

        const char* tmpl =
            "; CorridorKey AE global settings\r\n"
            "; Paths may be left blank where noted. Restart is not needed;\r\n"
            "; the effect re-reads this file each time it needs it.\r\n"
            "\r\n"
            "[paths]\r\n"
            "; Folder of your CorridorKey2.0 clone (the one containing infer.py)\r\n"
            "corridorkey_dir=\r\n"
            "\r\n"
            "; Optional. Blank = <corridorkey_dir>\\venv\\Scripts\\python.exe\r\n"
            "python=\r\n"
            "\r\n"
            "; Default cache folder for keyed results, e.g. a fast external drive.\r\n"
            "; Blank = %LOCALAPPDATA%\\CorridorKeyAE\\Cache\r\n"
            "; Each effect can override this with Cache > Cache Folder > Choose...\r\n"
            "cache_root=\r\n";

        HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(h, tmpl, (DWORD)strlen(tmpl), &written, NULL);
            CloseHandle(h);
        }
    }

    gs.cache_root      = Narrow(StripTrailingSlash(ReadIni(file, L"cache_root")));
    gs.corridorkey_dir = Narrow(StripTrailingSlash(ReadIni(file, L"corridorkey_dir")));
    gs.python_exe      = Narrow(ReadIni(file, L"python"));
    return gs;
}

std::string CK_DefaultCacheRoot()
{
    return Narrow(KnownFolder(FOLDERID_LocalAppData) + L"\\CorridorKeyAE\\Cache");
}

std::string CK_ResolveCacheRoot(const char* effect_override_utf8,
                                const CK_GlobalSettings& gs)
{
    if (effect_override_utf8 && effect_override_utf8[0])
        return std::string(effect_override_utf8);
    if (!gs.cache_root.empty())
        return gs.cache_root;
    return CK_DefaultCacheRoot();
}

std::string CK_ResolvePythonExe(const CK_GlobalSettings& gs)
{
    if (!gs.python_exe.empty())
        return gs.python_exe;
    if (gs.corridorkey_dir.empty())
        return std::string();
    return gs.corridorkey_dir + "\\venv\\Scripts\\python.exe";
}

bool CK_DirExists(const std::string& path_utf8)
{
    if (path_utf8.empty()) return false;
    DWORD a = GetFileAttributesW(Widen(path_utf8).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool CK_FileExists(const std::string& path_utf8)
{
    if (path_utf8.empty()) return false;
    DWORD a = GetFileAttributesW(Widen(path_utf8).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool CK_ChooseFolder(const std::string& start_utf8, std::string& chosen_utf8)
{
    // AE has normally initialized COM on the UI thread already; balance only
    // if this call actually initialized it.
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool picked = false;

    IFileOpenDialog* dlg = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dlg))))
    {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dlg->SetTitle(L"CorridorKey AE - Choose cache folder");

        if (!start_utf8.empty() && CK_DirExists(start_utf8))
        {
            IShellItem* startItem = NULL;
            if (SUCCEEDED(SHCreateItemFromParsingName(Widen(start_utf8).c_str(), NULL,
                                                      IID_PPV_ARGS(&startItem))))
            {
                dlg->SetFolder(startItem);
                startItem->Release();
            }
        }

        if (SUCCEEDED(dlg->Show(GetActiveWindow())))
        {
            IShellItem* item = NULL;
            if (SUCCEEDED(dlg->GetResult(&item)))
            {
                PWSTR path = NULL;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    chosen_utf8 = Narrow(StripTrailingSlash(path));
                    picked = !chosen_utf8.empty();
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
    }

    if (SUCCEEDED(init)) CoUninitialize();
    return picked;
}

void CK_OpenSettingsFile(const CK_GlobalSettings& gs)
{
    ShellExecuteW(NULL, L"open", L"notepad.exe",
                  (L"\"" + Widen(gs.settings_file) + L"\"").c_str(),
                  NULL, SW_SHOWNORMAL);
}

void CK_ShowMessage(const char* title_utf8, const std::string& body_utf8)
{
    MessageBoxW(GetActiveWindow(), Widen(body_utf8).c_str(),
                Widen(title_utf8 ? title_utf8 : "CorridorKey AE").c_str(),
                MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
}
