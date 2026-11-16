#pragma once

#ifndef CORRIDORKEYAE_H
#define CORRIDORKEYAE_H

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectUI.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_Macros.h"
#include "AEGP_SuiteHandler.h"
#include "Param_Utils.h"
#include "AEFX_SuiteHelper.h"
#include "String_Utils.h"
#include "CorridorKeyAE_Strings.h"

#define MAJOR_VERSION 0
#define MINOR_VERSION 15
#define BUG_VERSION 0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 0

// Internal ID AE uses to find the effect in saved projects. Never change it,
// even though the display name is now CorridorKey_MinionsTake.
#define CK_MATCH_NAME "Minion CorridorKeyAE"
#define CK_PATH_MAX 1024
#define CK_ARB_VERSION 2

// Per-effect state that persists with the project and does not animate.
// Fixed-size POD so flatten/unflatten is a straight copy. Only ever append
// fields; unflatten zero-fills anything an older project didn't have.
typedef struct CK_EffectData
{
    A_long version;
    char cache_override[CK_PATH_MAX];   // UTF-8, empty = use global default
    // v2
    char job_dir[CK_PATH_MAX];          // UTF-8 folder of the current key job
    A_long frames_total;                // expected frame count of that job
    A_long generation;                  // bumps when a job starts/finishes (forces re-render)
    A_long fps_x1000;                   // source frame rate * 1000 (0 = unknown)
    A_long src_start;                   // source frame of cached frame 0 (trimmed keys)
    A_long preview_time;                // Preview Frame: layer time to preview...
    A_long preview_scale;               // ...in this time scale
    A_long preview_token;               // bumps on every Preview Frame click (0 = never)
    A_long preview_owner;               // per-effect id for the Status line's preview info
    A_long preview_comp_frame;          // comp frame number that was clicked (for display)
    A_long reserved[1];
} CK_EffectData;

// Parameter order (indices). Saved projects are matched by disk ID, not by
// index, so params can be regrouped as long as every disk ID stays the same.
enum
{
    CK_INPUT = 0,
    CK_KEY_CLIP,
    CK_STATUS,              // arbitrary param: draws status, holds CK_EffectData
    CK_PREVIEW_FRAME,
    CK_OUTPUT_MODE,
    CK_WORKING_SPACE,
    CK_DESPILL,
    CK_SCREEN_COLOUR,
    CK_PICKED_COLOUR,
    CK_DESPILL_METHOD,
    CK_MATTE_CHOKE,
    CK_MATTE_GAMMA,
    CK_DESPECKLE,
    CK_KEEP_ORIGINAL,
    // Hint group
    CK_HINT_START,
    CK_HINT_SOURCE,
    CK_HINT_MASK,
    CK_HINT_LAYER,
    CK_HINT_QUALITY,        // shown as "Hint Strength"
    CK_CARRY_QUALITY,       // shown as "Carry Strength"
    CK_EXACT_HINT,          // every-frame hint: no carried model matte (--no_carry_hint)
    CK_HINT_END,
    // Key Settings group
    CK_RANGE_START,
    CK_KEY_SOURCE,
    CK_KEY_RANGE,
    CK_HANDLES,
    CK_TEMPORAL,
    CK_LOW_VRAM,
    CK_RANGE_END,
    // Cache group
    CK_CACHE_START,
    CK_CACHE_LOCATION,
    CK_CHOOSE_CACHE,        // shown as "Custom Folder"
    CK_COMPACT_KEYS,        // clear unused fg background once keyed (v0.15)
    CK_CACHE_END,
    CK_NUM_PARAMS
};

