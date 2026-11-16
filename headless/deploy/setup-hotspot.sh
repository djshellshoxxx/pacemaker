#!/usr/bin/env bash
# Wi-Fi access point for the config page (Raspberry Pi OS Bookworm and newer, NetworkManager).
# Phones join "Pacemaker-XXXX" and open http://10.42.0.1:8080/metronome (or http://pacemaker.local:8080/).
# Ethernet stays free for Ableton Link and OSC on the stage network.
#
#   sudo ./setup-hotspot.sh [--ssid NAME] [--psk PASSPHRASE] [--band bg|a] [--channel N] [--remove]
#
# Without --psk a random 12 character passphrase is generated and printed once (also saved in
# /var/lib/pacemaker/wifi-passphrase.txt, readable by root only).
set -euo pipefail

CON="PacemakerAP"
SSID=""
PSK=""
BAND="bg"
CHANNEL=""
REMOVE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --ssid)    SSID="$2"; shift 2 ;;
    --psk)     PSK="$2"; shift 2 ;;
    --band)    BAND="$2"; shift 2 ;;
    --channel) CHANNEL="$2"; shift 2 ;;
    --remove)  REMOVE=1; shift ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
[ "$(id -u)" -eq 0 ] || { echo "run as root (sudo)" >&2; exit 1; }
command -v nmcli >/dev/null || { echo "NetworkManager (nmcli) is required; see headless/README.md for the dhcpcd/hostapd alternative" >&2; exit 1; }

if [ "$REMOVE" -eq 1 ]; then nmcli con delete "$CON" 2>/dev/null || true; echo "hotspot removed"; exit 0; fi

if [ -z "$SSID" ]; then SSID="Pacemaker-$(tr -dc 'A-Z0-9' < /sys/class/net/wlan0/address 2>/dev/null | tail -c 5)"; fi
if [ -z "$PSK" ]; then PSK="$(tr -dc 'a-km-np-z2-9' < /dev/urandom | head -c 12)"; fi
[ "${#PSK}" -ge 8 ] || { echo "the passphrase needs at least 8 characters" >&2; exit 2; }

nmcli con delete "$CON" 2>/dev/null || true
ARGS=(con add type wifi ifname wlan0 con-name "$CON" autoconnect yes ssid "$SSID"
      802-11-wireless.mode ap 802-11-wireless.band "$BAND"
      ipv4.method shared ipv4.addresses 10.42.0.1/24
      ipv6.method disabled
      wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$PSK" wifi-sec.proto rsn wifi-sec.pairwise ccmp wifi-sec.group ccmp)
[ -n "$CHANNEL" ] && ARGS+=(802-11-wireless.channel "$CHANNEL")
nmcli "${ARGS[@]}"
nmcli con up "$CON" || true

mkdir -p /var/lib/pacemaker
umask 077
printf 'ssid: %s\npassphrase: %s\n' "$SSID" "$PSK" > /var/lib/pacemaker/wifi-passphrase.txt
echo "hotspot '$SSID' passphrase '$PSK' (saved in /var/lib/pacemaker/wifi-passphrase.txt)"
echo "web UI: http://10.42.0.1:8080/   drummer page: http://10.42.0.1:8080/metronome"
