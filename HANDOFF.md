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
- **Version:** `MAJOR_VERSION`/`MINOR_VERSION` in CorridorKeyAE.h and `AE_Effect_Version` in the PiPL move together: (MAJOR << 19) + (MINOR << 15). MINOR is only 4 bits, so it goes 0-15, then MAJOR goes up (0.16 overflowed and AE reported a version mismatch). Current: **1.4 = 655360**.
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
| tools/ck_infer_ref.py + CK_RefScript.h | reference-frame keys: builds forward/reversed link folders from `<key>\frames`, runs infer.py on each with the same `--hint_first_frame`, joins the outputs into `<key>\<key name>_fg_exr` / `_alpha_exr`. Prints `CK pass i/N forward|backward` and `CK joining passes` for the Status line. Written to `%LOCALAPPDATA%` per job. Regenerate the header with `python tools/embed_preview_script.py` |
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
- **Compact Keys (v0.15 tested by Eric: 1.13 GB -> 841 MB fg on a 4K shot; v1.0 tested by Eric: removed nothing on his shot, because of background alpha noise and a 0.995-0.998 interior; v1.1 thresholds verified by Eric in AE ("WAY better")).** Checkbox in the Cache group, default on. Key Clip writes `compact.flag`; when `CK_QueryJob` sees the key done (and no `compact2.done`) it queues it for a worker thread in CK_Job.cpp. The worker rewrites each `fg_*.exr` via `CK_CompactFgFile` to `fg_*.tmp`, then `CK_ReplaceFile` (retries if a render has it open). Resume point `compact3.txt`, finished `compact3.done` (older `compact.*`/`compact2.*` markers are ignored, so those keys get redone; bump the number whenever the rule changes). `CK_StartJob` cancels and clears it on re-key; GLOBAL_SETDOWN joins the worker. Off adds `|full` to the key ID.
  - **v1.0 rule (`CK_CompactFg`):** fg is kept only where the square neighbourhood of `CK_COMPACT_BORDER` (21 = Matte Choke's ±20 slider limit + 1) holds both an alpha > `CK_COMPACT_INVISIBLE` (0.001) and an alpha < `CK_COMPACT_SOLID` (0.99, which stays ≥ 0.98 for Matte Gamma ≥ 0.5). Everything else becomes NaN, meaning "use the plate". `CK_Frame::sparse` makes SmartRender read the plate even with Keep Original off; `CK_RenderKey` swaps NaN (`CK_IsMissing`, a bit test) for the plate. The GUID mix-in includes `compacted`, so AE re-renders once a key finishes compacting.
  - **Linux tests:** on Eric's real 4K frame (alpha: 65% exactly 0, ~5% noise ≤ 0.001, interior 0.995-1), fg went from 20.6 MB to 5.6 MB (80% of pixels removed). 10 random Choke / Gamma 0.5-1 cases against a pure-green plate: identical matte, max colour diff 0.34/255. Earlier synthetic tests: 900 random Choke/Gamma/Despeckle/mode cases render identically from full and compacted keys with Keep Original on; a grainy 4K frame went from 29 MB to 2.3 MB; NaN survives the EXR round trip; worker queue/cancel/resume.
  - **If the Choke slider range ever grows, raise `CK_COMPACT_BORDER` to match.** Foreground mode shows the plate where fg was removed.
- **Built, not yet tested:** sequence key IDs also hash file count + middle + last frame stamps (changes existing sequence key IDs only).
- **Not chosen:** alpha-only keys (Eric wants CorridorKey's fg edge colour); DWAA (tinyexr can't read it). PNG 16 ruled out (keys are already half-float; PNG clips > 1.0).
- **Built, not yet tested (v1.3): Matte Layer (current frame)** (Hint Source choice 5). Key Clip takes the frame under the time indicator (`in_data->current_time`) as `req.ref_index` in the keyed range and renders the Matte Layer there. If ref > 0: forces `trimmed` (so the input is always `<key>\\frames`), adds `|ref=N` to the key ID and launches ck_infer_ref.py instead of infer.py. `CK_QueryJob` counts frames in `_ref_out_fwd`/`_ref_out_bwd` while the passes run. Linux tests: the join mapping (ref in the middle, last, first, 1-frame clip) with a stand-in infer.py, and the Status text for a two-pass log. Never run against real CorridorKey.
- **Built, not yet tested (v1.4): hinted Preview Frame.** Before v1.4 the preview helper never got a hint (Eric saw his masked areas come back). `DoPreviewFrame` now builds the hint at the clicked frame for any Hint Source (`BuildHint` / `BuildHintFromLayer` with `start_frame` = clicked source frame) and stores the PNG with `CK_PreviewSetHint(owner, token)`. `GetPreviewFrame` mixes the hint into the preview folder hash, writes `<preview>\\hint.png` and passes path, Hint Strength and the input size. ck_preview_server.py resizes the hint to that size (cv2 INTER_AREA) and passes it as `initial_hint_path`.
- **Removed (v1.3): Key Resolution** (v1.2). `KEY_RES_DISK_ID` stays listed as retired. Eric prefers to precomp and scale. Note for the future: CorridorKey 485f5e9's `--model_scale` != 1 raises in `_resize_output_torch` (the output upscale isn't implemented).
- **Known gap:** neither loading line has a percentage, because CorridorKey doesn't report any while it loads.

## Ideas not yet built (roughly in priority order)

1. "Input is Linear" override for EXR plates.
2. Auto hint with a threshold.
3. The setup menu's cache manager only knows the global cache. It could also scan a chosen project's `CorridorKeys` folder.

Also pending: Eric plans to move Better Feather into the Minion effects category himself, later.

## Licence note

CorridorKey 2.0 is under a modified CC BY-NC-SA 4.0 licence. If this is ever shared, it must stay free, keep the CorridorKey name and use the same licence. The official CorridorKey 2.0 is now a paid product with its own AE plug-in; this is a personal build.
