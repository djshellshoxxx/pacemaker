#!/usr/bin/env bash
# Read-only root filesystem for the Pacemaker Box (Raspberry Pi OS Lite 64-bit, Bookworm or newer).
#
# A power cut on a stage must never corrupt the SD card. This script
#   1. mounts a small writable data partition at /var/lib/pacemaker (settings, calibration, drift logs),
#   2. turns on raspi-config's overlay file system (root becomes read-only, changes live in RAM),
#   3. optionally makes /boot/firmware read-only too.
# Logs under /var/log live in the RAM overlay and are lost at power-off; that is intended.
#
#   sudo ./overlay-root.sh --data-device /dev/mmcblk0p3 [--boot-ro]      enable (reboot afterwards)
#   sudo ./overlay-root.sh --disable                                      back to read-write for maintenance
#   sudo ./overlay-root.sh --status
#
# Create the data partition first (once), e.g. in the free space after the root partition:
#   sudo parted /dev/mmcblk0 mkpart primary ext4 <end-of-root> 100%   &&   sudo mkfs.ext4 -L PMDATA /dev/mmcblk0p3
# The image build (headless/image) creates it for you.
set -euo pipefail

DATA_DEV=""
BOOT_RO=0
MODE="enable"
while [ $# -gt 0 ]; do
  case "$1" in
    --data-device) DATA_DEV="$2"; shift 2 ;;
    --boot-ro)     BOOT_RO=1; shift ;;
    --disable)     MODE="disable"; shift ;;
    --status)      MODE="status"; shift ;;
    -h|--help)     sed -n '2,17p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
[ "$(id -u)" -eq 0 ] || { echo "run as root (sudo)" >&2; exit 1; }
command -v raspi-config >/dev/null || { echo "raspi-config not found: this script is for Raspberry Pi OS" >&2; exit 1; }

status() {
  # raspi-config nonint get_overlay_now prints 0 when the overlay is active in the running system
  echo "overlay active now : $([ "$(raspi-config nonint get_overlay_now)" = "0" ] && echo yes || echo no)"
  echo "overlay at boot    : $([ "$(raspi-config nonint get_overlay_conf)" = "0" ] && echo yes || echo no)"
  echo "boot partition ro  : $([ "$(raspi-config nonint get_bootro_conf)" = "0" ] && echo yes || echo no)"
  findmnt -no SOURCE,FSTYPE,OPTIONS /var/lib/pacemaker || echo "/var/lib/pacemaker is not a separate mount"
}

case "$MODE" in
  status) status; exit 0 ;;
  disable)
    raspi-config nonint disable_bootro || true
    raspi-config nonint disable_overlayfs
    echo "overlay disabled; reboot to make the root file system writable again"
    exit 0 ;;
esac

[ -n "$DATA_DEV" ] || { echo "--data-device is required (a writable ext4 partition for settings and logs)" >&2; exit 2; }
[ -b "$DATA_DEV" ] || { echo "$DATA_DEV is not a block device" >&2; exit 1; }

UUID="$(blkid -s UUID -o value "$DATA_DEV")"
[ -n "$UUID" ] || { echo "$DATA_DEV has no file system; run mkfs.ext4 -L PMDATA $DATA_DEV" >&2; exit 1; }

systemctl stop pacemaker.service 2>/dev/null || true
mkdir -p /var/lib/pacemaker
# Keep what is already there (calibration, saved state) on the data partition.
if ! findmnt -rn /var/lib/pacemaker >/dev/null; then
  TMP="$(mktemp -d)"
  mount "$DATA_DEV" "$TMP"
  if [ -d /var/lib/pacemaker ] && [ -n "$(ls -A /var/lib/pacemaker 2>/dev/null)" ]; then cp -a /var/lib/pacemaker/. "$TMP"/; fi
  chown pacemaker:pacemaker "$TMP" 2>/dev/null || true
  umount "$TMP"; rmdir "$TMP"
fi
# noatime,commit=30: few writes; nofail so a missing partition never blocks boot (the service then uses the RAM overlay).
FSTAB_LINE="UUID=${UUID} /var/lib/pacemaker ext4 defaults,noatime,commit=30,nofail,x-systemd.device-timeout=5 0 2"
sed -i '\#/var/lib/pacemaker#d' /etc/fstab
echo "$FSTAB_LINE" >> /etc/fstab
systemctl daemon-reload
mount /var/lib/pacemaker

raspi-config nonint enable_overlayfs
if [ "$BOOT_RO" -eq 1 ]; then raspi-config nonint enable_bootro; fi
echo
status
echo
echo "Reboot to activate. To update the software later: sudo $0 --disable, reboot, install, then enable again."
