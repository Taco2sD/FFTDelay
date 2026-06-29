#!/usr/bin/env bash
#
# Build FFTDelay (Release) and produce macOS distribution packages:
#   dist/FFTDelay-<version>.pkg                   VST3 + AU installer
#   dist/FFTDelay-<version>-macOS-Standalone.zip  standalone app
#
# The installer's postinstall sets the Finder "bundle" bit (via xattr, present on
# every Mac) so the installed .vst3 shows as a single file, not a folder.
#
# Usage:  scripts/package-macos.sh [version]   (defaults to 0.1.0)
set -euo pipefail

VERSION="${1:-0.1.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REL="$ROOT/build/FFTDelay_artefacts/Release"
DIST="$ROOT/dist"
PAY="$ROOT/build/pkg-payload"
SCRIPTS="$ROOT/build/pkg-scripts"

echo "==> Building Release"
cmake -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build"

echo "==> Staging payload"
rm -rf "$DIST" "$PAY" "$SCRIPTS"
mkdir -p "$DIST" "$PAY/Library/Audio/Plug-Ins/VST3" "$PAY/Library/Audio/Plug-Ins/Components" "$SCRIPTS"
ditto "$REL/VST3/FFTDelay.vst3"    "$PAY/Library/Audio/Plug-Ins/VST3/FFTDelay.vst3"
ditto "$REL/AU/FFTDelay.component" "$PAY/Library/Audio/Plug-Ins/Components/FFTDelay.component"

cat > "$SCRIPTS/postinstall" <<'POST'
#!/bin/sh
# kHasBundle flag in the 32-byte Finder info -> Finder shows the .vst3 as a file.
FINDERINFO="0000000000000000200000000000000000000000000000000000000000000000"
for B in "/Library/Audio/Plug-Ins/VST3/FFTDelay.vst3" \
         "/Library/Audio/Plug-Ins/Components/FFTDelay.component"; do
  [ -d "$B" ] && /usr/bin/xattr -wx com.apple.FinderInfo "$FINDERINFO" "$B"
done
exit 0
POST
chmod +x "$SCRIPTS/postinstall"

echo "==> Building installer"
pkgbuild --root "$PAY" --scripts "$SCRIPTS" \
  --identifier com.radicallydecent.fftdelay --version "$VERSION" \
  --install-location / "$DIST/FFTDelay-$VERSION.pkg"

echo "==> Zipping standalone"
ditto -c -k --sequesterRsrc --keepParent \
  "$REL/Standalone/FFTDelay.app" "$DIST/FFTDelay-$VERSION-macOS-Standalone.zip"

echo "==> Done. Artifacts in $DIST:"
ls -lh "$DIST"

# Note: these artifacts are NOT code-signed or notarized. For public distribution,
# add `codesign --deep --options runtime` + `xcrun notarytool submit` + `stapler`.
