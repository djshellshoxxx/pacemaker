#!/bin/bash -e
# Runs inside the image (chroot).
id pacemaker >/dev/null 2>&1 || useradd --system --home-dir /var/lib/pacemaker --shell /usr/sbin/nologin pacemaker
for g in audio gpio i2c; do getent group "$g" >/dev/null && usermod -a -G "$g" pacemaker || true; done
install -d -o pacemaker -g pacemaker /var/lib/pacemaker /var/log/pacemaker
systemctl enable pacemaker.service avahi-daemon.service pacemaker-firstboot.service
systemctl disable hciuart.service bluetooth.service triggerhappy.service apt-daily.timer apt-daily-upgrade.timer 2>/dev/null || true
