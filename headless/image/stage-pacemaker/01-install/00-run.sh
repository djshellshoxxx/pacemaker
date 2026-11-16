#!/bin/bash -e
# Runs on the build host. Copies the Pacemaker files (staged into ./files by build-image.sh) into the image.
install -d "${ROOTFS_DIR}/etc/pacemaker" "${ROOTFS_DIR}/usr/local/share/pacemaker" "${ROOTFS_DIR}/etc/avahi/services" "${ROOTFS_DIR}/etc/security/limits.d"

install -m 0755 files/pacemaker_headless          "${ROOTFS_DIR}/usr/local/bin/pacemaker_headless"
install -m 0644 files/config.json                 "${ROOTFS_DIR}/etc/pacemaker/config.json"
install -m 0644 files/pacemaker.service           "${ROOTFS_DIR}/etc/systemd/system/pacemaker.service"
install -m 0644 files/pacemaker.avahi.service     "${ROOTFS_DIR}/etc/avahi/services/pacemaker.service"
install -m 0644 files/99-pacemaker-limits.conf    "${ROOTFS_DIR}/etc/security/limits.d/99-pacemaker.conf"
install -m 0755 files/overlay-root.sh             "${ROOTFS_DIR}/usr/local/share/pacemaker/overlay-root.sh"
install -m 0755 files/setup-hotspot.sh            "${ROOTFS_DIR}/usr/local/share/pacemaker/setup-hotspot.sh"
install -m 0755 files/pacemaker-firstboot.sh      "${ROOTFS_DIR}/usr/local/share/pacemaker/pacemaker-firstboot.sh"
install -m 0644 files/pacemaker-firstboot.service "${ROOTFS_DIR}/etc/systemd/system/pacemaker-firstboot.service"
install -m 0644 files/README.md                   "${ROOTFS_DIR}/usr/local/share/pacemaker/README.md"

# Enable the audio-friendly pieces of the boot configuration.
cat >> "${ROOTFS_DIR}/boot/firmware/config.txt" <<'CFG'

# --- Pacemaker Box ---
# Pisound HAT (DIN MIDI + audio): uncomment after running the Blokas installer, see /usr/local/share/pacemaker/README.md
#dtoverlay=pisound
dtparam=i2c_arm=on
dtparam=audio=off
CFG
echo "i2c-dev" >> "${ROOTFS_DIR}/etc/modules"
