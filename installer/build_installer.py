#!/usr/bin/env python3
"""Packs falsecolor_installer.py + payload/ into a single double-clickable program with PyInstaller.
Run on the target OS (PyInstaller does not cross-compile):  pip install pyinstaller && python installer/build_installer.py
Output in installer/dist-out: .app (macOS, plus .dmg), .exe (Windows), single binary (Linux)."""
import os, shutil, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
if not os.path.isdir(os.path.join(here, "payload", "FalseColor.ofx.bundle")):
    sys.exit("payload/FalseColor.ofx.bundle missing: run scripts/assemble_payload.py first")
sep = ";" if sys.platform.startswith("win") else ":"

# The plugin goes in as payload.zip: PyInstaller would otherwise treat FalseColor.ofx as one of the
# app's own libraries (moving it, thinning it to one architecture and leaving symlinks behind).
import zipfile
bundle = os.path.join(here, "payload", "FalseColor.ofx.bundle")
zpath = os.path.join(here, "payload.zip")
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(bundle):
        for f in files:
            full = os.path.join(root, f)
            info = zipfile.ZipInfo.from_file(full, os.path.relpath(full, os.path.join(here, "payload")))
            info.external_attr = (0o755 if f.endswith(".ofx") else 0o644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(full, "rb") as fh:
                z.writestr(info, fh.read())
name = "FLMMKR FALSE COLOR Installer"
cmd = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--windowed", "--name", name,
       "--distpath", os.path.join(here, "dist-out"), "--workpath", os.path.join(here, "build-tmp"),
       "--specpath", os.path.join(here, "build-tmp"),
       "--add-data", zpath + sep + ".",
       "--add-data", os.path.join(here, "icon", "icon.png") + sep + "."]  # window icon (all OSes)
if sys.platform.startswith("win"):
    cmd += ["--onefile", "--uac-admin", "--icon", os.path.join(here, "icon", "FC.ico")]  # installs to Program Files\Common Files
elif sys.platform == "darwin":
    cmd += ["--osx-bundle-identifier", "com.flmmkr.falsecolor.installer", "--icon", os.path.join(here, "icon", "FC.icns")]
else:
    cmd += ["--onefile"]
cmd.append(os.path.join(here, "falsecolor_installer.py"))
subprocess.run(cmd, check=True)
if sys.platform == "darwin":
    out = os.path.join(here, "dist-out")
    app = os.path.join(out, name + ".app")
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", app], check=False)
    dmg = os.path.join(out, "FLMMKR-FALSE-COLOR-Installer.dmg")
    if os.path.exists(dmg): os.remove(dmg)
    subprocess.run(["hdiutil", "create", "-volname", "FLMMKR FALSE COLOR", "-srcfolder", app, "-ov", "-format", "UDZO", dmg], check=True)
shutil.rmtree(os.path.join(here, "build-tmp"), ignore_errors=True)
os.remove(zpath)
print("done ->", os.path.join(here, "dist-out"))
