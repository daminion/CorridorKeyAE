"""Keys a frame folder outward from a reference frame (Hint Source: Matte
Layer, Current Frame).

CorridorKey only takes a single-frame hint on the first frame it keys, so
this runs infer.py twice, both starting at the reference frame:
  pass 1: frames ref .. end         (forward)
  pass 2: frames ref .. 0 reversed  (backward)
each with the same --hint_first_frame, then joins the two into one sequence
in the key folder, named as a normal key would be.

Usage (working directory = the CorridorKey folder):
  python ck_infer_ref.py --ck_ref N --input <frame dir> --output_dir <key dir>
                         <any other infer.py arguments>
Embedded in the plug-in as CK_RefScript.h (tools/embed_preview_script.py).
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys


def log(msg):
    print(msg, flush=True)


def frame_paths(d):
    # Same rule as CorridorKey's video_io.frame_paths.
    paths = []
    for ext in ("*.exr", "*.png", "*.jpg", "*.jpeg"):
        paths.extend(glob.glob(os.path.join(d, ext)))
    return sorted(paths)


def link_or_copy(src, dst):
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def make_pass_dir(path, files):
    shutil.rmtree(path, ignore_errors=True)
    os.makedirs(path)
    for i, src in enumerate(files):
        ext = os.path.splitext(src)[1]
        link_or_copy(src, os.path.join(path, "f_%06d%s" % (i, ext)))


def find_sub(d, suffix):
    for name in sorted(os.listdir(d)):
        if name.endswith(suffix) and os.path.isdir(os.path.join(d, name)):
            return os.path.join(d, name)
    return None


def run_pass(label, frames_dir, out_dir, rest):
    shutil.rmtree(out_dir, ignore_errors=True)
    log("CK pass %s" % label)
    cmd = [sys.executable, os.path.join(os.getcwd(), "infer.py"),
           "--input", frames_dir, "--output_dir", out_dir] + rest
    return subprocess.call(cmd) == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ck_ref", type=int, required=True)
    ap.add_argument("--input", required=True)
    ap.add_argument("--output_dir", required=True)
    args, rest = ap.parse_known_args()

    files = frame_paths(args.input)
    n = len(files)
    ref = max(0, min(args.ck_ref, n - 1))
    if n == 0:
        log("CK error: no frames in %s" % args.input)
        return 1
    log("CK reference frame %d of %d" % (ref, n))

    key = os.path.normpath(args.output_dir)
    work = {
        "fwd_in": os.path.join(key, "_ref_in_fwd"),
        "bwd_in": os.path.join(key, "_ref_in_bwd"),
        "fwd_out": os.path.join(key, "_ref_out_fwd"),
        "bwd_out": os.path.join(key, "_ref_out_bwd"),
    }

    passes = []   # (label, in, out, frame index of output i)
    total = (1 if ref < n - 1 else 0) + (1 if ref > 0 else 0)
    k = 0
    if ref < n - 1 or ref == 0:
        k += 1
        make_pass_dir(work["fwd_in"], files[ref:])
        passes.append(("%d/%d forward" % (k, max(total, 1)), work["fwd_in"], work["fwd_out"],
                       lambda i: ref + i))
    if ref > 0:
        k += 1
        make_pass_dir(work["bwd_in"], files[ref::-1])
        passes.append(("%d/%d backward" % (k, max(total, 1)), work["bwd_in"], work["bwd_out"],
                       lambda i: ref - i))

    for label, d_in, d_out, _ in passes:
        if not run_pass(label, d_in, d_out, rest):
            log("CK error: pass %s failed" % label)
            return 1

    # Join: <key>\<key name>_fg_exr\fg_000000.exr ..., as a normal key.
    # Built under temporary names and renamed at the end (alpha first, then
    # fg), so the plug-in never sees a half-joined key.
    log("CK joining passes")
    base = os.path.basename(key)
    staged = {}
    for kind in ("fg", "alpha"):
        final = os.path.join(key, "_joining_%s" % kind)
        staged[kind] = final
        shutil.rmtree(final, ignore_errors=True)
        os.makedirs(final)
        for _, _, d_out, index_of in passes:
            sub = find_sub(d_out, "_%s_exr" % kind)
            if sub is None:
                continue
            for i, src in enumerate(sorted(glob.glob(os.path.join(sub, "*.exr")))):
                dst = os.path.join(final, "%s_%06d.exr" % (kind, index_of(i)))
                if not os.path.exists(dst):   # the reference frame: forward's copy wins
                    os.replace(src, dst)

    for kind in ("alpha", "fg"):
        final = os.path.join(key, "%s_%s_exr" % (base, kind))
        shutil.rmtree(final, ignore_errors=True)
        os.replace(staged[kind], final)

    for path in work.values():
        shutil.rmtree(path, ignore_errors=True)
    log("CK done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
