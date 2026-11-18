#!/bin/sh
# Luma P1: present the onboard OV9281 (CSI) to Limelight's visionserver as if it were an
# "Arducam OV9281 USB" camera on SystemCore's first USB camera port (usb3 3-1.1, "port 0").
#
# 1. Route the CSI receiver straight to its video node as 8-bit GREY (no ISP / libcamera,
#    whose Raspberry Pi IPA crashes on the SystemCore image).
# 2. Build a fake sysfs tree in /run/p1cam that limelight_visionserver bind-mounts over
#    /sys/class/video4linux and the usb3 root hub, in its own mount namespace only.
# 3. Write /run/p1cam/env for the p1-v4l2-shim.so preload (device paths, size, frame timing).
#
# If no OV9281 is found (e.g. config.txt not patched yet) this does nothing and exits 0;
# visionserver then starts normally with no fakes.
W=${P1CAM_W:-1280}
H=${P1CAM_H:-800}
VBLANK=${P1CAM_VBLANK:-1022}   # ~57 fps at 1280x800
R=/run/p1cam
rm -rf $R

# At boot the sensor driver may still be probing; give it up to ~15 s.
MDEV=""; SENSOR=""
for _ in $(seq 1 30); do
  for m in /dev/media*; do
    if media-ctl -d "$m" -p 2>/dev/null | grep -q "driver *rp1-cfe"; then MDEV=$m; break; fi
  done
  [ -n "$MDEV" ] && SENSOR=$(media-ctl -d "$MDEV" -p 2>/dev/null |
    sed -n 's/^- entity [0-9]*: \(ov9281 [0-9a-f-]*\) .*/\1/p' | head -1)
  [ -n "$SENSOR" ] && break
  sleep 0.5
done
if [ -z "$MDEV" ] || [ -z "$SENSOR" ]; then
  echo "p1-llcam: no OV9281 on the CSI port (is dtoverlay=ov9281,cam0 in config.txt?); skipping"
  exit 0
fi
VIDEO=$(media-ctl -d "$MDEV" -e "rp1-cfe-csi2_ch0")
SUBDEV=$(media-ctl -d "$MDEV" -e "$SENSOR")
M="media-ctl -d $MDEV"
echo "p1-llcam: $SENSOR on $MDEV -> $VIDEO (controls $SUBDEV)"

# --- media graph -----------------------------------------------------------------------
# rp1-cfe won't start streaming until every node with an enabled link is streaming, so the
# whole PiSP front-end path has to be unlinked.
for l in '"csi2":4 -> "pisp-fe":0' '"rp1-cfe-fe_config":0 -> "pisp-fe":1' \
         '"pisp-fe":2 -> "rp1-cfe-fe_image0":0' '"pisp-fe":4 -> "rp1-cfe-fe_stats":0'; do
  $M -l "$l [0]" 2>/dev/null
done
$M -l '"csi2":4 -> "rp1-cfe-csi2_ch0":0 [1]'
F="Y8_1X8/${W}x${H} field:none colorspace:raw"
$M -V "\"$SENSOR\":0 [fmt:$F]"
$M -V "\"csi2\":0 [fmt:$F]"
$M -V "\"csi2\":4 [fmt:$F]"
v4l2-ctl -d "$VIDEO" --set-fmt-video=width=$W,height=$H,pixelformat=GREY
v4l2-ctl -d "$SUBDEV" -c vertical_blanking=$VBLANK

# --- fake sysfs ------------------------------------------------------------------------
PORT=/sys/devices/platform/axi/1000120000.pcie/1f00300000.usb/xhci-hcd.1/usb3/3-1/3-1.1/3-1.1:1.0
VN=$(basename "$VIDEO")
mkdir -p $R/v4l/$VN $R/usb3/3-1/3-1.1/3-1.1:1.0 $R/ctl
for f in dev index; do cat /sys/class/video4linux/$VN/$f > $R/v4l/$VN/$f; done
# visionserver picks its curated "Arducam OV9281 USB" profile from the name
echo "Arducam OV9281 USB Camera: Ardu" > $R/v4l/$VN/name
ln -s $PORT $R/v4l/$VN/device
D=$R/usb3/3-1/3-1.1
echo 0c45 > $D/idVendor
echo 6366 > $D/idProduct
echo 5000 > $D/speed
echo P1CSI0001 > $D/serial
echo "Arducam OV9281 USB Camera" > $D/product
echo "Arducam Technology Co., Ltd." > $D/manufacturer

# --- shim config + v4l2-ctl wrapper ----------------------------------------------------
cat > $R/env <<EOF
P1SHIM_DEV=$VIDEO
P1SHIM_SUBDEV=$SUBDEV
P1SHIM_W=$W
P1SHIM_H=$H
P1SHIM_VBLANK=$VBLANK
P1SHIM_STATE=$R/ctl
EOF
# visionserver runs `sudo v4l2-ctl -d /dev/videoN --set-ctrl ...`, and sudo drops LD_PRELOAD.
# Inside its namespace /usr/sbin/v4l2-ctl is bind-mounted to this wrapper, which re-adds it.
cp /usr/sbin/v4l2-ctl $R/v4l2-ctl.real
cat > $R/v4l2-ctl <<'EOF'
#!/bin/sh
set -a; . /run/p1cam/env; set +a
LD_PRELOAD=/usr/local/lib/p1-v4l2-shim.so exec /run/p1cam/v4l2-ctl.real "$@"
EOF
chmod 755 $R/v4l2-ctl
echo "p1-llcam: ready"
