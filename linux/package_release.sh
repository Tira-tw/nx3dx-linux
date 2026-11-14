#!/usr/bin/env bash
# Build a binary-only Linux release archive. No proprietary data, soundtracks,
# generated dumps, game saves or credentials are copied into the package.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION=2.4.1
SOURCE="$ROOT/build/linux-main/NX3DX"
OUT="$ROOT/build/release"
PKG="NX3DX-Linux-v${VERSION}-x86_64"
if [[ ! -x "$SOURCE" ]]; then
  echo "ERROR: Build Linux first: bash linux/build_linux.sh" >&2
  exit 1
fi
if [[ "$(uname -m)" != "x86_64" ]]; then
  echo "ERROR: Release package is currently x86_64 only" >&2
  exit 1
fi
if [[ "$($SOURCE --version)" != "NX3DX+ Linux v$VERSION (x86_64, OpenGL ES 2.0)" ]]; then
  echo "ERROR: Built binary version does not match v$VERSION. Rebuild first." >&2
  exit 1
fi
mkdir -p "$OUT/$PKG"
cp "$SOURCE" "$OUT/$PKG/NX3DX"
cp linux/RELEASE_NOTES_v2.4.1.md "$OUT/$PKG/README.md"
cat > "$OUT/$PKG/launch.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ $# -lt 1 ]]; then
  echo "Usage: ./launch.sh /absolute/path/to/your/authorized/game/data [--fullscreen|--window]"
  exit 2
fi
DATA="$1"
shift
if [[ ! -s "$DATA/menu.bin" ]]; then
  echo "ERROR: game data missing: $DATA/menu.bin" >&2
  exit 1
fi
exec ./NX3DX --data "$DATA" "$@"
EOF
chmod 755 "$OUT/$PKG/launch.sh" "$OUT/$PKG/NX3DX"
tar -C "$OUT" -czf "$OUT/$PKG.tar.gz" "$PKG"
(cd "$OUT" && sha256sum "$PKG.tar.gz" > "$PKG.tar.gz.sha256")
echo "Created: $OUT/$PKG.tar.gz"
echo "SHA256: $OUT/$PKG.tar.gz.sha256"
echo "Only the Linux executable, launcher and release notes are included."
