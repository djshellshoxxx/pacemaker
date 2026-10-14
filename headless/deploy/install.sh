#!/usr/bin/env bash
# Installs the Pacemaker Box service on Raspberry Pi OS Lite (64-bit) or any systemd Linux with ALSA and avahi.
#
#   sudo ./install.sh [--binary PATH] [--hostname NAME] [--port N] [--no-start] [--uninstall]
#
# --binary    pacemaker_headless to install (default: ./pacemaker_headless, then ../../build/headless/.../pacemaker_headless)
# --hostname  system host name, advertised as NAME.local (default: pacemaker; "keep" leaves it alone)
# --port      web port written into the avahi service (default 8080; edit /etc/pacemaker/config.json to match)
# The script is idempotent: an existing /etc/pacemaker/config.json is never overwritten.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN=""
HOSTNAME_WANTED="pacemaker"
PORT=8080
START=1
UNINSTALL=0

while [ $# -gt 0 ]; do
  case "$1" in
    --binary)    BIN="$2"; shift 2 ;;
    --hostname)  HOSTNAME_WANTED="$2"; shift 2 ;;
    --port)      PORT="$2"; shift 2 ;;
    --no-start)  START=0; shift ;;
    --uninstall) UNINSTALL=1; shift ;;
    -h|--help)   sed -n '2,11p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

if [ "$(id -u)" -ne 0 ]; then echo "run as root (sudo)" >&2; exit 1; fi
command -v systemctl >/dev/null || { echo "systemd is required" >&2; exit 1; }

if [ "$UNINSTALL" -eq 1 ]; then
  systemctl disable --now pacemaker.service 2>/dev/null || true
  rm -f /etc/systemd/system/pacemaker.service /etc/avahi/services/pacemaker.service /etc/security/limits.d/99-pacemaker.conf
  rm -f /usr/local/bin/pacemaker_headless
  systemctl daemon-reload
  echo "removed the service and binary; /etc/pacemaker and /var/lib/pacemaker were kept"
  exit 0
fi

# ---- binary
if [ -z "$BIN" ]; then
  for c in "$HERE/pacemaker_headless" "$HERE/../pacemaker_headless" \
           "$HERE/../../build/headless/pacemaker_headless_artefacts/Release/pacemaker_headless" \
           "$HERE/../../build-b/headless/pacemaker_headless_artefacts/Release/pacemaker_headless"; do
    if [ -x "$c" ]; then BIN="$c"; break; fi
  done
fi
[ -n "$BIN" ] && [ -x "$BIN" ] || { echo "pacemaker_headless not found; build it or pass --binary PATH" >&2; exit 1; }
echo "binary: $BIN"

# ---- packages (Raspberry Pi OS / Debian). Skipped silently when apt is absent.
if command -v apt-get >/dev/null; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get install -y --no-install-recommends libasound2 avahi-daemon alsa-utils libstdc++6 || \
    apt-get install -y --no-install-recommends libasound2t64 avahi-daemon alsa-utils libstdc++6
fi

# ---- user and directories
if ! id pacemaker >/dev/null 2>&1; then
  useradd --system --home-dir /var/lib/pacemaker --shell /usr/sbin/nologin pacemaker
fi
for g in audio gpio i2c; do
  if getent group "$g" >/dev/null; then usermod -a -G "$g" pacemaker; fi
done
install -d -o pacemaker -g pacemaker -m 0755 /var/lib/pacemaker /var/log/pacemaker
install -d -m 0755 /etc/pacemaker /usr/local/share/pacemaker

# ---- files
install -m 0755 "$BIN" /usr/local/bin/pacemaker_headless
if [ ! -e /etc/pacemaker/config.json ]; then
  install -m 0644 "$HERE/config.json" /etc/pacemaker/config.json
  echo "wrote /etc/pacemaker/config.json (edit it: audio inputs, MIDI port, token)"
else
  echo "kept existing /etc/pacemaker/config.json"
  install -m 0644 "$HERE/config.json" /etc/pacemaker/config.json.example
fi
[ -f "$HERE/../README.md" ] && install -m 0644 "$HERE/../README.md" /usr/local/share/pacemaker/README.md
install -m 0644 "$HERE/99-pacemaker-limits.conf" /etc/security/limits.d/99-pacemaker.conf
install -m 0644 "$HERE/pacemaker.service" /etc/systemd/system/pacemaker.service
if [ -d /etc/avahi/services ]; then
  sed "s#<port>8080</port>#<port>${PORT}</port>#g" "$HERE/pacemaker.avahi.service" > /etc/avahi/services/pacemaker.service
  chmod 0644 /etc/avahi/services/pacemaker.service
fi

# ---- host name -> pacemaker.local
if [ "$HOSTNAME_WANTED" != "keep" ] && [ "$(hostname)" != "$HOSTNAME_WANTED" ]; then
  if command -v hostnamectl >/dev/null; then hostnamectl set-hostname "$HOSTNAME_WANTED"; else hostname "$HOSTNAME_WANTED"; fi
  sed -i "s/^127\.0\.1\.1.*/127.0.1.1\t${HOSTNAME_WANTED}/" /etc/hosts || true
  echo "host name set to $HOSTNAME_WANTED (reachable as ${HOSTNAME_WANTED}.local)"
fi

# ---- start
/usr/local/bin/pacemaker_headless --check-config --config /etc/pacemaker/config.json
systemctl daemon-reload
systemctl enable pacemaker.service
systemctl restart avahi-daemon 2>/dev/null || true
if [ "$START" -eq 1 ]; then
  systemctl restart pacemaker.service
  sleep 2
  systemctl --no-pager --lines=5 status pacemaker.service || true
fi
echo "done. Web UI: http://$(hostname).local:${PORT}/   drummer page: http://$(hostname).local:${PORT}/metronome"
