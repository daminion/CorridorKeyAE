// CorridorKey AE - Effect Controls UI: the Status line.

#include "CorridorKeyAE.h"
#include "CK_Job.h"

#include <adobesdk/DrawbotSuite.h>

#include <map>
#include <mutex>
#include <string>

namespace
{
    // Last text drawn per job folder, so idle only redraws on change.
    std::mutex g_mutex;
    std::map<std::string, std::string> g_last_drawn;

    struct StatusInfo
    {
        std::string key;      // job folder ("" = never keyed)
        std::string text;
        CK_JobState state = CK_JOB_NONE;
    };

    StatusInfo GetStatus(PF_InData* in_data, PF_ParamDef* params[])
    {
        StatusInfo info;
        CK_EffectData data;
        CK_ReadEffectData(in_data, params, &data);

        info.key = data.job_dir;
        CK_JobStatus st = CK_QueryJob(info.key);
        info.state = st.state;
        info.text = CK_StatusText(st, data.frames_total);
        return info;
    }

    DRAWBOT_ColorRGBA ColorFor(CK_JobState state)
    {
        DRAWBOT_ColorRGBA c;
        c.alpha = 1.0f;
        switch (state)
        {
            case CK_JOB_RUNNING:     c.red = 0.95f; c.green = 0.75f; c.blue = 0.30f; break;
            case CK_JOB_DONE:        c.red = 0.45f; c.green = 0.85f; c.blue = 0.45f; break;
            case CK_JOB_FAILED:
            case CK_JOB_OFFLINE:     c.red = 0.95f; c.green = 0.40f; c.blue = 0.35f; break;
            default:                 c.red = 0.70f; c.green = 0.70f; c.blue = 0.70f; break;
        }
        return c;
    }

    void ToUTF16(const std::string& s, DRAWBOT_UTF16Char* dst, size_t cap)
    {
        size_t i = 0;
        for (; i < s.size() && i < cap - 1; ++i)
            dst[i] = static_cast<DRAWBOT_UTF16Char>(static_cast<unsigned char>(s[i]));
        dst[i] = 0;
    }

    bool IsStatusControl(PF_EventExtra* extra)
    {
        return (*extra->contextH)->w_type == PF_Window_EFFECT &&
               extra->effect_win.index == CK_STATUS;
    }
}

static PF_Err DrawStatus(PF_InData* in_data,
                         PF_OutData* out_data,
                         PF_ParamDef* params[],
                         PF_EventExtra* extra)
{
    PF_Err err = PF_Err_NONE;

    if (!IsStatusControl(extra) || extra->effect_win.area != PF_EA_CONTROL)
        return err;

    AEGP_SuiteHandler suites(in_data->pica_basicP);
    StatusInfo info = GetStatus(in_data, params);

    DRAWBOT_Suites db;
    DRAWBOT_DrawRef drawing_ref = NULL;
    DRAWBOT_SupplierRef supplier_ref = NULL;
    DRAWBOT_SurfaceRef surface_ref = NULL;
    DRAWBOT_BrushRef brush_ref = NULL;
    DRAWBOT_FontRef font_ref = NULL;

    AEFX_CLR_STRUCT(db);
    AEFX_AcquireDrawbotSuites(in_data, out_data, &db);
    ERR(suites.EffectCustomUISuite1()->PF_GetDrawingReference(extra->contextH, &drawing_ref));

    if (!err && drawing_ref)
    {
        ERR(db.drawbot_suiteP->GetSupplier(drawing_ref, &supplier_ref));
        ERR(db.drawbot_suiteP->GetSurface(drawing_ref, &surface_ref));

        DRAWBOT_ColorRGBA color = ColorFor(info.state);
        float font_size = 11.0f;
        ERR(db.supplier_suiteP->NewBrush(supplier_ref, &color, &brush_ref));
        ERR(db.supplier_suiteP->GetDefaultFontSize(supplier_ref, &font_size));
        ERR(db.supplier_suiteP->NewDefaultFont(supplier_ref, font_size, &font_ref));

        if (!err)
        {
            const PF_UnionableRect& r = extra->effect_win.current_frame;
            DRAWBOT_PointF32 origin;
            origin.x = (float)r.left + 4.0f;
            origin.y = (float)r.top + ((float)(r.bottom - r.top) + font_size) * 0.5f - 1.0f;

            DRAWBOT_UTF16Char text[256];
            ToUTF16(info.text, text, 256);

            ERR(db.surface_suiteP->DrawString(surface_ref,
                                              brush_ref,
                                              font_ref,
                                              text,
                                              &origin,
                                              kDRAWBOT_TextAlignment_Left,
                                              kDRAWBOT_TextTruncation_EndEllipsis,
                                              (float)(r.right - r.left) - 8.0f));
        }

        if (font_ref)
            db.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(font_ref));
        if (brush_ref)
            db.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(brush_ref));
    }

    AEFX_ReleaseDrawbotSuites(in_data, out_data);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last_drawn[info.key] = info.text;
    }

    extra->evt_out_flags = PF_EO_HANDLED_EVENT;
    return err;
}

// While a key runs, ask AE to redraw the Status line whenever its text changes.
static PF_Err IdleStatus(PF_InData* in_data,
                         PF_OutData* out_data,
                         PF_ParamDef* params[],
                         PF_EventExtra* extra)
{
    if ((*extra->contextH)->w_type != PF_Window_EFFECT)
        return PF_Err_NONE;

    StatusInfo info = GetStatus(in_data, params);

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_last_drawn.find(info.key);
        changed = (it == g_last_drawn.end() || it->second != info.text);
    }

    if (changed)
        extra->evt_out_flags |= PF_EO_UPDATE_NOW;

    return PF_Err_NONE;
}

PF_Err CK_HandleEvent(PF_InData* in_data,
                      PF_OutData* out_data,
                      PF_ParamDef* params[],
                      PF_LayerDef* output,
                      PF_EventExtra* extra)
{
    switch (extra->e_type)
    {
        case PF_Event_DRAW:
            return DrawStatus(in_data, out_data, params, extra);

        case PF_Event_IDLE:
            return IdleStatus(in_data, out_data, params, extra);

        default:
            return PF_Err_NONE;
    }
}
