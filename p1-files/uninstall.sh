#!/bin/sh
# Undo install.sh. Run ON the P1:
#
#   sudo sh uninstall.sh camera   # one part: camera | can | canivore | config | net
#   sudo sh uninstall.sh all
#
# Note: removing "can" brings back the mrccomm crash loop (SystemCore expects can_s0..4).
set -u
SD=/etc/systemd/system
say() { printf '\033[1;32m==>\033[0m %s\n' "$*"; }
[ "$(id -u)" = 0 ] || { echo "run with sudo: sudo sh $0 $*"; exit 1; }
[ $# -gt 0 ] || { echo "usage: sudo sh $0 camera|can|canivore|config|net|all"; exit 1; }

un_camera() {
  say "removing camera"
  systemctl disable --now p1-llcam-setup.service 2>/dev/null
  rm -f $SD/p1-llcam-setup.service $SD/limelight_visionserver.service.d/p1-csicam.conf \
    /usr/local/bin/p1-llcam-setup.sh /usr/local/lib/p1-v4l2-shim.so
  rm -rf /run/p1cam
  systemctl daemon-reload
  systemctl restart limelight_visionserver
}

un_can() {
  say "removing CAN placeholders"
  systemctl disable --now p1-vcan.service 2>/dev/null
  rm -f $SD/p1-vcan.service $SD/limelight_canbusprocess.service.d/p1-override.conf \
    $SD/robot.service.d/p1-override.conf
  systemctl daemon-reload
}

un_net() {
  say "removing team address service"
  systemctl disable --now p1-teamip.service 2>/dev/null
  rm -f $SD/p1-teamip.service /usr/local/bin/p1-teamip.sh
  systemctl daemon-reload
}

un_canivore() {
  say "removing CANivore driver"
  opkg remove canivore-usb
}

un_config() {
  say "restoring config.txt from config.txt.pre-p1kit"
  MNT=/tmp/p1kit-boot
  mkdir -p $MNT
  for p in /dev/mmcblk0p2 /dev/mmcblk0p3; do
    mount -t vfat "$p" $MNT 2>/dev/null || continue
    if [ -f $MNT/config.txt.pre-p1kit ]; then
      cp $MNT/config.txt.pre-p1kit $MNT/config.txt && echo "   $p restored"
    fi
    sync; umount $MNT
  done
  say "reboot to apply (sudo reboot)"
}

for p in "$@"; do
  case $p in
    camera|can|canivore|config|net) un_$p ;;
    all) un_camera; un_net; un_can; un_canivore; un_config ;;
    *) echo "unknown part '$p'"; exit 1 ;;
  esac
done
