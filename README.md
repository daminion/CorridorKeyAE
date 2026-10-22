# CorridorKey AE

Personal After Effects effect that wraps [CorridorKey 2.0](https://github.com/nikopueringer/CorridorKey2.0) by Corridor Digital (keying model by Zachary Harris). Not affiliated with Corridor Digital. CorridorKey is licensed under their modified CC BY-NC-SA 4.0 terms. If this is ever shared, it must stay free, keep the CorridorKey name and use the same license.

## How it works

- **Key Clip** runs CorridorKey's `infer.py` on the layer's source in the background and writes linear EXR results to a cache folder.
- The effect reads the cached frame for the current time and applies the live controls (Output, Despill, Matte Choke, Matte Gamma) natively in 32 bpc.

## Status

- Milestone 1 (done): parameter layout, settings file, per-effect cache folder, pass-through render.
- Milestone 2 (done): Key Clip runs CorridorKey in the background (EXR alpha + fg into the cache), and the Status line shows progress. Clicking Key Clip again while it runs offers to cancel.
- Milestone 3 (done): the effect renders the cached key with live Output, Working Space, Despill, Matte Choke and Matte Gamma.
- Milestone 4 (done): Hint Mask. Pick a closed mask on the layer; at Key Clip it is rasterized at the source's first frame into a hint PNG (white = subject) and passed to CorridorKey with Hint Quality as the trust level. Mask mode/invert/feather are ignored; only the shape counts.
- Milestone 5 (done): Hint Layer. Pick a Matte Layer and set Use to First Frame or Every Frame. Its alpha (or luminance, if the layer is fully opaque) is rendered at the source frame times, resized to the source resolution, and passed as `--hint_first_frame` or as a `--hint_video` frame folder. Takes priority over the Hint Mask. The layer comes in its own pixel space with effects applied (Roto Brush works); masks on that layer itself aren't included, so precompose a masked layer first.
- Milestone 6 (done): Key Range. With Range = Layer In/Out, Key Clip keys only the layer's trimmed range plus Handles (default 8 frames each side). Movies are trimmed with ffmpeg into `frames\` in the key folder (deleted when done); sequences get just the needed frames hard-linked. Hints are read at the first keyed frame. Time-stretched layers key the whole clip; time remapping isn't supported.
- Milestone 7 (done): live progress via an AEGP idle hook; frames re-render as they arrive (GUID mix-in).
- **Milestone 8 (testing):** controls regrouped. **Hint** group: Hint Source (Off / Mask / Matte Layer first frame / every frame), Hint Mask, Matte Layer, Hint Strength (`--hint_quality`), Carry Strength (`--carry_hint_quality`). **Key Settings** group: Range, Handles, Temporal Window (Auto, or a fixed 80/48/24/8/1-frame `--hann_chunk`), Low VRAM (only with a fixed window, since CorridorKey's `--low_vram` disables its automatic window choice). Disk IDs are unchanged, so existing effects keep their values.

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

Global: `%APPDATA%\CorridorKeyAE\settings.ini`. It's created on first use; open it from the effect with **Cache > Global Settings > Open...**.

| key | meaning |
|---|---|
| `corridorkey_dir` | your CorridorKey2.0 clone (contains `infer.py`) |
| `python` | optional; blank = `<corridorkey_dir>\venv\Scripts\python.exe` |
| `cache_root` | default cache folder; blank = `%LOCALAPPDATA%\CorridorKeyAE\Cache` |

Per effect: **Cache > Cache Folder > Choose...** overrides the cache folder for that one effect and is saved with the project. **Reset** goes back to the global default.

Branches: `dev` is active development. Backup branches are `backup/<name>`.

## Third-party code

- [tinyexr](https://github.com/syoyo/tinyexr) (BSD 3-Clause) and [miniz](https://github.com/richgel999/miniz) (MIT) in `third_party/`, used to read the cached EXRs. Licenses are next to the sources.
