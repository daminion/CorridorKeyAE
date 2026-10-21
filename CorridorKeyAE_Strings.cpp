#include "CorridorKeyAE.h"

typedef struct {
    unsigned long index;
    char str[256];
} TableString;

TableString g_strs[StrID_NUMTYPES] = {
    StrID_NONE,                  "",
    StrID_Name,                  "CorridorKey AE",
    StrID_Description,           "After Effects wrapper for CorridorKey 2.0 by Corridor Digital.\rPersonal build, Minion LLC 2026",
    StrID_KeyClip_Name,          "Key Clip",
    StrID_KeyClip_Button,        "Key Clip",
    StrID_OutputMode_Name,       "Output",
    StrID_OutputMode_Choices,    "Cutout|Alpha|Foreground|Checker|Original",
    StrID_Despill_Name,          "Despill",
    StrID_MatteChoke_Name,       "Matte Choke",
    StrID_MatteGamma_Name,       "Matte Gamma",
    StrID_KeySettings_Name,      "Key Settings",
    StrID_HintMask_Name,         "Hint Mask",
    StrID_HintQuality_Name,      "Hint Quality",
    StrID_LowVRAM_Name,          "Low VRAM",
    StrID_Cache_Name,            "Cache",
    StrID_ChooseCache_Name,      "Cache Folder",
    StrID_ChooseCache_Button,    "Choose...",
    StrID_ResetCache_Name,       "Use Default Cache",
    StrID_ResetCache_Button,     "Reset",
    StrID_EditSettings_Name,     "Global Settings",
    StrID_EditSettings_Button,   "Open...",
    StrID_Status_Name,           "Status",
    StrID_Err_LoadSuite,         "Error loading suite.",
    StrID_Err_FreeSuite,         "Error releasing suite."
};

char* GetStringPtr(int strNum)
{
    return g_strs[strNum].str;
}
