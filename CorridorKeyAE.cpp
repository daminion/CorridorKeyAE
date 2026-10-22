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
                            PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
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

    // --- Key Settings: used by the next Key Clip run -----------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC(STR(StrID_KeySettings_Name), KEY_SETTINGS_START_DISK_ID);

    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_PATH;
    def.uu.id = HINT_MASK_DISK_ID;
    PF_STRCPY(def.PF_DEF_NAME, STR(StrID_HintMask_Name));
    def.u.path_d.dephault = 0;
    ERR(PF_ADD_PARAM(in_data, -1, &def));

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(STR(StrID_HintQuality_Name),
                         0, 100, 0, 100, 95,
                         PF_Precision_INTEGER,
                         PF_ValueDisplayFlag_PERCENT,
                         PF_ParamFlag_CANNOT_TIME_VARY,
                         HINT_QUALITY_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX(STR(StrID_LowVRAM_Name),
                     FALSE,
                     PF_ParamFlag_CANNOT_TIME_VARY,
                     LOW_VRAM_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(KEY_SETTINGS_END_DISK_ID);

    // --- Cache -------------------------------------------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC(STR(StrID_Cache_Name), CACHE_START_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON(STR(StrID_ChooseCache_Name),
                  STR(StrID_ChooseCache_Button),
                  0,
                  PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY,
                  CHOOSE_CACHE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON(STR(StrID_ResetCache_Name),
                  STR(StrID_ResetCache_Button),
                  0,
                  PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY,
                  RESET_CACHE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_BUTTON(STR(StrID_EditSettings_Name),
                  STR(StrID_EditSettings_Button),
                  0,
                  PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY,
                  EDIT_SETTINGS_DISK_ID);

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
// Layer source lookup
// ---------------------------------------------------------------------------

static bool GetLayerSource(PF_InData* in_data, CK_JobRequest& req, std::string& error)
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
    if (type != AEGP_ItemType_FOOTAGE)
    {
        error = "Key Clip works on footage layers (a movie file or image sequence).\n\n"
                "Precomps aren't supported yet - apply the effect to the footage itself.";
        return false;
    }

    AEGP_FootageH footageH = NULL;
    A_long num_files = 0, files_per_frame = 0;
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

    if (err || req.source_path.empty() || !CK_FileExists(req.source_path))
    {
        error = req.source_path.empty()
                    ? "This layer has no source file (solid, text or placeholder)."
                    : "The source file is missing:\n  " + req.source_path;
        return false;
    }

    req.is_sequence = num_files > 1;

    AEGP_FootageInterp interp;
    AEFX_CLR_STRUCT(interp);
    if (!suites.FootageSuite5()->AEGP_GetFootageInterpretation(itemH, FALSE, &interp))
        req.fps = interp.conform_fpsF > 0 ? interp.conform_fpsF : interp.native_fpsF;
    if (req.fps <= 0)
        req.fps = 24.0;

    A_Time dur;
    AEFX_CLR_STRUCT(dur);
    if (!suites.ItemSuite9()->AEGP_GetItemDuration(itemH, &dur) && dur.scale)
        req.frames_total = (int)((double)dur.value / (double)dur.scale * req.fps + 0.5);
    if (req.is_sequence && num_files > 0)
        req.frames_total = num_files;

    return true;
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

static PF_Err DoKeyClip(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[])
{
    PF_Err err = PF_Err_NONE;
    const char* title = "CorridorKey AE";

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
    if (!GetLayerSource(in_data, req, error))
    {
        CK_ShowMessage(title, error);
        return err;
    }
    req.hint_quality = (float)(params[CK_HINT_QUALITY]->u.fs_d.value / 100.0);
    req.low_vram = params[CK_LOW_VRAM]->u.bd.value != 0;

    // 4. Where does it go?
    std::string cache_root = CK_ResolveCacheRoot(data.cache_override, gs);
    std::string state = DescribeCacheFolder(cache_root);
    if (state.compare(0, 7, "OFFLINE") == 0)
    {
        CK_ShowMessage(title, "The cache folder is offline:\n  " + cache_root +
                              "\n\nReconnect the drive, or choose another folder under Cache.");
        return err;
    }

    std::string job_dir = CK_JobDirFor(cache_root, req);
    CK_JobStatus existing = CK_QueryJob(job_dir);

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
    else if (!CK_StartJob(job_dir, req, python, gs.corridorkey_dir, error))
    {
        CK_ShowMessage(title, error);
        return err;
    }

    SetPathField(data.job_dir, job_dir);
    data.frames_total = req.frames_total;
    data.fps_x1000 = (A_long)(req.fps * 1000.0 + 0.5);
    data.generation++;
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
                    CK_ShowMessage("CorridorKey AE",
                                   "This effect will cache new keys to:\n  " + chosen);
            }
            break;
        }

        case CK_RESET_CACHE:
        {
            CK_EffectData data;
            CK_ReadEffectData(in_data, params, &data);
            SetPathField(data.cache_override, std::string());
            err = CK_WriteEffectData(in_data, params, data);
            if (!err)
            {
                CK_GlobalSettings gs = CK_LoadGlobalSettings();
                CK_ShowMessage("CorridorKey AE",
                               "This effect now caches new keys to the default folder:\n  " +
                               CK_ResolveCacheRoot("", gs));
            }
            break;
        }

        case CK_EDIT_SETTINGS:
            CK_OpenSettingsFile(CK_LoadGlobalSettings());
            break;

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

static bool ReadEffectDataFromParam(PF_InData* in_data, PF_ParamDef& p, CK_EffectData* out)
{
    PF_ParamDef* params[CK_NUM_PARAMS] = { 0 };
    params[CK_STATUS] = &p;
    return CK_ReadEffectData(in_data, params, out);
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

    bool drew = false;

    if (!err && inW && outW && p_mode.u.pd.value != CK_OUT_ORIGINAL)
    {
        CK_EffectData data;
        if (ReadEffectDataFromParam(in_data, p_status, &data) && data.job_dir[0])
        {
            double fps = data.fps_x1000 > 0 ? data.fps_x1000 / 1000.0
                       : (ts > 0 ? (double)sc / (double)ts : 24.0);
            int index = (int)std::floor((double)t / (double)sc * fps + 1e-3);

            std::shared_ptr<const CK_Frame> frame =
                CK_GetKeyedFrame(data.job_dir, index, data.generation);

            if (frame)
            {
                CK_RenderSettings s;
                s.mode = p_mode.u.pd.value;
                s.srgb_out = p_space.u.pd.value != CK_SPACE_LINEAR;
                s.despill = (float)(p_despill.u.fs_d.value / 100.0);
                s.choke_px = (float)p_choke.u.fs_d.value;
                s.gamma = (float)p_gamma.u.fs_d.value;

                const CK_PreRenderData* prd =
                    reinterpret_cast<const CK_PreRenderData*>(extra->input->pre_render_data);
                s.origin_x = prd ? prd->left : 0;
                s.origin_y = prd ? prd->top : 0;
                s.scale_x = in_data->downsample_x.num
                                ? (float)in_data->downsample_x.den / (float)in_data->downsample_x.num
                                : 1.0f;
                s.scale_y = in_data->downsample_y.num
                                ? (float)in_data->downsample_y.den / (float)in_data->downsample_y.num
                                : 1.0f;

                std::vector<float> buf((size_t)outW->width * outW->height * 4);
                CK_RenderKey(*frame, s, outW->width, outW->height, buf.data());

                PF_PixelFormat fmt = PF_PixelFormat_ARGB32;
                AEFX_SuiteScoper<PF_WorldSuite2> world_suite(in_data,
                                                             kPFWorldSuite,
                                                             kPFWorldSuiteVersion2,
                                                             out_data);
                ERR(world_suite->PF_GetPixelFormat(outW, &fmt));
                if (!err)
                {
                    WriteToWorld(buf, outW, fmt);
                    drew = true;
                }
            }
        }
    }

    // Original, not keyed yet, or frame not written yet: show the plate.
    if (!err && !drew && inW && outW)
        ERR(suites.WorldTransformSuite1()->copy(in_data->effect_ref, inW, outW, NULL, NULL));

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
                                     "CorridorKey AE",
                                     CK_MATCH_NAME,
                                     "Keying",
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
