#!/usr/bin/env python3
"""Packs falsecolor_installer.py + payload/ into a single double-clickable program with PyInstaller.
Run on the target OS (PyInstaller does not cross-compile):  pip install pyinstaller && python installer/build_installer.py
Output in installer/dist-out: .app (macOS, plus .dmg), .exe (Windows), single binary (Linux)."""
import os, shutil, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
if not os.path.isdir(os.path.join(here, "payload", "FalseColor.ofx.bundle")):
    sys.exit("payload/FalseColor.ofx.bundle missing: run scripts/assemble_payload.py first")
sep = ";" if sys.platform.startswith("win") else ":"
name = "FLMMKR FALSE COLOR Installer"
cmd = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--windowed", "--name", name,
       "--distpath", os.path.join(here, "dist-out"), "--workpath", os.path.join(here, "build-tmp"),
       "--specpath", os.path.join(here, "build-tmp"),
       "--add-data", os.path.join(here, "payload") + sep + "payload",
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
print("done ->", os.path.join(here, "dist-out"))
