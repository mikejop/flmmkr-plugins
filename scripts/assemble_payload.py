#!/usr/bin/env python3
"""Assembles installer/payload/FalseColor.ofx.bundle from per-OS build outputs.
Usage: assemble_payload.py [--mac DIR_BUNDLE] [--win DIR_BUNDLE] [--linux DIR_BUNDLE]
Each DIR is a FalseColor.ofx.bundle produced by that OS's build (CI uploads them as artifacts)."""
import os, shutil, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dest = os.path.join(root, "installer", "payload", "FalseColor.ofx.bundle")
args = dict(zip(sys.argv[1::2], sys.argv[2::2]))
subdirs = {"--mac": ("MacOS", True), "--win": ("Win64", False), "--linux": ("Linux-x86-64", False)}
os.makedirs(os.path.join(dest, "Contents"), exist_ok=True)
for flag, src in args.items():
    sub, with_plist = subdirs[flag]
    shutil.rmtree(os.path.join(dest, "Contents", sub), ignore_errors=True)
    shutil.copytree(os.path.join(src, "Contents", sub), os.path.join(dest, "Contents", sub), symlinks=True)
    if with_plist or not os.path.exists(os.path.join(dest, "Contents", "Info.plist")):
        shutil.copy(os.path.join(src, "Contents", "Info.plist"), os.path.join(dest, "Contents", "Info.plist")) if os.path.exists(os.path.join(src, "Contents", "Info.plist")) else None
print("payload:", dest)