// Disk IDs: never change or reuse a value. Retired ones stay listed.
enum
{
    KEY_CLIP_DISK_ID = 1,
    OUTPUT_MODE_DISK_ID,
    DESPILL_DISK_ID,
    MATTE_CHOKE_DISK_ID,
    MATTE_GAMMA_DISK_ID,
    KEY_SETTINGS_START_DISK_ID,     // now the Hint group start
    HINT_MASK_DISK_ID,
    HINT_QUALITY_DISK_ID,
    LOW_VRAM_DISK_ID,
    KEY_SETTINGS_END_DISK_ID,       // now the Hint group end
    CACHE_START_DISK_ID,
    CHOOSE_CACHE_DISK_ID,
    RESET_CACHE_DISK_ID,            // retired (v0.14)
    EDIT_SETTINGS_DISK_ID,          // retired (v0.14)
    STATUS_DISK_ID,
    CACHE_END_DISK_ID,
    WORKING_SPACE_DISK_ID,
    HINTL_START_DISK_ID,            // retired (v0.5 Hint Layer group)
    HINT_LAYER_DISK_ID,
    HINT_LAYER_USE_DISK_ID,         // retired (replaced by Hint Source)
    HINTL_END_DISK_ID,              // retired
    RANGE_START_DISK_ID,            // Key Settings group start
    KEY_RANGE_DISK_ID,
    HANDLES_DISK_ID,
    RANGE_END_DISK_ID,              // Key Settings group end
    // v0.8
    HINT_SOURCE_DISK_ID,
    CARRY_QUALITY_DISK_ID,
    TEMPORAL_DISK_ID,
    EXACT_HINT_DISK_ID,
    // v0.10
    SCREEN_COLOUR_DISK_ID,
    PICKED_COLOUR_DISK_ID,
    DESPILL_METHOD_DISK_ID,
    DESPECKLE_DISK_ID,
    KEEP_ORIGINAL_DISK_ID,
    // v0.11
    PREVIEW_FRAME_DISK_ID,
    KEY_SOURCE_DISK_ID,
    // v0.14
    CACHE_LOCATION_DISK_ID,
    // v0.15
    COMPACT_KEYS_DISK_ID
};

// Cache Location popup: where Key Clip writes new keys
enum
{
    CK_CL_PROJECT = 1,      // <project folder>\CorridorKeys (global cache if unsaved)
    CK_CL_GLOBAL,           // cache_root from settings.ini
    CK_CL_CUSTOM,           // this effect's cache_override
    CK_CL_NUM = CK_CL_CUSTOM
};

// Key Source popup
enum
{
    CK_KS_AUTO = 1,         // footage file if there is one, else render the layer
    CK_KS_FILE,
    CK_KS_RENDERED,
    CK_KS_NUM = CK_KS_RENDERED
};

// Screen Colour popup
enum
{
    CK_SC_GREEN = 1,
    CK_SC_BLUE,
    CK_SC_PICKED,
    CK_SC_NUM = CK_SC_PICKED
};

// Despill Method popup
enum
{
    CK_DM_AVERAGE = 1,
    CK_DM_GENTLE,
    CK_DM_STRONG,
    CK_DM_OFF,              // v0.13, appended so saved values don't shift
    CK_DM_NUM = CK_DM_OFF
};

// Hint Source popup
enum
{
    CK_HS_OFF = 1,
    CK_HS_MASK,
    CK_HS_LAYER_FIRST,
    CK_HS_LAYER_EVERY,
    CK_HS_NUM = CK_HS_LAYER_EVERY
};

// Temporal Window popup: frames CorridorKey looks at together
enum
{
    CK_TW_AUTO = 1,
    CK_TW_80,
    CK_TW_48,
    CK_TW_24,
    CK_TW_8,
    CK_TW_1,
    CK_TW_NUM = CK_TW_1
};

// Key Range popup
enum
{
    CK_RANGE_LAYER = 1,     // layer in/out + handles
    CK_RANGE_WHOLE,         // entire source clip
    CK_RANGE_NUM = CK_RANGE_WHOLE
};


// Working Space popup
enum
{
    CK_SPACE_SRGB = 1,      // project not linearized: encode results to sRGB
    CK_SPACE_LINEAR,        // linearized project: pass scene-linear values
    CK_SPACE_NUM = CK_SPACE_LINEAR
};

// Output popup (1-based, as AE stores popups)
enum
{
    CK_OUT_CUTOUT = 1,
    CK_OUT_ALPHA,
    CK_OUT_FOREGROUND,
    CK_OUT_CHECKER,
    CK_OUT_ORIGINAL,
    CK_OUT_NUM = CK_OUT_ORIGINAL
};

#define CK_STATUS_UI_WIDTH  260
#define CK_STATUS_UI_HEIGHT 22

// Shared between CorridorKeyAE.cpp and CorridorKeyAE_UI.cpp
bool CK_ReadEffectData(PF_InData* in_data, PF_ParamDef* params[], CK_EffectData* out);
PF_Err CK_WriteEffectData(PF_InData* in_data, PF_ParamDef* params[], const CK_EffectData& data);

PF_Err CK_HandleEvent(PF_InData* in_data,
                      PF_OutData* out_data,
                      PF_ParamDef* params[],
                      PF_LayerDef* output,
                      PF_EventExtra* extra);

extern "C" {
    DllExport PF_Err EffectMain(
        PF_Cmd cmd,
        PF_InData* in_data,
        PF_OutData* out_data,
        PF_ParamDef* params[],
        PF_LayerDef* output,
        void* extra);
}

#endif
