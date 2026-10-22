#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include "AE_General.r"
#endif
resource 'PiPL' (16000) {
	{	/* array properties: 13 elements */
		/* [1] */
		Kind {
			AEEffect
		},
		/* [2] */
		Name {
			"CorridorKey AE"
		},
		/* [3] */
		Category {
			"Keying"
		},
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
    #elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
    #endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
#endif
		/* [6] */
		AE_PiPL_Version {
			2,
			0
		},
		/* [7] */
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/* [8] */
		AE_Effect_Version {
			229376	/* 0.7 */
		},
		/* [9] */
		AE_Effect_Info_Flags {
			0
		},
		/* [10] */
		AE_Effect_Global_OutFlags {
			33587200	/* DEEP_COLOR_AWARE + CUSTOM_UI */
		},
		AE_Effect_Global_OutFlags_2 {
			136320000	/* 0x8201400 = THREADED_RENDERING + I_MIX_GUID_DEPENDENCIES + FLOAT_COLOR_AWARE + SMART_RENDER (must match GlobalSetup) */
		},
		/* [11] */
		AE_Reserved_Info {
			8
		},
		/* [12] */
		AE_Effect_Match_Name {
			"Minion CorridorKeyAE"
		},
		/* [13] */
		AE_Effect_Support_URL {
			"https://corridorkey.com"
		}
	}
};
