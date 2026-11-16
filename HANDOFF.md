# CorridorKey_MinionsTake: developer handoff

Notes for picking this project up in a new session. README.md covers what the effect does; this file covers how it's built, the rules that keep saved projects working, and what's next.

## Workflow

- Repo `daminion/CorridorKeyAE`. All work is committed to **`dev`**. Before anything risky, push a `backup/<name>` branch from the current `dev`.
- Code is written and committed in a cloud session, which can't build Windows code. Eric then:
  1. runs `git pull` in `K:\After Effects Related\AfterEffectsSDK_25.6_61_win\ae25.6_61.64bit.AfterEffectsSDK\Examples\UI\CorridorKeyAE`
  2. builds Release x64 in VS2022 (the .aex lands on his Desktop)
  3. copies the .aex into AE and tests.
- A successful commit only means the source changed. Treat the code as untested until Eric has built and run it. Pure logic (pixel maths, EXR I/O, log parsing, path resolving, job command lines) can be unit-tested on Linux by pulling the functions into a small g++ harness.
- Eric's preferences: one step at a time, minimal explanation, menu scripts rather than commands to remember. He works with AE colour management off (16 bpc), so Working Space is sRGB.
- His machine: 5950X, RTX 4070 Ti Super. CorridorKey is installed at `P:\CorridorKeyAE` (venv, CUDA working). Global cache: `G:\Corridor`.

## Rules that keep saved projects working

- **Match name** `Minion CorridorKeyAE` (`CK_MATCH_NAME`) must never change. The display name is `CorridorKey_MinionsTake` and the category is `Minion`.
- **PiPL outflags must match GlobalSetup.**
  - out_flags = 33587200 (DEEP_COLOR_AWARE + CUSTOM_UI)
  - out_flags2 = 136320000 (0x8201400: THREADED_RENDERING + I_MIX_GUID_DEPENDENCIES 0x200000 + FLOAT_COLOR_AWARE + SMART_RENDER)
- **Version:** `MAJOR_VERSION`/`MINOR_VERSION` in CorridorKeyAE.h and `AE_Effect_Version` in the PiPL move together: (MAJOR << 19) + (MINOR << 15). MINOR is only 4 bits, so it goes 0-15, then MAJOR goes up (0.16 overflowed and AE reported a version mismatch). Current: **1.0 = 524288**.
- **Disk IDs** are append-only. Retired IDs stay listed in the enum and are never reused. Parameter *indices* can be reordered freely.
- **Popup choices** are stored 1-based, so new choices are appended at the end (e.g. Despill Method "Off" = 4).
- **`CK_EffectData`** (flattened into the Status arb param) is a fixed POD struct. New fields come out of `reserved[]`; unflatten zero-fills anything an older project doesn't have.
- **Key folder IDs:** the hash in `CK_JobDirFor` only adds optional parts when they're used, so existing key folder names stay valid. Changing the formula orphans every cached key.

## Architecture (files)

| file | role |
|---|---|
| CorridorKeyAE.cpp / .h | params, Key Clip, Preview Frame, SmartFX PreRender/SmartRender, idle hook, project-folder lookup |
| CorridorKeyAE_UI.cpp | Status line (Drawbot) and its idle refresh |
| CK_Job.* | background `infer.py` jobs: hashing, trimming, command line, status, log-phase parsing, project-folder key resolving, Compact Keys worker |
| CK_Preview.* + tools/ck_preview_server.py | Preview Frame helper process (model stays loaded; JSON over TCP). The script is embedded as CK_PreviewScript.h; regenerate with `python tools/embed_preview_script.py` |
| CK_Render.* | live controls: despill, choke, gamma, despeckle, Keep Original |
| CK_Frames.* | EXR read/write (tinyexr + miniz) with an 8-frame LRU; fg compaction |
| CK_Hint.* | mask rasterising, hint PNG, layer to hint |
| CK_Platform.h / _Win.cpp | settings.ini, dialogs, processes, files, TCP |
| tools/CorridorKey_Setup.bat/.ps1 | setup menu (1-8; 7 = manage cached keys, 8 = open settings.ini) |

### Key mechanics

- **Background jobs:** `cmd.exe` runs `infer.py` hidden. `<key>\done.flag` / `failed.flag` / `pid.txt` / `log.txt` give the state.
  - The Status line parses the log tail for CorridorKey's lines: "Loading CorridorKey", "RVM continuous prepass", "frozen VFM prepass", "Hann stage1 ... windows=N", "window i/N". Elapsed time comes from pid.txt's last-write time.
