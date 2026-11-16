#include "CorridorKeyAE.h"

typedef struct {
    unsigned long index;
    char str[256];
} TableString;

TableString g_strs[StrID_NUMTYPES] = {
    StrID_NONE,                  "",
    StrID_Name,                  "CorridorKey_MinionsTake",
    StrID_Description,           "After Effects wrapper for CorridorKey 2.0 by Corridor Digital.\rPersonal build, Minion LLC 2026",
    StrID_KeyClip_Name,          "Key Clip",
    StrID_KeyClip_Button,        "Key Clip",
    StrID_PreviewFrame_Name,     "Preview",
    StrID_PreviewFrame_Button,   "Preview Frame",
    StrID_KeySource_Name,        "Key Source",
    StrID_KeySource_Choices,     "Auto|Source File|Rendered Layer (what you see)",
    StrID_OutputMode_Name,       "Output",
    StrID_OutputMode_Choices,    "Cutout|Alpha|Foreground|Checker|Original",
    StrID_WorkingSpace_Name,     "Working Space",
    StrID_WorkingSpace_Choices,  "sRGB (not linearized)|Linear",
    StrID_Despill_Name,          "Despill",
    StrID_ScreenColour_Name,     "Screen Colour",
    StrID_ScreenColour_Choices,  "Green|Blue|Picked",
    StrID_PickedColour_Name,     "Picked Colour",
    StrID_DespillMethod_Name,    "Despill Method",
    StrID_DespillMethod_Choices, "Average|Gentle (keeps yellows)|Strong|Off",
    StrID_MatteChoke_Name,       "Matte Choke",
    StrID_Despeckle_Name,        "Despeckle Size (px)",
    StrID_KeepOriginal_Name,     "Keep Original in Solid Areas",
    StrID_MatteGamma_Name,       "Matte Gamma",
    StrID_HintMask_Name,         "Hint Mask",
    StrID_HintQuality_Name,      "Hint Strength",
    StrID_LowVRAM_Name,          "Low VRAM (fixed window)",
    StrID_Cache_Name,            "Cache",
    StrID_CacheLocation_Name,    "Save Keys In",
    StrID_CacheLocation_Choices, "Project Folder|Global Cache|Custom Folder",
    StrID_ChooseCache_Name,      "Custom Folder",
    StrID_ChooseCache_Button,    "Choose...",
    StrID_CompactKeys_Name,      "Compact Keys (smaller files)",
    StrID_Status_Name,           "Status",
    StrID_HintGroup_Name,        "Hint",
    StrID_HintSource_Name,       "Hint Source",
    StrID_HintSource_Choices,    "Off|Mask|Matte Layer (first frame)|Matte Layer (every frame)",
    StrID_CarryQuality_Name,     "Carry Strength",
    StrID_ExactHint_Name,        "Follow Matte Exactly (every frame)",
    StrID_KeyRes_Name,           "Key Resolution",
    StrID_KeyRes_Choices,        "Full|75% (draft)|50% (draft)|25% (draft)",
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
