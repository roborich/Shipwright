#!/usr/bin/env python3
"""Fixes dark fringes around Wind Waker-style cloud textures in a mods .o2r.

The clouds are drawn with bilinear filtering and ordinary (non-premultiplied) alpha blending, so
the filter interpolates RGB *and* alpha across the edge of a cloud. Most image editors export
fully-transparent pixels as transparent BLACK; filtering then drags the cloud's edge colour toward
black and you get a dark halo around every cloud. (The built-in textures avoid this because
gen-ww-cloud-textures.py computes colour independently of alpha, so their transparent pixels are
already white.)

The fix is "alpha bleed": flood the cloud colour outward into every transparent texel, leaving the
alpha channel untouched. Nothing visible changes; there is simply no black left to filter toward.

Usage:  ww-cloud-alpha-bleed.py <in.o2r> [-o <out.o2r>]      (default: rewrite in place, keeping a .bak)
"""
import argparse
import os
import shutil
import struct
import zipfile

import numpy as np

# Texture resource header: 0x50 bytes, with format/width/height/size at 0x40.
HEADER_LEN = 0x50
DIMS_OFF = 0x40
FMT_RGBA32 = 1

# The two horizon strips tile left-to-right (G_TX_WRAP on S), so their bleed must wrap in X.
WRAP_X = {"cloud_mae", "cloud_naka"}


def parse_texture(blob):
    """Returns (fmt, width, height, pixels HxWx4 uint8) or None if this isn't an RGBA32 texture."""
    if len(blob) < HEADER_LEN:
        return None
    fmt, width, height, size = struct.unpack_from("<IIII", blob, DIMS_OFF)
    if fmt != FMT_RGBA32 or size != width * height * 4 or len(blob) < HEADER_LEN + size:
        return None
    pixels = np.frombuffer(blob[HEADER_LEN:HEADER_LEN + size], dtype=np.uint8).reshape(height, width, 4)
    return fmt, width, height, pixels


def alpha_bleed(pixels, wrap_x):
    """Fills RGB of every alpha==0 texel from its nearest neighbours. Alpha is left untouched."""
    rgb = pixels[..., :3].astype(np.float32)
    known = pixels[..., 3] > 0
    mode_x = "wrap" if wrap_x else "edge"

    while not known.all():
        weight = known.astype(np.float32)
        acc = np.zeros_like(rgb)
        acc_w = np.zeros_like(weight)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                w = shift(weight, dy, dx, mode_x)
                acc += shift(rgb, dy, dx, mode_x, channels=True) * w[..., None]
                acc_w += w
        fillable = (~known) & (acc_w > 0)
        if not fillable.any():  # fully transparent image: nothing to bleed from
            break
        rgb[fillable] = acc[fillable] / acc_w[fillable][..., None]
        known |= fillable

    out = pixels.copy()
    out[..., :3] = np.rint(rgb).astype(np.uint8)
    return out


def shift(arr, dy, dx, mode_x, channels=False):
    """Shifts arr by (dy, dx), wrapping or clamping at the borders."""
    pad = [(1, 1), (1, 1)] + ([(0, 0)] if channels else [])
    padded = np.pad(arr, pad, mode="wrap" if mode_x == "wrap" else "edge")
    if mode_x == "wrap":  # X wraps, Y still clamps — the strips tile horizontally only
        padded[0, :] = padded[1, :]
        padded[-1, :] = padded[-2, :]
    h, w = arr.shape[:2]
    return padded[1 + dy:1 + dy + h, 1 + dx:1 + dx + w]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("archive", help="the mods .o2r containing textures/wind-waker/clouds/*")
    ap.add_argument("-o", "--output", help="write here instead of rewriting the input in place")
    args = ap.parse_args()

    with zipfile.ZipFile(args.archive) as zin:
        entries = [(info, zin.read(info.filename)) for info in zin.infolist()]

    fixed = []
    for i, (info, blob) in enumerate(entries):
        parsed = parse_texture(blob)
        if parsed is None:
            print(f"  skip    {info.filename} (not an RGBA32 texture)")
            continue
        _, width, height, pixels = parsed
        bled = alpha_bleed(pixels, os.path.basename(info.filename) in WRAP_X)
        changed = int((bled[..., :3] != pixels[..., :3]).any(axis=2).sum())
        entries[i] = (info, blob[:HEADER_LEN] + bled.tobytes() + blob[HEADER_LEN + pixels.nbytes:])
        fixed.append(info.filename)
        print(f"  bleed   {info.filename} ({width}x{height}) — {changed} transparent texels recoloured")

    if not fixed:
        raise SystemExit("no RGBA32 textures found — is this a cloud texture pack?")

    out = args.output
    if out is None:
        out = args.archive
        shutil.copy2(args.archive, args.archive + ".bak")
        print(f"  backup  {args.archive}.bak")

    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as zout:
        for info, blob in entries:
            zout.writestr(info, blob)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
