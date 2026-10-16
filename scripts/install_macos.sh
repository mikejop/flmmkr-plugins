#!/bin/bash
# Installs FalseColor.ofx.bundle into /Library/OFX/Plugins (asks for the admin password).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUNDLE="${1:-$ROOT/build/FalseColor.ofx.bundle}"
DEST="/Library/OFX/Plugins"
if [ ! -d "$BUNDLE" ]; then echo "Bundle not found: $BUNDLE (build first)"; exit 1; fi
sudo mkdir -p "$DEST"
sudo rm -rf "$DEST/FalseColor.ofx.bundle"
sudo cp -R "$BUNDLE" "$DEST/"
sudo xattr -dr com.apple.quarantine "$DEST/FalseColor.ofx.bundle" || true
# Ad-hoc signature (local use only; distribution needs Developer ID + notarization).
sudo codesign --force --deep --sign - "$DEST/FalseColor.ofx.bundle" || true
echo "Installed to $DEST/FalseColor.ofx.bundle. Quit and reopen DaVinci Resolve."
