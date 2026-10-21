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
#define MINOR_VERSION 1
#define BUG_VERSION 0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 0

#define CK_MATCH_NAME "Minion CorridorKeyAE"
#define CK_PATH_MAX 1024
#define CK_ARB_VERSION 1

// Per-effect settings that must persist with the project and do not
// animate. Fixed-size POD so flatten/unflatten is a straight copy.
typedef struct CK_EffectData
{
    A_long version;
    char cache_override[CK_PATH_MAX];   // UTF-8, empty = use global default
} CK_EffectData;

// Parameter order. Never reorder or remove entries once projects exist;
// add new ones at the end (before CK_NUM_PARAMS) with new disk IDs.
enum
{
    CK_INPUT = 0,
    CK_KEY_CLIP,
    CK_OUTPUT_MODE,
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
    CK_EFFECT_DATA,
    CK_CACHE_END,
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
    EFFECT_DATA_DISK_ID,
    CACHE_END_DISK_ID
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
