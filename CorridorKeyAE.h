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
#define MINOR_VERSION 7
#define BUG_VERSION 0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 0

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
    A_long reserved[6];
} CK_EffectData;

// Parameter order. Fixed from milestone 3 on: only append new params
// (before CK_NUM_PARAMS) with new disk IDs.
enum
{
    CK_INPUT = 0,
    CK_KEY_CLIP,
    CK_STATUS,              // arbitrary param: draws status, holds CK_EffectData
    CK_OUTPUT_MODE,
    CK_WORKING_SPACE,
    CK_DESPILL,
    CK_MATTE_CHOKE,
    CK_MATTE_GAMMA,
    CK_KEY_SETTINGS_START,
    CK_HINT_MASK,
    CK_HINT_QUALITY,
    CK_LOW_VRAM,
    CK_KEY_SETTINGS_END,
    CK_CACHE_START,
    CK_CHOOSE_CACHE,
    CK_RESET_CACHE,
    CK_EDIT_SETTINGS,
    CK_CACHE_END,
    // v0.5 (appended)
    CK_HINTL_START,
    CK_HINT_LAYER,
    CK_HINT_LAYER_USE,
    CK_HINTL_END,
    // v0.6 (appended)
    CK_RANGE_START,
    CK_KEY_RANGE,
    CK_HANDLES,
    CK_RANGE_END,
    CK_NUM_PARAMS
};

enum
{
    KEY_CLIP_DISK_ID = 1,
    OUTPUT_MODE_DISK_ID,
    DESPILL_DISK_ID,
    MATTE_CHOKE_DISK_ID,
    MATTE_GAMMA_DISK_ID,
    KEY_SETTINGS_START_DISK_ID,
    HINT_MASK_DISK_ID,
    HINT_QUALITY_DISK_ID,
    LOW_VRAM_DISK_ID,
    KEY_SETTINGS_END_DISK_ID,
    CACHE_START_DISK_ID,
    CHOOSE_CACHE_DISK_ID,
    RESET_CACHE_DISK_ID,
    EDIT_SETTINGS_DISK_ID,
    STATUS_DISK_ID,
    CACHE_END_DISK_ID,
    WORKING_SPACE_DISK_ID,
    HINTL_START_DISK_ID,
    HINT_LAYER_DISK_ID,
    HINT_LAYER_USE_DISK_ID,
    HINTL_END_DISK_ID,
    RANGE_START_DISK_ID,
    KEY_RANGE_DISK_ID,
    HANDLES_DISK_ID,
    RANGE_END_DISK_ID
};

// Key Range popup
enum
{
    CK_RANGE_LAYER = 1,     // layer in/out + handles
    CK_RANGE_WHOLE,         // entire source clip
    CK_RANGE_NUM = CK_RANGE_WHOLE
};

// Hint Layer > Use popup
enum
{
    CK_HL_OFF = 1,
    CK_HL_FIRST_FRAME,
    CK_HL_EVERY_FRAME,
    CK_HL_NUM = CK_HL_EVERY_FRAME
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
