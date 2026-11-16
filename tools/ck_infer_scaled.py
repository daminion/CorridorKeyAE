"""Runs CorridorKey's infer.py with a working --model_scale.

CorridorKey 2.0 (checked 2026-10-15, 485f5e9) shrinks the input and hints
for --model_scale < 1, but its output step raises "optimized runtime render
path requires native model_scale=1.0". This wrapper supplies the missing
upscale (bicubic, on the GPU) and then runs infer.py unchanged, with the
same arguments. If CorridorKey's own resize works, it is used instead.

Run with the working directory set to the CorridorKey folder.
Embedded in the plug-in as CK_InferScript.h (tools/embed_preview_script.py).
"""
import os
import runpy
import sys

ck_dir = os.getcwd()
sys.path.insert(0, ck_dir)

import torch.nn.functional as F
import corridorkey.runtime as rt

_original = rt._resize_output_torch


def _resize_output_torch(x, meta):
    try:
        return _original(x, meta)
    except RuntimeError:
        h, w = meta["output_hw"]
        return F.interpolate(x, size=(int(h), int(w)), mode="bicubic", align_corners=False)


rt._resize_output_torch = _resize_output_torch

infer = os.path.join(ck_dir, "infer.py")
sys.argv = [infer] + sys.argv[1:]
runpy.run_path(infer, run_name="__main__")
