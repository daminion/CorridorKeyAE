# CorridorKey AE

Personal After Effects effect that wraps [CorridorKey 2.0](https://github.com/nikopueringer/CorridorKey2.0) by Corridor Digital (keying model by Zachary Harris). Not affiliated with Corridor Digital. CorridorKey is licensed under their modified CC BY-NC-SA 4.0 terms. If this is ever shared, it must stay free, keep the CorridorKey name and use the same license.

## How it works

- **Key Clip** runs CorridorKey's `infer.py` on the layer's source in the background and writes linear EXR results to a cache folder.
- The effect reads the cached frame for the current time and applies the live controls (Output, Despill, Matte Choke, Matte Gamma) natively in 32 bpc.

## Status

- Milestone 1 (done): parameter layout, settings file, per-effect cache folder, pass-through render.
- **Milestone 2 (testing):** Key Clip runs CorridorKey in the background (EXR alpha + fg into the cache), and the Status line shows progress. Clicking Key Clip again while it runs offers to cancel.
- Milestone 3: render the cached result (Output, Despill, Matte Choke, Matte Gamma).
- Milestone 4: Hint Mask → first-frame hint.

Setup: run `tools\CorridorKey_Setup.bat` (menu) to install CorridorKey 2.0 and point the plug-in at it.

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