- **Live refresh:** an AEGP idle hook polls tracked jobs, preview state and the project folder, then calls `PF_RefreshAllWindows`. Re-renders are driven by the GUID mix-in in PreRender: job folder, generation, per-frame availability, and the preview token/state at the clicked frame.
- **Save Keys In (v0.14):**
  - **Project Folder** (default) writes to `<.aep folder>\CorridorKeys`, or the global cache if the project is unsaved.
  - **Global Cache** writes to `cache_root`.
  - **Custom Folder** uses `cache_override`.
  - `CK_ReadEffectData` passes the stored key folder through `CK_ResolveJobDir`. If it sits in some project's `CorridorKeys` folder and the same key exists next to the open project, the open project's copy wins.
  - The project folder is read on the main thread only (idle hook and Key Clip) and cached for render threads.
  - Preview Frame results always go to the global cache.
- **Rendered Key Source (precomps):** Key Clip checks out the layer frame by frame into `<cache>\_staging_*`, inside AE's progress dialog (PFAppSuite6, with Cancel). The staging folder is then moved into `<key>\frames`.

## Status

- **Verified in AE:** keying, live progress, per-click Preview Frame status, precomp keying, the v0.13 analysis feedback, and the v0.14 cache location (Eric: "working nicely so far").
- **Built, not yet tested:** Preview Frame's "loading model" line now shows an elapsed timer (pid.txt age via `CK_PreviewLoadingSeconds`; the idle hook redraws every pass while the helper is loading).
- **Built, not yet tested:** clicking Preview Frame while the helper is loading used to sit on "waiting - stay on that frame" long after loading finished (the render had already returned the plate, and nothing re-triggered it). `GetPreviewFrame` now holds the clicked frame's render until the helper is ready (polls `PF_ABORT`, 3 min cap).
- **Built, not yet tested:** cache cleanup. Preview Frame deletes its `in` frame once keyed, and low-VRAM jobs delete `<key>\tmp` when they finish. (Staged input frames were already half-float ZIP, and `<key>\frames` was already removed after each job.)
- **Compact Keys (v0.15 tested by Eric: 1.13 GB -> 841 MB fg on a 4K shot; v1.0 (was going to be 0.16) built, not yet tested).** Checkbox in the Cache group, default on. Key Clip writes `compact.flag`; when `CK_QueryJob` sees the key done (and no `compact2.done`) it queues it for a worker thread in CK_Job.cpp. The worker rewrites each `fg_*.exr` via `CK_CompactFgFile` to `fg_*.tmp`, then `CK_ReplaceFile` (retries if a render has it open). Resume point `compact2.txt`, finished `compact2.done` (v0.15's `compact.*` markers are ignored, so those keys get redone). `CK_StartJob` cancels and clears it on re-key; GLOBAL_SETDOWN joins the worker. Off adds `|full` to the key ID.
  - **v1.0 rule (`CK_CompactFg`):** fg is kept only where the square neighbourhood of `CK_COMPACT_BORDER` (21 = Matte Choke's ±20 slider limit + 1) holds both an alpha > 0 and an alpha < `CK_COMPACT_SOLID` (0.999, which stays ≥ 0.98 even at Matte Gamma 0.1). Everything else becomes NaN, meaning "use the plate". `CK_Frame::sparse` makes SmartRender read the plate even with Keep Original off; `CK_RenderKey` swaps NaN (`CK_IsMissing`, a bit test) for the plate. The GUID mix-in includes `compacted`, so AE re-renders once a key finishes compacting.
  - **Linux tests:** the rule against brute force; 900 random Choke/Gamma/Despeckle/mode cases render identically from full and compacted keys with Keep Original on; a grainy 4K frame went from 29 MB to 2.3 MB; NaN survives the EXR round trip; worker queue/cancel/resume.
  - **If the Choke slider range ever grows, raise `CK_COMPACT_BORDER` to match.** Foreground mode shows the plate where fg was removed.
- **Built, not yet tested:** sequence key IDs also hash file count + middle + last frame stamps (changes existing sequence key IDs only).
- **Not chosen:** alpha-only keys (Eric wants CorridorKey's fg edge colour); DWAA (tinyexr can't read it). PNG 16 ruled out (keys are already half-float; PNG clips > 1.0).
- **Known gap:** neither loading line has a percentage, because CorridorKey doesn't report any while it loads.

## Ideas not yet built (roughly in priority order)

1. Internal Resolution (`--model_scale`) for faster draft keys.
2. "Input is Linear" override for EXR plates.
3. Auto hint with a threshold.
4. Reference Frame: key both directions from a chosen frame.
5. The setup menu's cache manager only knows the global cache. It could also scan a chosen project's `CorridorKeys` folder.

Also pending: Eric plans to move Better Feather into the Minion effects category himself, later.

## Licence note

CorridorKey 2.0 is under a modified CC BY-NC-SA 4.0 licence. If this is ever shared, it must stay free, keep the CorridorKey name and use the same licence. The official CorridorKey 2.0 is now a paid product with its own AE plug-in; this is a personal build.
