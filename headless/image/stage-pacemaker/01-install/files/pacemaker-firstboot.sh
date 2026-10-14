#!/usr/bin/env bash
# First boot of the Pacemaker Box image:
#   1. root partition grows to ROOT_GIB (not to the whole card), a PMDATA partition takes the rest,
#   2. the Wi-Fi hotspot is created,
#   3. the read-only overlay root is enabled and the box reboots once.
# UNTESTED on hardware: written to the pi-gen / Raspberry Pi OS Bookworm layout (p1 boot, p2 root).
set -euo pipefail
ROOT_GIB="${ROOT_GIB:-6}"
FLAG=/var/lib/pacemaker/.firstboot-done
exec > >(tee -a /var/log/pacemaker-firstboot.log) 2>&1
echo "pacemaker first boot $(date -Is)"

ROOT_DEV="$(findmnt -no SOURCE /)"             # e.g. /dev/mmcblk0p2
DISK="/dev/$(lsblk -no pkname "$ROOT_DEV")"
PNUM="${ROOT_DEV##*p}"
DATA_DEV="${DISK}p3"

if ! blkid -L PMDATA >/dev/null 2>&1; then
  parted -s "$DISK" resizepart "$PNUM" "${ROOT_GIB}GiB"
  resize2fs "$ROOT_DEV"
  parted -s "$DISK" mkpart primary ext4 "${ROOT_GIB}GiB" 100%
  partprobe "$DISK"; sleep 2
  mkfs.ext4 -F -L PMDATA "$DATA_DEV"
fi
DATA_DEV="$(blkid -L PMDATA)"

/usr/local/share/pacemaker/setup-hotspot.sh || echo "hotspot not configured (no wlan0?)"

mkdir -p /var/lib/pacemaker && mount "$DATA_DEV" /var/lib/pacemaker 2>/dev/null || true
chown pacemaker:pacemaker /var/lib/pacemaker
touch "$FLAG"
umount /var/lib/pacemaker 2>/dev/null || true
/usr/local/share/pacemaker/overlay-root.sh --data-device "$DATA_DEV" --boot-ro
# overlay-root.sh left the flag on the data partition mount; make sure it is there after the remount
mount "$DATA_DEV" /var/lib/pacemaker 2>/dev/null || true
touch "$FLAG"
sync
systemctl reboot
