# Pacemaker Box SD card image

A pi-gen stage on top of Raspberry Pi OS Lite (64-bit, Bookworm) that produces a flashable image with the service, the
web UI, avahi (`pacemaker.local`), a Wi-Fi hotspot and a read-only root file system.

**Status: written but never run.** The development container has neither Docker nor network access to pi-gen, so the
scripts here are syntax-checked (`bash -n`) and follow pi-gen's documented stage layout, nothing more. Expect to fix small
things on the first real build. Everything the stage installs is also installable on a stock Pi OS with
`headless/deploy/install.sh`, which is tested logic-wise and is the faster way to try the software.

## Build

1. Build an aarch64 Release binary of `pacemaker_headless` (on a Pi 4/5, or in an arm64 container; the command is in
   the header of `build-image.sh`).
2. `./build-image.sh --binary /path/to/pacemaker_headless` (needs Docker, git, about 15 GB and an hour).
3. The image lands in `deploy-out/`. Flash it with Raspberry Pi Imager ("Use custom") or `dd`.

## What is in the stage

| Path | Purpose |
|---|---|
| `config` | pi-gen config: Lite stages plus `stage-pacemaker`, host name `pacemaker`, SSH on, **change `FIRST_USER_PASS`** |
| `stage-pacemaker/00-packages/00-packages` | apt packages (ALSA, avahi, NetworkManager, parted, ...) |
| `stage-pacemaker/01-install/00-run.sh` | installs the binary, config, systemd unit, avahi file, limits.d, helper scripts; enables I2C |
| `stage-pacemaker/01-install/01-run-chroot.sh` | creates the `pacemaker` user, enables the services |
| `.../files/pacemaker-firstboot.*` | first boot: grows root to 6 GiB, creates the `PMDATA` partition, creates the hotspot, enables the overlay root, reboots once |

First boot takes about a minute and one reboot. Afterwards the SD card is not written during normal operation: settings and
drift logs go to the `PMDATA` partition (`/var/lib/pacemaker`), everything else is RAM. To update the software over SSH:
`sudo /usr/local/share/pacemaker/overlay-root.sh --disable && sudo reboot`, install, enable again.

Pisound: after flashing, run the Blokas installer once with the root file system writable and uncomment `dtoverlay=pisound`
in `/boot/firmware/config.txt` (see `headless/README.md`).
