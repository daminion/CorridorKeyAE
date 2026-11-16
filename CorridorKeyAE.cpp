// CorridorKey AE - After Effects wrapper for CorridorKey 2.0
//
// Key Clip runs CorridorKey in the background into the cache folder, the
// Status control shows progress, and the render draws the cached EXRs with
// the live controls (Output, Working Space, Despill, Matte Choke/Gamma).

#include "CorridorKeyAE.h"
#include "AE_EffectSuites.h"
#include "CK_Platform.h"
#include "CK_Job.h"
#include "CK_Frames.h"
#include "CK_Render.h"
#include "CK_Hint.h"
#include "CK_Preview.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <cstring>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Effect data (arbitrary param) helpers
// ---------------------------------------------------------------------------

static void InitEffectData(CK_EffectData* d)
{
    std::memset(d, 0, sizeof(CK_EffectData));
    d->version = CK_ARB_VERSION;
}

static PF_Err NewEffectData(PF_InData* in_data, PF_ArbitraryH* arbPH)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);

    *arbPH = suites.HandleSuite1()->host_new_handle(sizeof(CK_EffectData));
    if (!*arbPH)
        return PF_Err_OUT_OF_MEMORY;

    CK_EffectData* d = reinterpret_cast<CK_EffectData*>(
        suites.HandleSuite1()->host_lock_handle(*arbPH));
    if (!d)
        return PF_Err_OUT_OF_MEMORY;

    InitEffectData(d);
    suites.HandleSuite1()->host_unlock_handle(*arbPH);
    return PF_Err_NONE;
}

static PF_Err CopyEffectData(PF_InData* in_data,
                             PF_ArbitraryH srcH,
                             PF_ArbitraryH* dstPH)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Err err = NewEffectData(in_data, dstPH);
    if (err || !srcH)
        return err;

    CK_EffectData* src = reinterpret_cast<CK_EffectData*>(
        suites.HandleSuite1()->host_lock_handle(srcH));
    CK_EffectData* dst = reinterpret_cast<CK_EffectData*>(
        suites.HandleSuite1()->host_lock_handle(*dstPH));

    if (src && dst)
        std::memcpy(dst, src, sizeof(CK_EffectData));

    if (dst) suites.HandleSuite1()->host_unlock_handle(*dstPH);
    if (src) suites.HandleSuite1()->host_unlock_handle(srcH);
    return err;
}

static PF_Err HandleArbitrary(PF_InData* in_data,
                              PF_OutData* out_data,
                              PF_ArbParamsExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);

    switch (extra->which_function)
    {
        case PF_Arbitrary_NEW_FUNC:
            err = NewEffectData(in_data, extra->u.new_func_params.arbPH);
            break;

        case PF_Arbitrary_DISPOSE_FUNC:
            if (extra->u.dispose_func_params.arbH)
                suites.HandleSuite1()->host_dispose_handle(
                    extra->u.dispose_func_params.arbH);
            break;

        case PF_Arbitrary_COPY_FUNC:
            err = CopyEffectData(in_data,
                                 extra->u.copy_func_params.src_arbH,
                                 extra->u.copy_func_params.dst_arbPH);
            break;

        case PF_Arbitrary_FLAT_SIZE_FUNC:
            *(extra->u.flat_size_func_params.flat_data_sizePLu) = sizeof(CK_EffectData);
            break;

        case PF_Arbitrary_FLATTEN_FUNC:
        {
            CK_EffectData* src = reinterpret_cast<CK_EffectData*>(
                suites.HandleSuite1()->host_lock_handle(
                    extra->u.flatten_func_params.arbH));
            if (!src)
                return PF_Err_OUT_OF_MEMORY;

            if (extra->u.flatten_func_params.buf_sizeLu >= sizeof(CK_EffectData))
                std::memcpy(extra->u.flatten_func_params.flat_dataPV, src,
                            sizeof(CK_EffectData));

            suites.HandleSuite1()->host_unlock_handle(extra->u.flatten_func_params.arbH);
            break;
        }

        case PF_Arbitrary_UNFLATTEN_FUNC:
        {
            err = NewEffectData(in_data, extra->u.unflatten_func_params.arbPH);
            if (err)
                break;

            CK_EffectData* dst = reinterpret_cast<CK_EffectData*>(
                suites.HandleSuite1()->host_lock_handle(
                    *extra->u.unflatten_func_params.arbPH));
            if (!dst)
                return PF_Err_OUT_OF_MEMORY;

            const A_u_long size = extra->u.unflatten_func_params.buf_sizeLu;
            if (size >= sizeof(A_long) && extra->u.unflatten_func_params.flat_dataPV)
            {
                const A_u_long n = size < sizeof(CK_EffectData) ? size : sizeof(CK_EffectData);
                std::memcpy(dst, extra->u.unflatten_func_params.flat_dataPV, n);
                dst->version = CK_ARB_VERSION;
                dst->cache_override[CK_PATH_MAX - 1] = '\0';
                dst->job_dir[CK_PATH_MAX - 1] = '\0';
            }

            suites.HandleSuite1()->host_unlock_handle(*extra->u.unflatten_func_params.arbPH);
            break;
        }

        case PF_Arbitrary_INTERP_FUNC:
            // Param cannot time-vary; hold the left value.
            err = CopyEffectData(in_data,
                                 extra->u.interp_func_params.left_arbH,
                                 extra->u.interp_func_params.interpPH);
            break;

        case PF_Arbitrary_COMPARE_FUNC:
        {
            CK_EffectData* a = reinterpret_cast<CK_EffectData*>(
                suites.HandleSuite1()->host_lock_handle(extra->u.compare_func_params.a_arbH));
            CK_EffectData* b = reinterpret_cast<CK_EffectData*>(
                suites.HandleSuite1()->host_lock_handle(extra->u.compare_func_params.b_arbH));

            *(extra->u.compare_func_params.compareP) =
                (a && b && std::memcmp(a, b, sizeof(CK_EffectData)) == 0)
                    ? PF_ArbCompare_EQUAL
                    : PF_ArbCompare_NOT_EQUAL;

            if (b) suites.HandleSuite1()->host_unlock_handle(extra->u.compare_func_params.b_arbH);
            if (a) suites.HandleSuite1()->host_unlock_handle(extra->u.compare_func_params.a_arbH);
            break;
        }

        case PF_Arbitrary_PRINT_SIZE_FUNC:
            *(extra->u.print_size_func_params.print_sizePLu) = CK_PATH_MAX + 64;
            break;

        case PF_Arbitrary_PRINT_FUNC:
        {
            CK_EffectData* d = reinterpret_cast<CK_EffectData*>(
                suites.HandleSuite1()->host_lock_handle(extra->u.print_func_params.arbH));
            if (d)
            {
                std::snprintf(extra->u.print_func_params.print_bufferPC,
                              extra->u.print_func_params.print_sizeLu,
                              "CorridorKeyAE v%d job=%s",
                              static_cast<int>(d->version),
                              d->job_dir[0] ? d->job_dir : "(none)");
                suites.HandleSuite1()->host_unlock_handle(extra->u.print_func_params.arbH);
            }
            break;
        }

        case PF_Arbitrary_SCAN_FUNC:
            err = NewEffectData(in_data, extra->u.scan_func_params.arbPH);
            break;

        default:
            break;
    }

    return err;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

static PF_Err About(PF_InData* in_data, PF_OutData* out_data,
                    PF_ParamDef* params[], PF_LayerDef* output)
{
    PF_SPRINTF(out_data->return_msg,
               "%s v%d.%d\r%s",
               STR(StrID_Name),
               MAJOR_VERSION,
               MINOR_VERSION,
               STR(StrID_Description));
    return PF_Err_NONE;
}

// ---------------------------------------------------------------------------
// Background refresh: AE only sends idle events to the Effect Controls panel
// while you interact with it, so an AEGP idle hook polls running keys and
// refreshes the panels when their progress changes.
// ---------------------------------------------------------------------------

static SPBasicSuite* S_basic = NULL;
static AEGP_PluginID S_plugin_id = 0;
static bool S_idle_registered = false;

// Folder of the open .aep ("" when unsaved). AEGP project calls are main
// thread only, so this runs from the idle hook and Key Clip; render threads
// read the stored copy through CK_GetProjectDir().
static std::string QueryProjectDir(SPBasicSuite* basic)
{
    std::string dir;
    if (!basic)
        return dir;
    try
    {
        AEGP_SuiteHandler suites(basic);
        AEGP_ProjectH projH = NULL;
        AEGP_MemHandle pathH = NULL;
        if (!suites.ProjSuite6()->AEGP_GetProjectByIndex(0, &projH) && projH &&
            !suites.ProjSuite6()->AEGP_GetProjectPath(projH, &pathH) && pathH)
        {
            A_UTF16Char* p = NULL;
            if (!suites.MemorySuite1()->AEGP_LockMemHandle(pathH, reinterpret_cast<void**>(&p)) && p)
            {
                const std::string path = CK_FromUtf16(reinterpret_cast<const unsigned short*>(p));
                const size_t slash = path.find_last_of("\\/");
                if (slash != std::string::npos)
                    dir = path.substr(0, slash);
                suites.MemorySuite1()->AEGP_UnlockMemHandle(pathH);
            }
            suites.MemorySuite1()->AEGP_FreeMemHandle(pathH);
        }
    }
    catch (...)
    {
    }
    return dir;
}

