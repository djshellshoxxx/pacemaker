#!/usr/bin/env bash
# Builds the Pacemaker Box SD card image with pi-gen (https://github.com/RPi-Distro/pi-gen), Docker based.
#
#   ./build-image.sh --binary /path/to/aarch64/pacemaker_headless [--pi-gen DIR] [--out DIR]
#
# The binary must be an aarch64 Linux build (Release): build it on a Pi 4/5, or with
#   docker run --rm --platform linux/arm64 -v "$PWD/../..":/src -w /src debian:bookworm bash -c \
#     'apt-get update && apt-get install -y build-essential cmake ninja-build git libasound2-dev libfreetype-dev libfontconfig1-dev && \
#      cmake -S . -B build-arm -G Ninja -DCMAKE_BUILD_TYPE=Release -DPACEMAKER_BUILD_HEADLESS=ON && cmake --build build-arm --target pacemaker_headless'
# Needs: docker, git, ~15 GB of disk, an hour. NOT run in the development container (no Docker, no network access to pi-gen).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN=""; PIGEN="$HERE/.pi-gen"; OUT="$HERE/deploy-out"
while [ $# -gt 0 ]; do
  case "$1" in
    --binary) BIN="$2"; shift 2 ;;
    --pi-gen) PIGEN="$2"; shift 2 ;;
    --out)    OUT="$2"; shift 2 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
[ -x "$BIN" ] || { echo "--binary PATH (aarch64 pacemaker_headless) is required" >&2; exit 2; }
file "$BIN" | grep -qi "aarch64\|ARM aarch64" || { echo "warning: $BIN does not look like an aarch64 binary" >&2; }

if [ ! -d "$PIGEN/.git" ]; then
  git clone --depth 1 --branch bookworm-arm64 https://github.com/RPi-Distro/pi-gen.git "$PIGEN" 2>/dev/null || \
  git clone --depth 1 https://github.com/RPi-Distro/pi-gen.git "$PIGEN"
fi

# stage the Pacemaker files
STAGE="$PIGEN/stage-pacemaker"
rm -rf "$STAGE"; cp -r "$HERE/stage-pacemaker" "$STAGE"
F="$STAGE/01-install/files"
cp "$BIN" "$F/pacemaker_headless"
for f in config.json pacemaker.service pacemaker.avahi.service 99-pacemaker-limits.conf overlay-root.sh setup-hotspot.sh; do cp "$HERE/../deploy/$f" "$F/"; done
cp "$HERE/../README.md" "$F/README.md"
cp "$HERE/config" "$PIGEN/config"

# pi-gen builds Lite at stage2 by default: skip its image export, ours exports instead
touch "$PIGEN/stage2/SKIP_IMAGES"
rm -f "$PIGEN/stage2/EXPORT_IMAGE" 2>/dev/null || true
for s in stage3 stage4 stage5; do [ -d "$PIGEN/$s" ] && touch "$PIGEN/$s/SKIP" "$PIGEN/$s/SKIP_IMAGES"; done

( cd "$PIGEN" && ./build-docker.sh )
mkdir -p "$OUT"
cp "$PIGEN"/deploy/*.img* "$OUT"/ 2>/dev/null || cp "$PIGEN"/deploy/* "$OUT"/
echo "image written to $OUT"
