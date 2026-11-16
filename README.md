# CorridorKey_MinionsTake

Personal After Effects effect that wraps [CorridorKey 2.0](https://github.com/nikopueringer/CorridorKey2.0) by Corridor Digital (keying model by Zachary Harris). Not affiliated with Corridor Digital. CorridorKey is licensed under their modified CC BY-NC-SA 4.0 terms. If this is ever shared, it must stay free, keep the CorridorKey name and use the same license.

## How it works

- **Key Clip** runs CorridorKey's `infer.py` on the layer's source in the background and writes linear EXR results to a cache folder.
- The effect reads the cached frame for the current time and applies the live controls (Output, Despill, Matte Choke, Matte Gamma) natively in 32 bpc.

## Status

- Milestone 1 (done): parameter layout, settings file, per-effect cache folder, pass-through render.
- Milestone 2 (done): Key Clip runs CorridorKey in the background (EXR alpha + fg into the cache), and the Status line shows progress. Clicking Key Clip again while it runs offers to cancel.
- Milestone 3 (done): the effect renders the cached key with live Output, Working Space, Despill, Matte Choke and Matte Gamma.
- Milestone 4 (done): Hint Mask. Pick a closed mask on the layer; at Key Clip it is rasterized at the source's first frame into a hint PNG (white = subject) and passed to CorridorKey with Hint Quality as the trust level. Mask mode/invert/feather are ignored; only the shape counts.
- Milestone 5 (done): Hint Layer. Pick a Matte Layer and set Use to First Frame or Every Frame. Its alpha (or luminance, if the layer is fully opaque) is rendered at the source frame times, resized to the source resolution, and passed as `--hint_first_frame` or as a `--hint_video` frame folder. Takes priority over the Hint Mask. The layer comes in its own pixel space, and AE gives effects other layers WITHOUT their masks or effects, so a Roto Brush or masked layer must be precomposed (Move all attributes) and the precomp picked. An opaque, colourful Matte Layer (raw footage) is rejected with that advice.
- Milestone 6 (done): Key Range. With Range = Layer In/Out, Key Clip keys only the layer's trimmed range plus Handles (default 8 frames each side). Movies are trimmed with ffmpeg into `frames\` in the key folder (deleted when done); sequences get just the needed frames hard-linked. Hints are read at the first keyed frame. Time-stretched layers key the whole clip; time remapping isn't supported.
- Milestone 7 (done): live progress via an AEGP idle hook; frames re-render as they arrive (GUID mix-in).
- **Milestone 8 (testing):** controls regrouped. **Hint** group: Hint Source (Off / Mask / Matte Layer first frame / every frame), Hint Mask, Matte Layer, Hint Strength (`--hint_quality`), Carry Strength (`--carry_hint_quality`). **Key Settings** group: Range, Handles, Temporal Window (Auto, or a fixed 80/48/24/8/1-frame `--hann_chunk`), Low VRAM (only with a fixed window, since CorridorKey's `--low_vram` disables its automatic window choice). Disk IDs are unchanged, so existing effects keep their values.
- v0.9: Follow Matte Exactly (default on) passes `--no_carry_hint` for every-frame hints, so CorridorKey doesn't replace the matte with its own carried prediction on overlapping window frames.
- v0.10: shown as **CorridorKey_MinionsTake** in the **Minion** effects category (internal match name still `Minion CorridorKeyAE`, so saved projects keep working). New live controls: Screen Colour (Green / Blue / Picked + Picked Colour), Despill Method (Average = CorridorKey's, Gentle, Strong), Despeckle Size (removes matte islands under N full-res pixels), Keep Original in Solid Areas (plate colour where alpha > ~0.9). Hint Strength default lowered to 75% for new effects.
- **v0.11 (testing):**
  - **Preview Frame** button: keys just the current frame. The first click starts a preview helper (`tools/ck_preview_server.py`, embedded in the plug-in as `CK_PreviewScript.h`; regenerate with `python tools/embed_preview_script.py`) that keeps the model in GPU memory, so later clicks take seconds. It keys exactly the pixels the effect receives (previous effects included) at the current preview resolution, without hints or time awareness. Results are cached in `<cache>\_preview\<pixel hash>`, so revisiting a previewed frame is instant. The helper stops on Key Clip (frees VRAM) or after 10 idle minutes.
  - **Key Source** (Key Settings): Auto / Source File / Rendered Layer. Auto keys the footage file, or renders the layer when there's no file (precomps). Rendered Layer renders the layer's input for the key range to EXRs in the cache and keys those, so a LUT inside a precomp is keyed as seen. Effects above this one on the same layer may not be included (AE limitation), so put the conversion inside the precomp. The Composition panel must be at Full resolution. Rendered keys are identified by their pixels, so an unchanged render reuses its key.
  - Setup menu option 7: P clears Preview Frame results.
- **v0.13 (testing):**
  - Status line follows CorridorKey's log while it analyses: starting / loading model / analysing motion / analysing scene / analysing clip, window i / N, then frames, with elapsed time.
  - Rendered Key Source shows AE's progress bar with Cancel while it renders the layer's frames.
  - Despill Method: Off (appended, so saved choices don't shift).
  - Keep Original in Solid Areas is on for new effects.
- **v0.14 (testing):** Cache group is now **Save Keys In**: Project Folder (default; `<folder of the .aep>\CorridorKeys`, or the global cache while the project is unsaved) / Global Cache / Custom Folder (Choose... picks it and switches to Custom). A key stored in some project's CorridorKeys folder is found next to the open project if the project was moved or copied (the open project's copy wins). Preview Frame results and render staging for Global stay in the global cache (setup menu option 6). The old Reset and Global Settings buttons are gone; settings.ini is edited through the setup menu.
- **v1.4 (testing):** Preview Frame uses the hint too: whatever Hint Source is set (Mask or Matte Layer), it is built at the clicked frame and passed to the preview helper (resized to the preview's resolution). Hinted previews are cached separately from unhinted ones.
- **v1.3 (testing):** Hint Source **Matte Layer (current frame)**: park the time indicator where the Matte Layer is good and click Key Clip. CorridorKey only hints the first frame it keys, so the key runs in two passes from that frame, forwards to the end and backwards to the start (frames fed in reverse), and the passes are joined into one key (`tools/ck_infer_ref.py`). The Status line shows "Pass 1/2 (forward)". The Key Resolution popup from v1.2 is gone (precomp and scale instead).
- **v1.1 (testing):** Compact Keys thresholds tuned on a real 4K key: alpha up to 0.001 counts as empty (background noise) and 0.99 and above as solid. That frame's colour pass went from 20.6 MB to 5.6 MB. With Keep Original on and Matte Gamma 0.5 or higher, renders stay within a third of one 8-bit step of the full key, and the matte is identical. Keys compacted by v1.0 are compacted again.
- **v1.0 (testing):** Compact Keys keeps CorridorKey's colour only near soft edges: anywhere the matte is solid, or more than 21 px from any matte, the colour is dropped and the plate stands in (as Keep Original shows). With Keep Original on (the default) every output mode except Foreground looks the same as a full key, for any Matte Choke, Gamma or Despeckle. With Keep Original off, solid areas show the despilled plate instead of CorridorKey's colour. Keys compacted by v0.15 are compacted again the new way.
- **v0.15 (testing):**
  - **Compact Keys (smaller files)** in the Cache group, on by default. Once a key finishes, each fg frame is re-saved (PIZ) with the colour cleared wherever no matte lies within 51 px; the Status line shows "compacting i/N". Every pixel you can see is unchanged, including Matte Choke growing the edge up to 50 px; only Output > Foreground shows black away from the subject. Off = full-size fg as its own key. Reusing an existing key with it on compacts that key.
  - Image sequences are fingerprinted by frame count and the middle and last frames too, so a re-rendered sequence gets a fresh key. (Existing sequence keys get new IDs; clip keys are unchanged.)
  - Preview Frame: the loading line shows a timer, and a click while the model loads no longer sticks on "waiting - stay on that frame".
  - Preview input frames and low-VRAM scratch folders are deleted once used.

Setup: run `tools\CorridorKey_Setup.bat` (menu) to install CorridorKey 2.0 and point the plug-in at it. Option 7 manages cached keys: lists each key's clip, frames, size, date and state, and deletes picked keys, keys whose source is missing, or keys older than N days (running keys are never deleted).

## Layout

Clone into the AE SDK's `Examples\UI\` folder (the project finds the SDK through `..\..\..\`):

```
...\AfterEffectsSDK\Examples\UI\CorridorKeyAE\
    CorridorKeyAE.cpp / .h        effect
    CorridorKeyAE_Strings.*       UI strings
    CorridorKeyAE_PiPL.r          PiPL (flags must match GlobalSetup)
    CK_Platform.h / _Win.cpp      settings file, folder picker, messages
    Win\CorridorKeyAE.sln         Visual Studio 2022
```

The build output goes to `%AE_PLUGIN_BUILD_DIR%`, same as Better Feather.

## Settings

Global: `%APPDATA%\CorridorKeyAE\settings.ini`. It's created on first use and edited through the setup menu (`tools\CorridorKey_Setup.bat`).

| key | meaning |
|---|---|
| `corridorkey_dir` | your CorridorKey2.0 clone (contains `infer.py`) |
| `python` | optional; blank = `<corridorkey_dir>\venv\Scripts\python.exe` |
| `cache_root` | default cache folder; blank = `%LOCALAPPDATA%\CorridorKeyAE\Cache` |

Per effect: **Cache > Save Keys In** = Project Folder (default, `CorridorKeys` next to the .aep), Global Cache (`cache_root`), or Custom Folder (**Choose...**, saved with the project).

Branches: `dev` is active development. Backup branches are `backup/<name>`. Developer notes and handoff: [HANDOFF.md](HANDOFF.md).

## Third-party code

- [tinyexr](https://github.com/syoyo/tinyexr) (BSD 3-Clause) and [miniz](https://github.com/richgel999/miniz) (MIT) in `third_party/`, used to read the cached EXRs. Licenses are next to the sources.