static A_Err CK_IdleHook(AEGP_GlobalRefcon, AEGP_IdleRefcon, A_long* max_sleepPL)
{
    static unsigned long last = 0;
    if (max_sleepPL)
        *max_sleepPL = 500;

    const unsigned long now = CK_TickMs();
    if (now - last < 750)
        return A_Err_NONE;
    last = now;

    static CK_PreviewState last_preview = CK_PV_OFF;
    static unsigned long last_info = 0;
    const CK_PreviewState preview = CK_PreviewQuery();
    const unsigned long info = CK_PreviewInfoVersion();
    const bool preview_changed = preview != last_preview || info != last_info;
    last_preview = preview;
    last_info = info;

    // Project opened, saved elsewhere or closed: keys in its CorridorKeys
    // folder resolve differently, so redraw.
    const bool project_changed = CK_SetProjectDir(QueryProjectDir(S_basic));

    if ((CK_PollTrackedJobs() || preview_changed || project_changed) && S_basic)
    {
        try
        {
            AEGP_SuiteHandler suites(S_basic);
            suites.AdvAppSuite2()->PF_RefreshAllWindows();
        }
        catch (...)
        {
        }
    }
    return A_Err_NONE;
}

static void RegisterIdleHook(PF_InData* in_data)
{
    if (S_idle_registered)
        return;
    try
    {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        S_basic = in_data->pica_basicP;
        if (!suites.UtilitySuite6()->AEGP_RegisterWithAEGP(NULL, CK_MATCH_NAME, &S_plugin_id) &&
            !suites.RegisterSuite5()->AEGP_RegisterIdleHook(S_plugin_id, CK_IdleHook, NULL))
        {
            S_idle_registered = true;
        }
    }
    catch (...)
    {
        // No hook: the Status line still updates when the panel is touched.
    }
}

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data,
                          PF_ParamDef* params[], PF_LayerDef* output)
{
    out_data->my_version = PF_VERSION(MAJOR_VERSION,
                                      MINOR_VERSION,
                                      BUG_VERSION,
                                      STAGE_VERSION,
                                      BUILD_VERSION);

    // Must match AE_Effect_Global_OutFlags / _2 in CorridorKeyAE_PiPL.r
    out_data->out_flags  |= PF_OutFlag_DEEP_COLOR_AWARE |
                            PF_OutFlag_CUSTOM_UI;
    out_data->out_flags2 |= PF_OutFlag2_SUPPORTS_SMART_RENDER |
                            PF_OutFlag2_FLOAT_COLOR_AWARE |
                            PF_OutFlag2_I_MIX_GUID_DEPENDENCIES |
                            PF_OutFlag2_SUPPORTS_THREADED_RENDERING;

    RegisterIdleHook(in_data);
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data,
                          PF_ParamDef* params[], PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON(STR(StrID_KeyClip_Name),
                  STR(StrID_KeyClip_Button),
                  0,
                  PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY,
                  KEY_CLIP_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ArbitraryH default_arbH = NULL;
    ERR(NewEffectData(in_data, &default_arbH));
    if (err)
        return err;
    PF_ADD_ARBITRARY2(STR(StrID_Status_Name),
                      CK_STATUS_UI_WIDTH,
                      CK_STATUS_UI_HEIGHT,
                      PF_ParamFlag_CANNOT_TIME_VARY,
                      PF_PUI_CONTROL,
                      default_arbH,
                      STATUS_DISK_ID,
                      NULL);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON(STR(StrID_PreviewFrame_Name),
                  STR(StrID_PreviewFrame_Button),
                  0,
                  PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY,
                  PREVIEW_FRAME_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX(STR(StrID_OutputMode_Name),
                  CK_OUT_NUM,
                  CK_OUT_CUTOUT,
                  STR(StrID_OutputMode_Choices),
                  PF_ParamFlag_CANNOT_TIME_VARY,
                  OUTPUT_MODE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX(STR(StrID_WorkingSpace_Name),
                  CK_SPACE_NUM,
                  CK_SPACE_SRGB,
                  STR(StrID_WorkingSpace_Choices),
                  PF_ParamFlag_CANNOT_TIME_VARY,
                  WORKING_SPACE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_Despill_Name),
                         0, 100, 0, 100, 50,
                         PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_PERCENT,
                         0,
                         DESPILL_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX(STR(StrID_ScreenColour_Name),
                  CK_SC_NUM,
                  CK_SC_GREEN,
                  STR(StrID_ScreenColour_Choices),
                  0,
                  SCREEN_COLOUR_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR(STR(StrID_PickedColour_Name), 40, 170, 70, PICKED_COLOUR_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX(STR(StrID_DespillMethod_Name),
                  CK_DM_NUM,
                  CK_DM_AVERAGE,
                  STR(StrID_DespillMethod_Choices),
                  0,
                  DESPILL_METHOD_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_MatteChoke_Name),
                         -20, 20, -5, 5, 0,
                         PF_Precision_TENTHS,
                         0,
                         0,
                         MATTE_CHOKE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_MatteGamma_Name),
                         0.1, 10, 0.2, 5, 1,
                         PF_Precision_HUNDREDTHS,
                         0,
                         0,
                         MATTE_GAMMA_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER(STR(StrID_Despeckle_Name), 0, 20000, 0, 500, 0, DESPECKLE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    // On for new effects (v0.13): keeps the plate's own grain and detail where the
    // subject is solid. Saved projects keep whatever they had.
    PF_ADD_CHECKBOXX(STR(StrID_KeepOriginal_Name), TRUE, 0, KEEP_ORIGINAL_DISK_ID);

    // --- Hint: tells CorridorKey which subject to keep (read at Key Clip) ---
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC(STR(StrID_HintGroup_Name), KEY_SETTINGS_START_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX(STR(StrID_HintSource_Name),
                  CK_HS_NUM,
                  CK_HS_MASK,
                  STR(StrID_HintSource_Choices),
                  PF_ParamFlag_CANNOT_TIME_VARY,
                  HINT_SOURCE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_PATH;
    def.uu.id = HINT_MASK_DISK_ID;
    PF_STRCPY(def.PF_DEF_NAME, STR(StrID_HintMask_Name));
    def.u.path_d.dephault = 0;
    ERR(PF_ADD_PARAM(in_data, -1, &def));

    AEFX_CLR_STRUCT(def);
    PF_ADD_LAYER(STR(StrID_HintLayer_Name), PF_LayerDefault_NONE, HINT_LAYER_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_HintQuality_Name),
                         0, 100, 0, 100, 75,
                         PF_Precision_INTEGER,
                         PF_ValueDisplayFlag_PERCENT,
                         PF_ParamFlag_CANNOT_TIME_VARY,
                         HINT_QUALITY_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_CarryQuality_Name),
                         0, 100, 0, 100, 95,
                         PF_Precision_INTEGER,
                         PF_ValueDisplayFlag_PERCENT,
                         PF_ParamFlag_CANNOT_TIME_VARY,
                         CARRY_QUALITY_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX(STR(StrID_ExactHint_Name),
                     TRUE,
                     PF_ParamFlag_CANNOT_TIME_VARY,
                     EXACT_HINT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(KEY_SETTINGS_END_DISK_ID);

    // --- Key Settings: range and time window for the next Key Clip ----------
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC(STR(StrID_RangeGroup_Name), RANGE_START_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX(STR(StrID_KeySource_Name),
                  CK_KS_NUM,
                  CK_KS_AUTO,
                  STR(StrID_KeySource_Choices),
                  PF_ParamFlag_CANNOT_TIME_VARY,
                  KEY_SOURCE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX(STR(StrID_KeyRange_Name),
                  CK_RANGE_NUM,
                  CK_RANGE_LAYER,
                  STR(StrID_KeyRange_Choices),
                  PF_ParamFlag_CANNOT_TIME_VARY,
                  KEY_RANGE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER(STR(StrID_Handles_Name), 0, 200, 0, 48, 8, HANDLES_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX(STR(StrID_Temporal_Name),
                  CK_TW_NUM,
                  CK_TW_AUTO,
                  STR(StrID_Temporal_Choices),
                  PF_ParamFlag_CANNOT_TIME_VARY,
                  TEMPORAL_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX(STR(StrID_LowVRAM_Name),
                     FALSE,
                     PF_ParamFlag_CANNOT_TIME_VARY,
                     LOW_VRAM_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(RANGE_END_DISK_ID);

    // --- Cache -------------------------------------------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC(STR(StrID_Cache_Name), CACHE_START_DISK_ID);

    // Read only by Key Clip. The global cache folder is set in the setup menu
    // (option 6) and also holds Preview Frame results.
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX(STR(StrID_CacheLocation_Name),
                  CK_CL_NUM,
                  CK_CL_PROJECT,
                  STR(StrID_CacheLocation_Choices),
                  PF_ParamFlag_CANNOT_TIME_VARY,
                  CACHE_LOCATION_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON(STR(StrID_ChooseCache_Name),
                  STR(StrID_ChooseCache_Button),
                  0,
                  PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY,
                  CHOOSE_CACHE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(CACHE_END_DISK_ID);

    if (!err)
    {
        PF_CustomUIInfo ci;
        AEFX_CLR_STRUCT(ci);
        ci.events = PF_CustomEFlag_EFFECT;
        err = (*(in_data->inter.register_ui))(in_data->effect_ref, &ci);
    }

    out_data->num_params = CK_NUM_PARAMS;
    return err;
}

// ---------------------------------------------------------------------------
// Effect data access (the Status arbitrary param)
// ---------------------------------------------------------------------------

static void SetPathField(char* field, const std::string& value);   // below

bool CK_ReadEffectData(PF_InData* in_data, PF_ParamDef* params[], CK_EffectData* out)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    InitEffectData(out);

    PF_ArbitraryH h = params[CK_STATUS]->u.arb_d.value;
    if (!h)
        return false;

    CK_EffectData* d = reinterpret_cast<CK_EffectData*>(
        suites.HandleSuite1()->host_lock_handle(h));
    if (!d)
        return false;

    std::memcpy(out, d, sizeof(CK_EffectData));
    suites.HandleSuite1()->host_unlock_handle(h);
    out->cache_override[CK_PATH_MAX - 1] = '\0';
    out->job_dir[CK_PATH_MAX - 1] = '\0';

    // A key in a moved/copied project's CorridorKeys folder: point at the
    // copy next to the open project.
    const std::string resolved = CK_ResolveJobDir(out->job_dir);
    if (resolved != out->job_dir && resolved.size() < CK_PATH_MAX)
        SetPathField(out->job_dir, resolved);
    return true;
}

PF_Err CK_WriteEffectData(PF_InData* in_data, PF_ParamDef* params[],
                          const CK_EffectData& data)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_ArbitraryH h = params[CK_STATUS]->u.arb_d.value;
    if (!h)
        return PF_Err_BAD_CALLBACK_PARAM;

    CK_EffectData* d = reinterpret_cast<CK_EffectData*>(
        suites.HandleSuite1()->host_lock_handle(h));
    if (!d)
        return PF_Err_OUT_OF_MEMORY;

    std::memcpy(d, &data, sizeof(CK_EffectData));
    d->version = CK_ARB_VERSION;
    suites.HandleSuite1()->host_unlock_handle(h);

    params[CK_STATUS]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
    return PF_Err_NONE;
}

static void SetPathField(char* field, const std::string& value)
{
    std::memset(field, 0, CK_PATH_MAX);
    std::strncpy(field, value.c_str(), CK_PATH_MAX - 1);
}

// "ready", "will be created", or "OFFLINE" for a folder that should hold cache.
static const char* DescribeCacheFolder(const std::string& path)
{
    if (CK_DirExists(path))
        return "ready";

    // Folder missing: is its drive / share root there?
    std::string root;
    if (path.size() >= 3 && path[1] == ':')
        root = path.substr(0, 3);
    else if (path.size() > 2 && path[0] == '\\' && path[1] == '\\')
    {
        size_t a = path.find('\\', 2);
        size_t b = (a == std::string::npos) ? a : path.find('\\', a + 1);
        root = (b == std::string::npos) ? path : path.substr(0, b);
    }

    if (!root.empty() && CK_DirExists(root))
        return "will be created";
    return "OFFLINE (drive or share not connected)";
}

// ---------------------------------------------------------------------------
// Pixel helpers
// ---------------------------------------------------------------------------

static PF_PixelFormat WorldFormat(PF_InData* in_data, PF_OutData* out_data, PF_EffectWorld* w)
{
    PF_PixelFormat fmt = PF_PixelFormat_INVALID;
    try
    {
        AEFX_SuiteScoper<PF_WorldSuite2> world_suite(in_data, kPFWorldSuite,
                                                     kPFWorldSuiteVersion2, out_data);
        world_suite->PF_GetPixelFormat(w, &fmt);
    }
    catch (...)
    {
    }
    if (fmt == PF_PixelFormat_INVALID)
        fmt = PF_WORLD_IS_DEEP(w) ? PF_PixelFormat_ARGB64 : PF_PixelFormat_ARGB32;
    return fmt;
}

// AE world -> interleaved RGB floats (row 0 = top).
// decode_srgb: sRGB-encoded values to linear. unpremult: divide by alpha.
static void ReadWorldRGB(const PF_EffectWorld* w, PF_PixelFormat fmt, bool decode_srgb,
                         bool unpremult, std::vector<float>& out)
{
    out.resize((size_t)w->width * w->height * 3);
    for (A_long y = 0; y < w->height; ++y)
    {
        const char* row = reinterpret_cast<const char*>(w->data) + (size_t)y * w->rowbytes;
        for (A_long x = 0; x < w->width; ++x)
        {
            float a, r, g, b;
            switch (fmt)
            {
                case PF_PixelFormat_ARGB128:
                {
                    const PF_PixelFloat* p = reinterpret_cast<const PF_PixelFloat*>(row) + x;
                    a = p->alpha; r = p->red; g = p->green; b = p->blue;
                    break;
                }
                case PF_PixelFormat_ARGB64:
                {
                    const PF_Pixel16* p = reinterpret_cast<const PF_Pixel16*>(row) + x;
                    const float k = 1.0f / PF_MAX_CHAN16;
                    a = p->alpha * k; r = p->red * k; g = p->green * k; b = p->blue * k;
                    break;
                }
                default:
                {
                    const PF_Pixel8* p = reinterpret_cast<const PF_Pixel8*>(row) + x;
                    const float k = 1.0f / PF_MAX_CHAN8;
                    a = p->alpha * k; r = p->red * k; g = p->green * k; b = p->blue * k;
                    break;
                }
            }
            if (unpremult && a > 1e-4f && a < 0.9999f) { r /= a; g /= a; b /= a; }
            float* o = &out[((size_t)y * w->width + x) * 3];
            o[0] = decode_srgb ? CK_SRGBToLinear(r) : r;
            o[1] = decode_srgb ? CK_SRGBToLinear(g) : g;
            o[2] = decode_srgb ? CK_SRGBToLinear(b) : b;
        }
    }
}

static unsigned long long HashBytes(const void* data, size_t n,
                                    unsigned long long h = 1469598103934665603ULL)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

// Exact identity of a world's pixels (used to name preview results).
static unsigned long long HashWorld(const PF_EffectWorld* w, PF_PixelFormat fmt,
                                   unsigned long long h)
{
    const size_t px = fmt == PF_PixelFormat_ARGB128 ? 16 : fmt == PF_PixelFormat_ARGB64 ? 8 : 4;
    const long dims[3] = { (long)w->width, (long)w->height, (long)fmt };
    h = HashBytes(dims, sizeof(dims), h);
    for (A_long y = 0; y < w->height; ++y)
        h = HashBytes(reinterpret_cast<const char*>(w->data) + (size_t)y * w->rowbytes,
                      px * (size_t)w->width, h);
    return h;
}

// ---------------------------------------------------------------------------
// Layer source lookup
// ---------------------------------------------------------------------------

static bool GetLayerSource(PF_InData* in_data, A_long key_source,
                           CK_JobRequest& req, std::string& error)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Err err = PF_Err_NONE;

    AEGP_LayerH layerH = NULL;
    AEGP_ItemH itemH = NULL;
    AEGP_ItemType type = AEGP_ItemType_NONE;

    ERR(suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &layerH));
    if (!err && layerH)
        ERR(suites.LayerSuite9()->AEGP_GetLayerSourceItem(layerH, &itemH));
    if (!err && itemH)
        ERR(suites.ItemSuite9()->AEGP_GetItemType(itemH, &type));

    if (err || !itemH)
    {
        error = "Couldn't find this layer's source.";
        return false;
    }

    // Footage file details (also gives the frame rate for rendered footage).
    A_long num_files = 0, files_per_frame = 0;
    if (type == AEGP_ItemType_FOOTAGE)
    {
        AEGP_FootageH footageH = NULL;
        AEGP_MemHandle pathH = NULL;
        ERR(suites.FootageSuite5()->AEGP_GetMainFootageFromItem(itemH, &footageH));
        if (!err && footageH)
            ERR(suites.FootageSuite5()->AEGP_GetFootageNumFiles(footageH, &num_files, &files_per_frame));
        if (!err && footageH)
            ERR(suites.FootageSuite5()->AEGP_GetFootagePath(footageH, 0,
                                                            AEGP_FOOTAGE_MAIN_FILE_INDEX, &pathH));
        if (!err && pathH)
        {
            A_UTF16Char* p = NULL;
            ERR(suites.MemorySuite1()->AEGP_LockMemHandle(pathH, reinterpret_cast<void**>(&p)));
            if (!err && p)
                req.source_path = CK_FromUtf16(reinterpret_cast<const unsigned short*>(p));
            suites.MemorySuite1()->AEGP_UnlockMemHandle(pathH);
            suites.MemorySuite1()->AEGP_FreeMemHandle(pathH);
        }
        err = PF_Err_NONE;

        AEGP_FootageInterp interp;
        AEFX_CLR_STRUCT(interp);
        if (!suites.FootageSuite5()->AEGP_GetFootageInterpretation(itemH, FALSE, &interp))
            req.fps = interp.conform_fpsF > 0 ? interp.conform_fpsF : interp.native_fpsF;
    }
    else if (type == AEGP_ItemType_COMP)
    {
        AEGP_CompH compH = NULL;
        A_FpLong fps = 0;
        if (!suites.CompSuite11()->AEGP_GetCompFromItem(itemH, &compH) && compH &&
            !suites.CompSuite11()->AEGP_GetCompFramerate(compH, &fps))
            req.fps = fps;
    }
    if (req.fps <= 0)   // solids etc.: the comp's own rate
        req.fps = in_data->time_step ? (double)in_data->time_scale / in_data->time_step : 24.0;

    const bool have_file = type == AEGP_ItemType_FOOTAGE && !req.source_path.empty() &&
                           CK_FileExists(req.source_path);
    req.rendered = key_source == CK_KS_RENDERED ||
                   (key_source == CK_KS_AUTO && type != AEGP_ItemType_FOOTAGE);

    if (!req.rendered && !have_file)
    {
        if (type != AEGP_ItemType_FOOTAGE)
            error = "Key Source is set to Source File, but this layer isn't footage.\n\n"
                    "Set Key Source to Auto or Rendered Layer to key a precomp.";
        else if (req.source_path.empty())
            error = "This layer has no source file. Set Key Source to Rendered Layer.";
        else
            error = "The source file is missing:\n  " + req.source_path;
        return false;
    }

    A_long w = 0, h = 0;
    if (!suites.ItemSuite9()->AEGP_GetItemDimensions(itemH, &w, &h))
    {
        req.width = w;
        req.height = h;
    }

    A_Time dur;
    AEFX_CLR_STRUCT(dur);
    if (!suites.ItemSuite9()->AEGP_GetItemDuration(itemH, &dur) && dur.scale)
        req.frames_total = (int)((double)dur.value / (double)dur.scale * req.fps + 0.5);

    if (req.rendered)
    {
        // No file: name the key after the project item.
        A_long id = 0;
        suites.ItemSuite9()->AEGP_GetItemID(itemH, &id);
        char name[64];
        std::snprintf(name, sizeof(name), "rendered_item_%d", (int)id);
        req.source_path = name;
        req.is_sequence = false;
    }
    else
    {
        req.is_sequence = num_files > 1;
        if (req.is_sequence && num_files > 0)
            req.frames_total = num_files;
    }
    return true;
}

// Renders this layer's input (what the effect receives) for every frame in
// req's range into staging_dir as EXRs, and fingerprints the pixels so an
// unchanged render reuses its key. Precomp contents are always included;
// effects above this one on the same layer may not be (an AE limitation).
static const char* kCK_Cancelled = "(cancelled)";

// AE's own progress bar with a Cancel button (PFAppSuite6). AE only shows it
// once the work has run for a moment. Missing suite = no bar, no harm.
class CK_ProgressDialog
{
public:
    CK_ProgressDialog(PF_InData* in_data, const char* title)
        : m_basic(in_data->pica_basicP)
    {
        if (m_basic && m_basic->AcquireSuite(kPFAppSuite, kPFAppSuiteVersion6,
                                             (const void**)&m_suite) != 0)
            m_suite = NULL;
        if (m_suite)
        {
            A_UTF16Char t[128], c[16];
            ToUTF16Z(title, t, 128);
            ToUTF16Z("Cancel", c, 16);
            if (m_suite->PF_CreateNewAppProgressDialog(t, c, FALSE, &m_dlg) != PF_Err_NONE)
                m_dlg = NULL;
        }
    }
    ~CK_ProgressDialog()
    {
        if (m_suite && m_dlg)
            m_suite->PF_DisposeAppProgressDialog(m_dlg);
        if (m_suite)
            m_basic->ReleaseSuite(kPFAppSuite, kPFAppSuiteVersion6);
    }
    // False when the user pressed Cancel.
    bool Update(A_long done, A_long total)
    {
        return !(m_suite && m_dlg) ||
               m_suite->PF_AppProgressDialogUpdate(m_dlg, done, total) == PF_Err_NONE;
    }

private:
    static void ToUTF16Z(const char* s, A_UTF16Char* dst, int cap)
    {
        int i = 0;
        for (; s[i] && i < cap - 1; ++i)
            dst[i] = (A_UTF16Char)(unsigned char)s[i];
        dst[i] = 0;
    }
    SPBasicSuite* m_basic = NULL;
    PFAppSuite6* m_suite = NULL;
    PF_AppProgressDialogP m_dlg = NULL;
};

static bool RenderLayerFrames(PF_InData* in_data, PF_OutData* out_data, CK_JobRequest& req,
                              const std::string& staging_dir, std::string& error)
{
    if (!CK_MakeDirs(staging_dir))
    {
        error = "Couldn't create a folder in the cache:\n  " + staging_dir;
        return false;
    }

    CK_ProgressDialog progress(in_data, "CorridorKey: rendering the layer's frames for the key");

    unsigned long long hash = 1469598103934665603ULL;
    std::vector<float> rgb;
    for (int i = 0; i < req.frames_total; ++i)
    {
        if (!progress.Update(i, req.frames_total))
        {
            error = kCK_Cancelled;
            return false;
        }
        const A_long t = (A_long)((double)(req.start_frame + i) * in_data->time_scale / req.fps + 0.5);
        PF_ParamDef p;
        AEFX_CLR_STRUCT(p);
        if (PF_CHECKOUT_PARAM(in_data, CK_INPUT, t, in_data->time_step, in_data->time_scale, &p))
        {
            error = "Couldn't render the layer.";
            return false;
        }

        PF_EffectWorld* w = &p.u.ld;
        bool ok = w->data && w->width > 0 && w->height > 0;
        if (ok && (w->width != req.width || w->height != req.height))
        {
            PF_CHECKIN_PARAM(in_data, &p);
            char dims[160];
            std::snprintf(dims, sizeof(dims),
                          "The layer rendered at %dx%d instead of %dx%d.\n\n"
                          "Set the Composition panel's resolution to Full, then click Key Clip again.",
                          (int)w->width, (int)w->height, req.width, req.height);
            error = dims;
            return false;
        }
        if (ok)
        {
            ReadWorldRGB(w, WorldFormat(in_data, out_data, w), false, false, rgb);
            hash = HashBytes(rgb.data(), rgb.size() * sizeof(float), hash);
            char name[32];
            std::snprintf(name, sizeof(name), "\\f_%06d.exr", i);
            ok = CK_WriteExrRGB(staging_dir + name, w->width, w->height, rgb.data(), error);
        }
        PF_CHECKIN_PARAM(in_data, &p);
        if (!ok)
        {
            if (error.empty())
                error = "The layer returned no pixels.";
            return false;
        }
    }
    req.render_hash = hash;
    req.prerendered_dir = staging_dir;
    return true;
}

// ---------------------------------------------------------------------------
// Key Range: limit the key to the layer's in/out (+ handles)
// ---------------------------------------------------------------------------

// Narrows req to the layer's trimmed range. req.frames_total must hold the
// whole clip's length. Leaves req untouched for Whole Clip, time-stretched
// layers, or when the range already covers the clip.
static void ApplyKeyRange(PF_InData* in_data, PF_ParamDef* params[], CK_JobRequest& req)
{
    if (params[CK_KEY_RANGE]->u.pd.value == CK_RANGE_WHOLE || req.frames_total <= 0)
        return;

    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Err err = PF_Err_NONE;
    AEGP_LayerH layerH = NULL;
    A_Ratio stretch = { 1, 1 };
    A_Time in_t, dur_t;
    AEFX_CLR_STRUCT(in_t);
    AEFX_CLR_STRUCT(dur_t);

    ERR(suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &layerH));
    ERR(suites.LayerSuite9()->AEGP_GetLayerStretch(layerH, &stretch));
    ERR(suites.LayerSuite9()->AEGP_GetLayerInPoint(layerH, AEGP_LTimeMode_LayerTime, &in_t));
    ERR(suites.LayerSuite9()->AEGP_GetLayerDuration(layerH, AEGP_LTimeMode_LayerTime, &dur_t));

    if (err || !in_t.scale || !dur_t.scale)
        return;
    if (stretch.num != stretch.den)
        return;   // stretched layers: layer time isn't source time, key everything

    const double in_s = (double)in_t.value / in_t.scale;
    const double out_s = in_s + (double)dur_t.value / dur_t.scale;
    int first = (int)std::floor(in_s * req.fps + 1e-3);
    int end = (int)std::ceil(out_s * req.fps - 1e-3);

    const int handles = std::max(0, (int)params[CK_HANDLES]->u.sd.value);
    first = std::max(0, first - handles);
    end = std::min(req.frames_total, end + handles);

    if (end - first <= 0 || (first == 0 && end >= req.frames_total))
        return;

    req.trimmed = true;
    req.start_frame = first;
    req.frames_total = end - first;
}

// ---------------------------------------------------------------------------
// Hint Mask -> first-frame hint PNG
// ---------------------------------------------------------------------------

// Reads the chosen mask at the layer's first frame and rasterizes it at
// source resolution. Returns false (with a message) if the mask can't be used.
static bool BuildHint(PF_InData* in_data, PF_ParamDef* params[],
                      CK_JobRequest& req, std::string& error)
{
    const PF_PathID path_id = params[CK_HINT_MASK]->u.path_d.path_id;
    if (!path_id)
        return true;   // no hint chosen

    if (req.width <= 0 || req.height <= 0)
    {
        error = "Couldn't read the source size for the Hint Mask.";
        return false;
    }

    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    PF_PathOutlinePtr pathP = NULL;
    std::vector<CK_MaskVertex> verts;
    PF_Boolean openB = FALSE;

    // CorridorKey hints the first frame it keys: source frame start_frame,
    // which is layer time start_frame / fps.
    const A_long hint_t = (A_long)((double)req.start_frame * in_data->time_scale / req.fps + 0.5);
    ERR(suites.PathQuerySuite1()->PF_CheckoutPath(in_data->effect_ref, path_id, hint_t,
                                                  in_data->time_step, in_data->time_scale,
                                                  &pathP));
    if (!err && pathP)
    {
        A_long num = 0;
        ERR(suites.PathDataSuite1()->PF_PathNumSegments(in_data->effect_ref, pathP, &num));
        ERR(suites.PathDataSuite1()->PF_PathIsOpen(in_data->effect_ref, pathP, &openB));

        for (A_long i = 0; i < num && !err; ++i)
        {
            PF_PathVertex v;
            AEFX_CLR_STRUCT(v);
            ERR(suites.PathDataSuite1()->PF_PathVertexInfo(in_data->effect_ref, pathP, i, &v));
            if (!err)
                verts.push_back({v.x, v.y, v.tan_in_x, v.tan_in_y, v.tan_out_x, v.tan_out_y});
        }
    }
    if (pathP)
        ERR2(suites.PathQuerySuite1()->PF_CheckinPath(in_data->effect_ref, path_id, FALSE, pathP));

    if (err || verts.size() < 3)
    {
        error = "The Hint Mask needs at least 3 points.";
        return false;
    }
    if (openB)
    {
        error = "The Hint Mask is an open path. Close it so it surrounds the subject.";
        return false;
    }

    std::vector<unsigned char> coverage;
    CK_RasterizeMask(verts, req.width, req.height, coverage);

    size_t filled = 0;
    for (unsigned char c : coverage)
        filled += c > 127;
    if (filled == 0)
    {
        error = "The Hint Mask doesn't cover any of the first keyed frame.\n\n"
                "Check it surrounds the subject at the layer's in-point\n"
                "(minus Handles), or at the start of the clip for Whole Clip.";
        return false;
    }

    req.hint_png = CK_EncodeGrayPng(coverage, req.width, req.height);
    if (req.hint_png.empty())
    {
        error = "Couldn't encode the hint image.";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Hint Layer -> first-frame hint or per-frame hint folder
// ---------------------------------------------------------------------------

// Renders the Matte Layer's alpha (or luminance, if it has no alpha) at the
// source's frame times. AE hands the layer over in its own pixel space with
// its effects applied (masks on that layer are not included).
static bool BuildHintFromLayer(PF_InData* in_data, PF_OutData* out_data,
                               CK_JobRequest& req, bool every_frame,
                               std::string& error)
{
    if (req.width <= 0 || req.height <= 0 || req.fps <= 0)
    {
        error = "Couldn't read the source size or frame rate for the Hint Layer.";
        return false;
    }

    AEFX_SuiteScoper<PF_WorldSuite2> world_suite(in_data, kPFWorldSuite,
                                                 kPFWorldSuiteVersion2, out_data);

    // One extra frame so rounding in the clip length never leaves us short.
    const int count = every_frame ? std::max(1, req.frames_total + 1) : 1;
    bool use_luma = false;

    for (int i = 0; i < count; ++i)
    {
        const A_long t = (A_long)((double)(req.start_frame + i) * in_data->time_scale / req.fps + 0.5);

        PF_ParamDef p;
        AEFX_CLR_STRUCT(p);
        PF_Err err = PF_CHECKOUT_PARAM(in_data, CK_HINT_LAYER, t,
                                       in_data->time_step, in_data->time_scale, &p);
        if (err)
        {
            error = "Couldn't read the Matte Layer.";
            return false;
        }

        PF_EffectWorld* w = &p.u.ld;
        std::vector<unsigned char> matte;
        bool opaque = false, empty = true;

        if (w->data && w->width > 0 && w->height > 0)
        {
            PF_PixelFormat fmt = PF_PixelFormat_ARGB32;
            world_suite->PF_GetPixelFormat(w, &fmt);
            const CK_PixelKind kind = fmt == PF_PixelFormat_ARGB128 ? CK_PIX_32F
                                    : fmt == PF_PixelFormat_ARGB64  ? CK_PIX_16
                                                                    : CK_PIX_8;

            float colour = 0.0f;
            CK_WorldToHint(w->data, w->rowbytes, w->width, w->height, kind, use_luma,
                           req.width, req.height, matte, &opaque, &empty, &colour);

            // A fully opaque layer has no alpha matte. If it's black & white, read
            // its brightness as the matte. If it's colourful, it's ordinary footage:
            // almost always a Roto Brush (or masked) layer picked directly, which AE
            // hands to effects WITHOUT its effects and masks.
            if (i == 0 && opaque)
            {
                if (colour > 0.03f)
                {
                    PF_CHECKIN_PARAM(in_data, &p);
                    error = "The Matte Layer has no transparency and looks like ordinary footage.\n\n"
                            "After Effects gives effects another layer WITHOUT its masks or effects,\n"
                            "so a Roto Brush or masked layer can't be used directly.\n\n"
                            "Fix: select that layer, Layer > Pre-compose (Move all attributes),\n"
                            "then pick the new precomp as the Matte Layer.";
                    return false;
                }
                use_luma = true;
                CK_WorldToHint(w->data, w->rowbytes, w->width, w->height, kind, use_luma,
                               req.width, req.height, matte, &opaque, &empty);
            }
        }
        else
        {
            matte.assign((size_t)req.width * req.height, 0);
        }

        PF_CHECKIN_PARAM(in_data, &p);

        if (i == 0 && empty)
        {
            error = "The Matte Layer has no matte at the first keyed frame.\n\n"
                    "Pick a layer under Hint > Matte Layer (its alpha, or white = subject),\n"
                    "or change Hint > Hint Source.";
            return false;
        }

        std::string png = CK_EncodeGrayPng(matte, req.width, req.height);
        if (png.empty())
        {
            error = "Couldn't encode the hint image.";
            return false;
        }

        if (every_frame)
            req.hint_frames.push_back(std::move(png));
        else
            req.hint_png = std::move(png);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

static PF_Err DoKeyClip(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    PF_Err err = PF_Err_NONE;
    const char* title = "CorridorKey_MinionsTake";

    CK_EffectData data;
    CK_ReadEffectData(in_data, params, &data);

    // 1. Is CorridorKey installed?
    CK_GlobalSettings gs = CK_LoadGlobalSettings();
    std::string python = CK_ResolvePythonExe(gs);
    if (gs.corridorkey_dir.empty() ||
        !CK_FileExists(gs.corridorkey_dir + "\\infer.py") ||
        !CK_FileExists(python))
    {
        CK_ShowMessage(title,
            "CorridorKey isn't set up yet.\n\n"
            "Run tools\\CorridorKey_Setup.bat from the CorridorKeyAE folder,\n"
            "or set corridorkey_dir in Global Settings.\n\n"
            "Settings file:\n  " + gs.settings_file);
        return err;
    }

    // 2. A job already linked to this effect?
    if (data.job_dir[0])
    {
        CK_JobStatus st = CK_QueryJob(data.job_dir);
        if (st.state == CK_JOB_RUNNING)
        {
            if (CK_AskYesNo(title, "CorridorKey is keying this clip.\n\nCancel it?"))
            {
                CK_CancelJob(data.job_dir);
                data.generation++;
                err = CK_WriteEffectData(in_data, params, data);
            }
            return err;
        }
        if (st.state == CK_JOB_FAILED)
        {
            CK_OpenInEditor(CK_JobLogPath(data.job_dir));
            if (!CK_AskYesNo(title, "The last key failed. Its log is open in Notepad.\n\nTry again?"))
                return err;
        }
    }

    // 3. What are we keying, and with which settings?
    CK_JobRequest req;
    std::string error;
    if (!GetLayerSource(in_data, params[CK_KEY_SOURCE]->u.pd.value, req, error))
    {
        CK_ShowMessage(title, error);
        return err;
    }
    req.hint_quality = (float)(params[CK_HINT_QUALITY]->u.fs_d.value / 100.0);
    req.carry_quality = (float)(params[CK_CARRY_QUALITY]->u.fs_d.value / 100.0);
    req.exact_hint = params[CK_EXACT_HINT]->u.bd.value != 0;
    req.low_vram = params[CK_LOW_VRAM]->u.bd.value != 0;
    switch (params[CK_TEMPORAL]->u.pd.value)
    {
        case CK_TW_80: req.temporal_chunk = 80; break;
        case CK_TW_48: req.temporal_chunk = 48; break;
        case CK_TW_24: req.temporal_chunk = 24; break;
        case CK_TW_8:  req.temporal_chunk = 8;  break;
        case CK_TW_1:  req.temporal_chunk = 1;  break;
        default:       req.temporal_chunk = 0;  break;   // Auto
    }
    ApplyKeyRange(in_data, params, req);

    // Hint Source decides where the hint comes from (if anywhere).
    bool hint_ok = true;
    switch (params[CK_HINT_SOURCE]->u.pd.value)
    {
        case CK_HS_MASK:
            hint_ok = BuildHint(in_data, params, req, error);   // no mask picked = no hint
            break;
        case CK_HS_LAYER_FIRST:
            hint_ok = BuildHintFromLayer(in_data, out_data, req, false, error);
            break;
        case CK_HS_LAYER_EVERY:
            hint_ok = BuildHintFromLayer(in_data, out_data, req, true, error);
            break;
        default:
            break;   // Off
    }
    if (!hint_ok)
    {
        CK_ShowMessage(title, error);
        return err;
    }

    // 4. Where does it go?
    CK_SetProjectDir(QueryProjectDir(in_data->pica_basicP));
    std::string cache_root;
    switch (params[CK_CACHE_LOCATION]->u.pd.value)
    {
        case CK_CL_CUSTOM:
            if (!data.cache_override[0])
            {
                CK_ShowMessage(title, "Save Keys In is set to Custom Folder, but no folder is chosen.\n\n"
                                      "Click Custom Folder > Choose... first.");
                return err;
            }
            cache_root = data.cache_override;
            break;
        case CK_CL_GLOBAL:
            cache_root = CK_ResolveCacheRoot("", gs);
            break;
        default:
            cache_root = CK_GetProjectDir();
            if (cache_root.empty())
                cache_root = CK_ResolveCacheRoot("", gs);   // unsaved project
            else
                cache_root += "\\" CK_PROJECT_KEYS_DIR;
            break;
    }
    std::string state = DescribeCacheFolder(cache_root);
    if (state.compare(0, 7, "OFFLINE") == 0)
    {
        CK_ShowMessage(title, "The cache folder is offline:\n  " + cache_root +
                              "\n\nReconnect the drive, or choose another folder under Cache.");
        return err;
    }

    // Rendered Key Source: render the frames now (AE is busy until done).
    std::string staging;
    if (req.rendered)
    {
        char name[48];
        std::snprintf(name, sizeof(name), "\\_staging_%lu", CK_TickMs());
        staging = cache_root + name;
        req.rendered_linear = params[CK_WORKING_SPACE]->u.pd.value == CK_SPACE_LINEAR;
        if (!RenderLayerFrames(in_data, out_data, req, staging, error))
        {
            CK_RemoveTree(staging);
            if (error != kCK_Cancelled)
                CK_ShowMessage(title, error);
            return err;
        }
    }

    std::string job_dir = CK_JobDirFor(cache_root, req);
    CK_JobStatus existing = CK_QueryJob(job_dir);
    bool starting = false;

    if (existing.state == CK_JOB_RUNNING)
    {
        CK_ShowMessage(title, "This clip is already being keyed with these settings\n"
                              "(probably by another copy of the effect).\n\n"
                              "This effect will show the same progress.");
    }
    else if (existing.state == CK_JOB_DONE &&
             !CK_AskYesNo(title, "This clip is already keyed with these settings.\n\n"
                                 "Key it again from scratch?"))
    {
        // Just relink to the existing result.
    }
    else
    {
        // Free the GPU for the full key.
        CK_PreviewStop();
        if (!CK_StartJob(job_dir, req, python, gs.corridorkey_dir,
                         CK_ResolveFfmpegExe(gs), error))
        {
            CK_RemoveTree(staging);
            CK_ShowMessage(title, error);
            return err;
        }
        starting = true;
    }
    if (!starting && !staging.empty())
        CK_RemoveTree(staging);   // reused an existing key: rendered frames not needed

    SetPathField(data.job_dir, job_dir);
    data.frames_total = req.frames_total;
    data.fps_x1000 = (A_long)(req.fps * 1000.0 + 0.5);
    data.src_start = (req.trimmed || req.rendered) ? req.start_frame : 0;
    data.generation++;
    err = CK_WriteEffectData(in_data, params, data);
    out_data->out_flags |= PF_OutFlag_REFRESH_UI;
    return err;
}

static PF_Err DoPreviewFrame(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    PF_Err err = PF_Err_NONE;
    const char* title = "CorridorKey_MinionsTake";

    CK_GlobalSettings gs = CK_LoadGlobalSettings();
    if (gs.corridorkey_dir.empty() ||
        !CK_FileExists(gs.corridorkey_dir + "\\infer.py") ||
        !CK_FileExists(CK_ResolvePythonExe(gs)))
    {
        CK_ShowMessage(title, "CorridorKey isn't set up yet.\n\n"
                              "Run tools\\CorridorKey_Setup.bat from the CorridorKeyAE folder.");
        return err;
    }

    CK_EffectData data;
    CK_ReadEffectData(in_data, params, &data);
    if (data.job_dir[0] && CK_QueryJob(data.job_dir).state == CK_JOB_RUNNING)
    {
        CK_ShowMessage(title, "Key Clip is running for this layer, so previews are paused\n"
                              "(both would need the GPU).");
        return err;
    }

    const CK_PreviewState st = CK_PreviewQuery();
    if (st == CK_PV_FAILED)
    {
        CK_OpenInEditor(CK_PreviewLogPath());
        if (!CK_AskYesNo(title, "The preview helper stopped with an error. Its log is open in Notepad.\n\n"
                                "Start it again?"))
            return err;
    }
    if (st != CK_PV_READY && st != CK_PV_LOADING)
    {
        std::string error;
        if (!CK_PreviewStart(gs, error))
        {
            CK_ShowMessage(title, error);
            return err;
        }
    }

    if (!data.preview_owner)
        data.preview_owner = (A_long)((CK_TickMs() * 2654435761u) | 1u);

    // Comp frame number, for the Status line.
    A_long comp_frame = 0;
    {
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        AEGP_LayerH layerH = NULL;
        A_Time lt = { in_data->current_time, (A_u_long)in_data->time_scale };
        A_Time ct = lt;
        if (!suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &layerH) && layerH)
            suites.LayerSuite9()->AEGP_ConvertLayerToCompTime(layerH, &lt, &ct);
        if (ct.scale && in_data->time_step)
            comp_frame = (A_long)std::floor((double)ct.value / ct.scale *
                                            in_data->time_scale / in_data->time_step + 1e-3);
    }

    // Already previewing this frame: the Status line shows progress, so a
    // second click does nothing.
    CK_PreviewReqInfo info;
    if (data.preview_token > 0 && comp_frame == data.preview_comp_frame &&
        CK_PreviewInfo((unsigned long)data.preview_owner, info) &&
        info.token == data.preview_token &&
        (info.state == CK_PR_QUEUED || info.state == CK_PR_RUNNING))
        return err;

    data.preview_time = in_data->current_time;
    data.preview_scale = in_data->time_scale;
    data.preview_comp_frame = comp_frame;
    data.preview_token++;
    CK_PreviewNote((unsigned long)data.preview_owner, data.preview_token, CK_PR_QUEUED);
    err = CK_WriteEffectData(in_data, params, data);
    out_data->out_flags |= PF_OutFlag_REFRESH_UI;
    return err;
}

static PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data,
                               PF_ParamDef* params[],
                               PF_UserChangedParamExtra* extra)
{
    PF_Err err = PF_Err_NONE;

    switch (extra->param_index)
    {
        case CK_KEY_CLIP:
            err = DoKeyClip(in_data, out_data, params);
            break;

        case CK_PREVIEW_FRAME:
            err = DoPreviewFrame(in_data, out_data, params);
            break;

        case CK_CHOOSE_CACHE:
        {
            CK_EffectData data;
            CK_ReadEffectData(in_data, params, &data);
            CK_GlobalSettings gs = CK_LoadGlobalSettings();
            std::string current = CK_ResolveCacheRoot(data.cache_override, gs);
            std::string chosen;
            if (CK_ChooseFolder(current, chosen))
            {
                SetPathField(data.cache_override, chosen);
                err = CK_WriteEffectData(in_data, params, data);
                if (!err)
                {
                    // Picking a folder means "use it": switch Save Keys In to Custom.
                    params[CK_CACHE_LOCATION]->u.pd.value = CK_CL_CUSTOM;
                    params[CK_CACHE_LOCATION]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
                }
            }
            break;
        }

        default:
            break;
    }

    return err;
}

// ---------------------------------------------------------------------------
// SmartFX render: cached key with live controls, else pass-through
// ---------------------------------------------------------------------------

struct CK_PreRenderData
{
    A_long left = 0;
    A_long top = 0;
};

static void DeletePreRenderData(void* p)
{
    delete reinterpret_cast<CK_PreRenderData*>(p);
}

static bool ReadEffectDataFromParam(PF_InData* in_data, PF_ParamDef& p, CK_EffectData* out)
{
    PF_ParamDef* params[CK_NUM_PARAMS] = { 0 };
    params[CK_STATUS] = &p;
    return CK_ReadEffectData(in_data, params, out);
}

// Source frame index (in cache numbering) for the current render time.
static int CacheIndexFor(PF_InData* in_data, const CK_EffectData& data)
{
    const A_long ts = in_data->time_step, sc = in_data->time_scale;
    double fps = data.fps_x1000 > 0 ? data.fps_x1000 / 1000.0
               : (ts > 0 ? (double)sc / (double)ts : 24.0);
    return (int)std::floor((double)in_data->current_time / (double)sc * fps + 1e-3)
           - data.src_start;
}

static bool IsPreviewTime(PF_InData* in_data, const CK_EffectData& data)
{
    if (data.preview_token <= 0 || data.preview_scale <= 0 || in_data->time_scale <= 0)
        return false;
    const double now = (double)in_data->current_time / in_data->time_scale;
    const double then = (double)data.preview_time / data.preview_scale;
    const double half_frame = 0.5 * (double)in_data->time_step / in_data->time_scale;
    return std::fabs(now - then) < std::max(half_frame, 1e-4);
}

static PF_Err PreRender(PF_InData* in_data, PF_OutData* out_data,
                        PF_PreRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    PF_RenderRequest req = extra->input->output_request;
    PF_CheckoutResult in_result;

    ERR(extra->cb->checkout_layer(in_data->effect_ref,
                                  CK_INPUT,
                                  CK_INPUT,
                                  &req,
                                  in_data->current_time,
                                  in_data->time_step,
                                  in_data->time_scale,
                                  &in_result));
    if (!err)
    {
        extra->output->result_rect = in_result.result_rect;
        extra->output->max_result_rect = in_result.max_result_rect;

        CK_PreRenderData* prd = new CK_PreRenderData;
        prd->left = in_result.result_rect.left;
        prd->top = in_result.result_rect.top;
        extra->output->pre_render_data = prd;
        extra->output->delete_pre_render_data_func = DeletePreRenderData;
    }

    // Tell AE which version of this frame it's getting, so frames that were
    // drawn as the plain plate while keying are redrawn once they're cached.
    PF_ParamDef p_status;
    AEFX_CLR_STRUCT(p_status);
    if (!err && !PF_CHECKOUT_PARAM(in_data, CK_STATUS, in_data->current_time,
                                   in_data->time_step, in_data->time_scale, &p_status))
    {
        CK_EffectData data;
        if (ReadEffectDataFromParam(in_data, p_status, &data))
        {
            struct
            {
                char job[CK_PATH_MAX];
                A_long generation;
                A_long available;
                A_long preview_token;
                A_long preview_state;     // only at the clicked frame
            } mix;
            std::memset(&mix, 0, sizeof(mix));
            std::memcpy(mix.job, data.job_dir, CK_PATH_MAX);
            mix.generation = data.generation;
            // Only the clicked frame changes version, so a click doesn't
            // throw away AE's cache for the rest of the layer.
            if (IsPreviewTime(in_data, data))
            {
                mix.preview_token = data.preview_token;
                mix.preview_state = (A_long)CK_PreviewQuery() + 1;
            }

            if (data.job_dir[0])
            {
                const int index = CacheIndexFor(in_data, data);
                CK_JobStatus st = CK_QueryJob(data.job_dir);
                // While running, the newest file may still be mid-write.
                mix.available = (st.state == CK_JOB_DONE) ||
                                (st.state == CK_JOB_RUNNING && index >= 0 &&
                                 index < st.frames_done - 1);
            }
            ERR(extra->cb->GuidMixInPtr(in_data->effect_ref, sizeof(mix), &mix));
        }
        PF_CHECKIN_PARAM(in_data, &p_status);
    }
    return err;
}

// Straight-alpha float RGBA buffer -> AE world in its own bit depth.
static void WriteToWorld(const std::vector<float>& buf, PF_EffectWorld* w,
                         PF_PixelFormat fmt)
{
    for (A_long y = 0; y < w->height; ++y)
    {
        const float* src = &buf[(size_t)y * w->width * 4];
        char* row = reinterpret_cast<char*>(w->data) + (size_t)y * w->rowbytes;

        for (A_long x = 0; x < w->width; ++x, src += 4)
        {
            switch (fmt)
            {
                case PF_PixelFormat_ARGB128:
                {
                    PF_PixelFloat* p = reinterpret_cast<PF_PixelFloat*>(row) + x;
                    p->red = src[0];
                    p->green = src[1];
                    p->blue = src[2];
                    p->alpha = src[3];
                    break;
                }
                case PF_PixelFormat_ARGB64:
                {
                    PF_Pixel16* p = reinterpret_cast<PF_Pixel16*>(row) + x;
                    auto q = [](float v) {
                        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                        return (A_u_short)(v * PF_MAX_CHAN16 + 0.5f);
                    };
                    p->red = q(src[0]);
                    p->green = q(src[1]);
                    p->blue = q(src[2]);
                    p->alpha = q(src[3]);
                    break;
                }
                default:
                {
                    PF_Pixel8* p = reinterpret_cast<PF_Pixel8*>(row) + x;
                    auto q = [](float v) {
                        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                        return (A_u_char)(v * PF_MAX_CHAN8 + 0.5f);
                    };
                    p->red = q(src[0]);
                    p->green = q(src[1]);
                    p->blue = q(src[2]);
                    p->alpha = q(src[3]);
                    break;
                }
            }
        }
    }
}

// Preview Frame results live in <cache>\_preview\<hash of the input pixels>.
// Any frame whose exact input was previewed before shows that result; the
// clicked frame asks the preview helper to key it (blocking this render).
static std::shared_ptr<const CK_Frame> GetPreviewFrame(PF_InData* in_data,
                                                       const CK_EffectData& data,
                                                       const PF_EffectWorld* inW,
                                                       PF_PixelFormat fmt, bool srgb)
{
    // Previews pause while this layer's full key is running (GPU).
    if (data.job_dir[0] && CK_QueryJob(data.job_dir).state == CK_JOB_RUNNING)
        return nullptr;

    // Previews are throwaway, so they always go to the global cache.
    const CK_GlobalSettings gs = CK_LoadGlobalSettings();
    const std::string cache_root = CK_ResolveCacheRoot("", gs);
    if (!CK_DirExists(cache_root) && !CK_MakeDirs(cache_root))
        return nullptr;

    const unsigned long long h = HashWorld(inW, fmt, srgb ? 1ULL : 2ULL);
    char hex[24];
    std::snprintf(hex, sizeof(hex), "%016llx", h);
    const std::string dir = cache_root + "\\_preview\\" + hex;

    const bool clicked = IsPreviewTime(in_data, data);
    const unsigned long owner = (unsigned long)data.preview_owner;

    std::shared_ptr<const CK_Frame> frame = CK_GetKeyedFrame(dir, 0, 0);
    if (frame || !clicked || CK_PreviewQuery() != CK_PV_READY)
    {
        if (frame && clicked)
            CK_PreviewNote(owner, data.preview_token, CK_PR_DONE);   // previewed before
        return frame;
    }

    // Another render of this same frame may already be keying it.
    if (!CK_PreviewBeginDir(dir))
        return CK_GetKeyedFrame(dir, 0, 0);

    CK_PreviewNote(owner, data.preview_token, CK_PR_RUNNING);

    // Same values CorridorKey reads from a file: display values for an sRGB
    // project, scene-linear for a linear one.
    const std::string in_dir = dir + "\\in";
    std::vector<float> rgb;
    ReadWorldRGB(inW, fmt, false, false, rgb);
    std::string error;
    const bool ok = CK_MakeDirs(in_dir) &&
        CK_WriteExrRGB(in_dir + "\\f_000000.exr", inW->width, inW->height, rgb.data(), error) &&
        CK_PreviewKey(in_dir, dir, !srgb, error);

    frame = ok ? CK_GetKeyedFrame(dir, 0, 0) : nullptr;
    CK_PreviewNote(owner, data.preview_token, frame ? CK_PR_DONE : CK_PR_FAILED);
    CK_PreviewEndDir(dir);
    return frame;
}

static PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data,
                          PF_SmartRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);

    PF_EffectWorld* inW = NULL;
    PF_EffectWorld* outW = NULL;

    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, CK_INPUT, &inW));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &outW));

    PF_ParamDef p_mode, p_space, p_despill, p_choke, p_gamma, p_status;
    PF_ParamDef p_screen, p_picked, p_method, p_speck, p_keep;
    AEFX_CLR_STRUCT(p_screen);
    AEFX_CLR_STRUCT(p_picked);
    AEFX_CLR_STRUCT(p_method);
    AEFX_CLR_STRUCT(p_speck);
    AEFX_CLR_STRUCT(p_keep);
    AEFX_CLR_STRUCT(p_mode);
    AEFX_CLR_STRUCT(p_space);
    AEFX_CLR_STRUCT(p_despill);
    AEFX_CLR_STRUCT(p_choke);
    AEFX_CLR_STRUCT(p_gamma);
    AEFX_CLR_STRUCT(p_status);

    const A_long t = in_data->current_time, ts = in_data->time_step, sc = in_data->time_scale;
    ERR(PF_CHECKOUT_PARAM(in_data, CK_OUTPUT_MODE, t, ts, sc, &p_mode));
    ERR(PF_CHECKOUT_PARAM(in_data, CK_WORKING_SPACE, t, ts, sc, &p_space));
    ERR(PF_CHECKOUT_PARAM(in_data, CK_DESPILL, t, ts, sc, &p_despill));
    ERR(PF_CHECKOUT_PARAM(in_data, CK_MATTE_CHOKE, t, ts, sc, &p_choke));
    ERR(PF_CHECKOUT_PARAM(in_data, CK_MATTE_GAMMA, t, ts, sc, &p_gamma));
    ERR(PF_CHECKOUT_PARAM(in_data, CK_STATUS, t, ts, sc, &p_status));
    ERR(PF_CHECKOUT_PARAM(in_data, CK_SCREEN_COLOUR, t, ts, sc, &p_screen));
    ERR(PF_CHECKOUT_PARAM(in_data, CK_PICKED_COLOUR, t, ts, sc, &p_picked));
    ERR(PF_CHECKOUT_PARAM(in_data, CK_DESPILL_METHOD, t, ts, sc, &p_method));
    ERR(PF_CHECKOUT_PARAM(in_data, CK_DESPECKLE, t, ts, sc, &p_speck));
    ERR(PF_CHECKOUT_PARAM(in_data, CK_KEEP_ORIGINAL, t, ts, sc, &p_keep));

    bool drew = false;
    CK_EffectData data;
    const bool have_data = !err && ReadEffectDataFromParam(in_data, p_status, &data);

    if (!err && have_data && inW && outW && p_mode.u.pd.value != CK_OUT_ORIGINAL)
    {
        const bool srgb = p_space.u.pd.value != CK_SPACE_LINEAR;
        const PF_PixelFormat fmt = WorldFormat(in_data, out_data, outW);

        // 1. The finished key for this frame, if there is one.
        std::shared_ptr<const CK_Frame> frame;
        if (data.job_dir[0])
            frame = CK_GetKeyedFrame(data.job_dir, CacheIndexFor(in_data, data), data.generation);

        // 2. Otherwise a Preview Frame result (keyed from exactly this input).
        const bool is_preview = !frame && data.preview_token > 0;
        if (is_preview)
            frame = GetPreviewFrame(in_data, data, inW, fmt, srgb);

        if (frame)
        {
            CK_RenderSettings s;
            s.mode = p_mode.u.pd.value;
            s.srgb_out = srgb;
            s.despill = (float)(p_despill.u.fs_d.value / 100.0);
            s.choke_px = (float)p_choke.u.fs_d.value;
            s.gamma = (float)p_gamma.u.fs_d.value;
            s.despeckle_px = (int)p_speck.u.sd.value;
            s.keep_original = p_keep.u.bd.value != 0;

            switch (p_screen.u.pd.value)
            {
                case CK_SC_BLUE:   s.screen = CK_SCREEN_BLUE;   break;
                case CK_SC_PICKED: s.screen = CK_SCREEN_PICKED; break;
                default:           s.screen = CK_SCREEN_GREEN;  break;
            }
            switch (p_method.u.pd.value)
            {
                case CK_DM_GENTLE: s.method = CK_DESPILL_GENTLE; break;
                case CK_DM_STRONG: s.method = CK_DESPILL_STRONG; break;
                default:           s.method = CK_DESPILL_AVERAGE; break;
            }
            if (p_method.u.pd.value == CK_DM_OFF)
                s.despill = 0.0f;
            // The colour control is in the project's space; despill works in linear.
            const PF_Pixel& pc = p_picked.u.cd.value;
            const float pick[3] = { pc.red / 255.0f, pc.green / 255.0f, pc.blue / 255.0f };
            for (int c = 0; c < 3; ++c)
                s.picked[c] = s.srgb_out ? CK_SRGBToLinear(pick[c]) : pick[c];

            const float sx = in_data->downsample_x.num
                                 ? (float)in_data->downsample_x.den / (float)in_data->downsample_x.num
                                 : 1.0f;
            const float sy = in_data->downsample_y.num
                                 ? (float)in_data->downsample_y.den / (float)in_data->downsample_y.num
                                 : 1.0f;
            if (is_preview)
            {
                // The preview was keyed from this very input buffer: 1:1 pixels.
                // Choke/Despeckle are set in full-res pixels, so scale them down.
                s.choke_px /= sx;
                s.despeckle_px = (int)(s.despeckle_px / (sx * sy) + 0.5f);
            }
            else
            {
                const CK_PreRenderData* prd =
                    reinterpret_cast<const CK_PreRenderData*>(extra->input->pre_render_data);
                s.origin_x = prd ? prd->left : 0;
                s.origin_y = prd ? prd->top : 0;
                s.scale_x = sx;
                s.scale_y = sy;
            }

            // Keep Original needs the plate in linear RGB, pixel-aligned with
            // the output (same result rect, so same size).
            std::vector<float> plate;
            if (s.keep_original && inW->width == outW->width && inW->height == outW->height)
            {
                ReadWorldRGB(inW, fmt, s.srgb_out, true, plate);
                s.plate = plate.data();
            }

            std::vector<float> buf((size_t)outW->width * outW->height * 4);
            CK_RenderKey(*frame, s, outW->width, outW->height, buf.data());
            WriteToWorld(buf, outW, fmt);
            drew = true;
        }
    }

    // Original, not keyed yet, or frame not written yet: show the plate.
    if (!err && !drew && inW && outW)
        ERR(suites.WorldTransformSuite1()->copy(in_data->effect_ref, inW, outW, NULL, NULL));

    ERR2(PF_CHECKIN_PARAM(in_data, &p_keep));
    ERR2(PF_CHECKIN_PARAM(in_data, &p_speck));
    ERR2(PF_CHECKIN_PARAM(in_data, &p_method));
    ERR2(PF_CHECKIN_PARAM(in_data, &p_picked));
    ERR2(PF_CHECKIN_PARAM(in_data, &p_screen));
    ERR2(PF_CHECKIN_PARAM(in_data, &p_status));
    ERR2(PF_CHECKIN_PARAM(in_data, &p_gamma));
    ERR2(PF_CHECKIN_PARAM(in_data, &p_choke));
    ERR2(PF_CHECKIN_PARAM(in_data, &p_despill));
    ERR2(PF_CHECKIN_PARAM(in_data, &p_space));
    ERR2(PF_CHECKIN_PARAM(in_data, &p_mode));
    ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, CK_INPUT));
    return err;
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

extern "C" DllExport
PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr,
                                PF_PluginDataCB2 inPluginDataCallBackPtr,
                                SPBasicSuite* inSPBasicSuitePtr,
                                const char* inHostName,
                                const char* inHostVersion)
{
    // PF_REGISTER_EFFECT_EXT2 assigns into a local named 'result'.
    PF_Err result = PF_Err_INVALID_CALLBACK;

    result = PF_REGISTER_EFFECT_EXT2(inPtr,
                                     inPluginDataCallBackPtr,
                                     "CorridorKey_MinionsTake",
                                     CK_MATCH_NAME,
                                     "Minion",
                                     AE_RESERVED_INFO,
                                     "EffectMain",
                                     "https://corridorkey.com");
    return result;
}

PF_Err EffectMain(PF_Cmd cmd,
                  PF_InData* in_data,
                  PF_OutData* out_data,
                  PF_ParamDef* params[],
                  PF_LayerDef* output,
                  void* extra)
{
    PF_Err err = PF_Err_NONE;

    try
    {
        switch (cmd)
        {
            case PF_Cmd_ABOUT:
                err = About(in_data, out_data, params, output);
                break;

            case PF_Cmd_GLOBAL_SETUP:
                err = GlobalSetup(in_data, out_data, params, output);
                break;

            case PF_Cmd_PARAMS_SETUP:
                err = ParamsSetup(in_data, out_data, params, output);
                break;

            case PF_Cmd_USER_CHANGED_PARAM:
                err = UserChangedParam(in_data, out_data, params,
                                       reinterpret_cast<PF_UserChangedParamExtra*>(extra));
                break;

            case PF_Cmd_SMART_PRE_RENDER:
                err = PreRender(in_data, out_data,
                                reinterpret_cast<PF_PreRenderExtra*>(extra));
                break;

            case PF_Cmd_SMART_RENDER:
                err = SmartRender(in_data, out_data,
                                  reinterpret_cast<PF_SmartRenderExtra*>(extra));
                break;

            case PF_Cmd_EVENT:
                err = CK_HandleEvent(in_data, out_data, params, output,
                                     reinterpret_cast<PF_EventExtra*>(extra));
                break;

            case PF_Cmd_ARBITRARY_CALLBACK:
                err = HandleArbitrary(in_data, out_data,
                                      reinterpret_cast<PF_ArbParamsExtra*>(extra));
                break;

            default:
                break;
        }
    }
    catch (PF_Err& thrown)
    {
        err = thrown;
    }
    catch (...)
    {
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }

    return err;
}
