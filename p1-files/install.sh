#!/bin/sh
# Luma P1 -> SystemCore kit installer. Run ON the P1 (after flashing SystemCore):
#
#   sudo sh install.sh            # everything
#   sudo sh install.sh camera     # just one part: config | can | canivore | camera | net
#
# Safe to re-run. Parts:
#   config    comment out the SPI CAN chip overlays the P1 doesn't have and add the OV9281
#             camera overlay, in config.txt on BOTH boot partitions (backup: config.txt.pre-p1kit)
#   can       vcan placeholders can_s0..can_s4 + drop-ins so mrccomm/robot code run without them
#   canivore  install a CTRE canivore-usb *.ipk if you put one next to this script
#   camera    onboard OV9281 -> Limelight visionserver as a USB camera (robot.local/cam0)
#   net       keep 10.TE.AM.2/24 on eth0 (from the team number) so the H.264 (WebRTC) stream
#             works on a direct Ethernet link, where the P1 otherwise only has 169.254.x.x
set -u
KIT=$(cd "$(dirname "$0")" && pwd)
SD=/etc/systemd/system
REBOOT=0
say() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*"; }

[ "$(id -u)" = 0 ] || { echo "run with sudo: sudo sh $0 $*"; exit 1; }

# A real SystemCore has MCP2518FD CAN chips on SPI; this kit would disable them.
if ls /sys/bus/spi/drivers/mcp251xfd/ 2>/dev/null | grep -q spi && [ "${P1KIT_FORCE:-0}" != 1 ]; then
  echo "This looks like a real SystemCore (MCP2518 CAN chips found). Refusing. (P1KIT_FORCE=1 to override)"
  exit 1
fi

part_config() {
  say "config.txt: disable missing SPI CAN chips, enable OV9281 camera"
  MNT=/tmp/p1kit-boot
  mkdir -p $MNT
  for p in /dev/mmcblk0p2 /dev/mmcblk0p3; do
    if grep -q "^$p " /proc/mounts; then warn "$p is mounted; skipping"; continue; fi
    mount -t vfat "$p" $MNT 2>/dev/null || { warn "could not mount $p; skipping"; continue; }
    f=$MNT/config.txt
    if [ -f "$f" ]; then
      [ -f $MNT/config.txt.pre-p1kit ] || cp "$f" $MNT/config.txt.pre-p1kit
      cp "$f" /tmp/p1kit-before
      sed -i -E 's/^dtoverlay=(spi2-2cs-pi5|spi3-1cs|spi1-2cs|sc-mcp2518-can)/#P1-no-hw# &/' "$f"
      grep -q '^dtoverlay=ov9281' "$f" ||
        printf '\n# Luma P1: onboard OV9281 global-shutter camera on CAM0\ndtoverlay=ov9281,cam0\n' >> "$f"
      if cmp -s /tmp/p1kit-before "$f"; then echo "   $p: already patched"
      else echo "   $p: patched"; REBOOT=1; fi
    else
      warn "$p has no config.txt; skipping"
    fi
    sync; umount $MNT
  done
}

part_can() {
  say "CAN: vcan placeholders can_s0..can_s4"
  install -m644 "$KIT/can/p1-vcan.service" $SD/p1-vcan.service
  for d in limelight_canbusprocess.service.d robot.service.d; do
    mkdir -p $SD/$d
    install -m644 "$KIT/can/$d/p1-override.conf" $SD/$d/p1-override.conf
  done
  systemctl daemon-reload
  systemctl enable --now p1-vcan.service
  systemctl restart limelight_canbusprocess mrccomm 2>/dev/null
  echo "   $(ip -br link | grep -c can_s) can_s interfaces; mrccomm $(systemctl is-active mrccomm)"
}

part_canivore() {
  IPK=$(ls "$KIT"/canivore-usb_*.ipk 2>/dev/null | tail -1)
  if opkg list-installed 2>/dev/null | grep -q '^canivore-usb '; then
    say "CANivore driver already installed ($(opkg list-installed | grep '^canivore-usb '))"
  elif [ -n "$IPK" ]; then
    say "CANivore: installing $(basename "$IPK")"
    opkg install "$IPK"
  else
    warn "CANivore: no canivore-usb_*.ipk next to install.sh (get it from https://ctre.download/files/systemcore/); skipping"
  fi
}

part_camera() {
  say "Camera: OV9281 -> Limelight visionserver"
  # legacy MJPEG sidecar from an earlier version of this kit
  if [ -f $SD/p1-csicam.service ]; then
    systemctl disable --now p1-csicam.service 2>/dev/null
    rm -f $SD/p1-csicam.service /usr/local/bin/p1-csicam.py
  fi
  mkdir -p /usr/local/lib $SD/limelight_visionserver.service.d
  install -m755 "$KIT/camera/p1-v4l2-shim.so" /usr/local/lib/p1-v4l2-shim.so
  install -m755 "$KIT/camera/p1-llcam-setup.sh" /usr/local/bin/p1-llcam-setup.sh
  install -m644 "$KIT/camera/p1-llcam-setup.service" $SD/p1-llcam-setup.service
  install -m644 "$KIT/camera/limelight_visionserver.service.d/p1-csicam.conf" \
    $SD/limelight_visionserver.service.d/p1-csicam.conf
  systemctl daemon-reload
  systemctl enable p1-llcam-setup.service
  if [ "$REBOOT" = 1 ]; then
    echo "   camera starts after the reboot"
    return
  fi
  systemctl stop limelight_visionserver
  systemctl restart p1-llcam-setup.service
  journalctl -u p1-llcam-setup -n 3 --no-pager -o cat | sed 's/^/   /'
  systemctl reset-failed limelight_visionserver 2>/dev/null
  systemctl start limelight_visionserver
}

part_net() {
  say "Network: team address 10.TE.AM.2/24 on eth0"
  install -m755 "$KIT/net/p1-teamip.sh" /usr/local/bin/p1-teamip.sh
  install -m644 "$KIT/net/p1-teamip.service" $SD/p1-teamip.service
  systemctl daemon-reload
  systemctl enable p1-teamip.service
  systemctl restart p1-teamip.service
  sleep 1
  T=$(tr -cd 0-9 < /etc/team_number.txt 2>/dev/null)
  if [ -z "$T" ] || [ "$T" = 0 ]; then
    warn "no team number set yet; set it on the dashboard and the address follows within 10 s"
  else
    echo "   eth0: $(ip -4 -o addr show dev eth0 | awk '{printf "%s ", $4}')"
    echo "   laptop needs an address in 10.$((T / 100)).$((T % 100)).0/24 (e.g. .5) for the H.264 stream"
  fi
}

PARTS=${*:-config can canivore camera net}
for p in $PARTS; do
  case $p in
    config|can|canivore|camera|net) part_$p ;;
    all) part_config; part_can; part_canivore; part_camera; part_net ;;
    *) echo "unknown part '$p' (config | can | canivore | camera | net)"; exit 1 ;;
  esac
done

if [ "$REBOOT" = 1 ]; then
  say "config.txt changed: reboot to apply (sudo reboot)"
else
  say "done. Camera UI: http://robot.local/cameras"
fi
