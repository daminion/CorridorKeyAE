// CorridorKey AE - After Effects wrapper for CorridorKey 2.0
//
// Milestone 1: parameter layout, global settings file, per-effect cache
// folder override, SmartFX pass-through render. Key Clip reports the
// resolved setup instead of keying.

#include "CorridorKeyAE.h"
#include "CK_Platform.h"

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
                              "CorridorKeyAE v%d cache=%s",
                              static_cast<int>(d->version),
                              d->cache_override[0] ? d->cache_override : "(default)");
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
    out_data->out_flags  |= PF_OutFlag_DEEP_COLOR_AWARE;
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
    PF_ADD_POPUPX(STR(StrID_OutputMode_Name),
                  CK_OUT_NUM,
                  CK_OUT_CUTOUT,
                  STR(StrID_OutputMode_Choices),
                  PF_ParamFlag_CANNOT_TIME_VARY,
                  OUTPUT_MODE_DISK_ID);

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
    PF_ArbitraryH default_arbH = NULL;
    ERR(NewEffectData(in_data, &default_arbH));
    if (!err)
    {
        PF_ADD_ARBITRARY2(STR(StrID_EffectData_Name),
                          0,
                          0,
                          PF_ParamFlag_CANNOT_TIME_VARY,
                          PF_PUI_NO_ECW_UI,
                          default_arbH,
                          EFFECT_DATA_DISK_ID,
                          NULL);
    }

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(CACHE_END_DISK_ID);

    out_data->num_params = CK_NUM_PARAMS;
    return err;
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

static std::string ReadCacheOverride(PF_InData* in_data, PF_ParamDef* params[])
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    std::string out;
    PF_ArbitraryH h = params[CK_EFFECT_DATA]->u.arb_d.value;
    if (!h)
        return out;

    CK_EffectData* d = reinterpret_cast<CK_EffectData*>(
        suites.HandleSuite1()->host_lock_handle(h));
    if (d)
    {
        d->cache_override[CK_PATH_MAX - 1] = '\0';
        out = d->cache_override;
        suites.HandleSuite1()->host_unlock_handle(h);
    }
    return out;
}

static PF_Err WriteCacheOverride(PF_InData* in_data, PF_ParamDef* params[],
                                 const std::string& path)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    PF_ArbitraryH h = params[CK_EFFECT_DATA]->u.arb_d.value;
    if (!h)
        return PF_Err_BAD_CALLBACK_PARAM;

    CK_EffectData* d = reinterpret_cast<CK_EffectData*>(
        suites.HandleSuite1()->host_lock_handle(h));
    if (!d)
        return PF_Err_OUT_OF_MEMORY;

    d->version = CK_ARB_VERSION;
    std::memset(d->cache_override, 0, CK_PATH_MAX);
    std::strncpy(d->cache_override, path.c_str(), CK_PATH_MAX - 1);
    suites.HandleSuite1()->host_unlock_handle(h);

    params[CK_EFFECT_DATA]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
    return PF_Err_NONE;
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

static void ReportSetup(PF_InData* in_data, PF_ParamDef* params[])
{
    CK_GlobalSettings gs = CK_LoadGlobalSettings();
    std::string override_path = ReadCacheOverride(in_data, params);
    std::string cache = CK_ResolveCacheRoot(override_path.c_str(), gs);
    std::string python = CK_ResolvePythonExe(gs);

    const char* cache_src = !override_path.empty() ? "this effect"
                          : !gs.cache_root.empty() ? "settings.ini"
                          : "built-in default";

    std::string infer = gs.corridorkey_dir.empty()
                            ? std::string()
                            : gs.corridorkey_dir + "\\infer.py";

    std::string msg;
    msg += "Keying isn't wired up yet (milestone 1). Current setup:\n\n";

    msg += "CorridorKey folder:\n  ";
    msg += gs.corridorkey_dir.empty() ? "(not set)" : gs.corridorkey_dir;
    msg += gs.corridorkey_dir.empty() ? ""
         : CK_FileExists(infer) ? "   [infer.py found]" : "   [infer.py NOT found]";

    msg += "\n\nPython:\n  ";
    msg += python.empty() ? "(not set)" : python;
    msg += python.empty() ? "" : CK_FileExists(python) ? "   [found]" : "   [NOT found]";

    msg += "\n\nCache folder (from ";
    msg += cache_src;
    msg += "):\n  ";
    msg += cache;
    msg += "   [";
    msg += DescribeCacheFolder(cache);
    msg += "]";

    msg += "\n\nSettings file:\n  ";
    msg += gs.settings_file;

    CK_ShowMessage("CorridorKey AE", msg);
}

static PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data,
                               PF_ParamDef* params[],
                               PF_UserChangedParamExtra* extra)
{
    PF_Err err = PF_Err_NONE;

    switch (extra->param_index)
    {
        case CK_KEY_CLIP:
            ReportSetup(in_data, params);
            break;

        case CK_CHOOSE_CACHE:
        {
            CK_GlobalSettings gs = CK_LoadGlobalSettings();
            std::string current = CK_ResolveCacheRoot(
                ReadCacheOverride(in_data, params).c_str(), gs);
            std::string chosen;
            if (CK_ChooseFolder(current, chosen))
            {
                err = WriteCacheOverride(in_data, params, chosen);
                if (!err)
                    CK_ShowMessage("CorridorKey AE",
                                   "This effect will cache to:\n  " + chosen);
            }
            break;
        }

        case CK_RESET_CACHE:
        {
            err = WriteCacheOverride(in_data, params, std::string());
            if (!err)
            {
                CK_GlobalSettings gs = CK_LoadGlobalSettings();
                CK_ShowMessage("CorridorKey AE",
                               "This effect now uses the default cache folder:\n  " +
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
// SmartFX render (pass-through for milestone 1)
// ---------------------------------------------------------------------------

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
    }
    return err;
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

    if (!err && inW && outW)
    {
        ERR(suites.WorldTransformSuite1()->copy(in_data->effect_ref,
                                                inW, outW, NULL, NULL));
    }

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
