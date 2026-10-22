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
    StrID_WorkingSpace_Name,     "Working Space",
    StrID_WorkingSpace_Choices,  "sRGB (not linearized)|Linear",
    StrID_Despill_Name,          "Despill",
    StrID_MatteChoke_Name,       "Matte Choke",
    StrID_MatteGamma_Name,       "Matte Gamma",
    StrID_HintMask_Name,         "Hint Mask",
    StrID_HintQuality_Name,      "Hint Strength",
    StrID_LowVRAM_Name,          "Low VRAM (fixed window)",
    StrID_Cache_Name,            "Cache",
    StrID_ChooseCache_Name,      "Cache Folder",
    StrID_ChooseCache_Button,    "Choose...",
    StrID_ResetCache_Name,       "Use Default Cache",
    StrID_ResetCache_Button,     "Reset",
    StrID_EditSettings_Name,     "Global Settings",
    StrID_EditSettings_Button,   "Open...",
    StrID_Status_Name,           "Status",
    StrID_HintGroup_Name,        "Hint",
    StrID_HintSource_Name,       "Hint Source",
    StrID_HintSource_Choices,    "Off|Mask|Matte Layer (first frame)|Matte Layer (every frame)",
    StrID_CarryQuality_Name,     "Carry Strength",
    StrID_Temporal_Name,         "Temporal Window",
    StrID_Temporal_Choices,      "Auto (from free VRAM)|80 frames (max)|48 frames|24 frames|8 frames|1 frame (no temporal)",
    StrID_HintLayer_Name,        "Matte Layer",
    StrID_RangeGroup_Name,       "Key Settings",
    StrID_KeyRange_Name,         "Range",
    StrID_KeyRange_Choices,      "Layer In/Out|Whole Clip",
    StrID_Handles_Name,          "Handles (frames)",
    StrID_Err_LoadSuite,         "Error loading suite.",
    StrID_Err_FreeSuite,         "Error releasing suite."
};

char* GetStringPtr(int strNum)
{
    return g_strs[strNum].str;
}
