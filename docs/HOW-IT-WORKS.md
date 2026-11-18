# How the P1 kit works

Background for the curious. The [README](../README.md) is all you need to set it up.

## P1 vs a real SystemCore

| | Luma P1 | Real SystemCore |
|---|---|---|
| Compute | Raspberry Pi CM5, **2 GB RAM**, 16 GB eMMC | CM5, 4 GB RAM, 16 GB eMMC |
| CAN | none (we use a CANivore over USB) | 5× MCP2518FD CAN FD chips on SPI |
| I/O chip | none | RP2350 (DIO/PWM/analog/LEDs) |
| Camera | OV9281 global shutter on the CSI port (cam0) | USB cameras |
| Ports | Ethernet (passive PoE), Weidmuller power in, **one USB-A**, USB-C (flash only) | several USB, USB-C to DS, Wi-Fi AP |
| Power | 3.3–22.5 V via PoE or Weidmuller | battery |

Robot code, NetworkTables, the Driver Station, CAN through the CANivore, and Limelight vision all work. DIO/PWM/analog/IMU don't exist, so harmless faults about them show up. The only USB port is taken by the CANivore. About 1.4 GB RAM is still free with robot code and vision running.

**Power notes:** the P1's PoE is *passive*, so an active 802.3af/at injector won't power it. USB-C only puts it into flash mode. A passive kit runs the link at 100 Mbps (power uses the spare pairs), which is fine for this.

## What `install.sh` does

`p1-files/install.sh` runs on the P1 and is safe to re-run. Run one part with `sudo sh install.sh <part>`; undo with `sudo sh uninstall.sh <part>` (or `all`). It refuses to run on a real SystemCore (it checks for MCP2518 CAN chips).

| Part | What it does | Why |
|---|---|---|
| `config` | In `config.txt` on **both** boot partitions (A/B): comments out the 8 SPI CAN-chip overlays with `#P1-no-hw# ` and adds `dtoverlay=ov9281,cam0`. Backup: `config.txt.pre-p1kit`. | The P1 has no MCP2518 chips. The camera line is the same one Luma uses. |
| `can` | Creates virtual `can_s0`–`can_s4` at boot (`p1-vcan.service`). A drop-in skips the MCP2518 bitrate setup in `limelight_canbusprocess`, and another removes the robot service's 15 s "wait for CAN UP". | Without them, `mrccomm` (Driver Station comms) crashes over and over. |
| `canivore` | `opkg install`s the CTRE `.ipk` from `p1-files`. | CTRE only publishes it for image 17, but its kernel module matches beta18's kernel (`6.12.112-v8-16k` PREEMPT_RT). |
| `camera` | See below. | |
| `net` | `p1-teamip.service` keeps `10.TE.AM.2/24` on `eth0` (from `/etc/team_number.txt`). | Limelight's H.264 (WebRTC) stream never offers link-local `169.254.x` addresses, only `10.TE.AM.2` and the USB-C addresses. |

The real CANivore shows up as `can0`; the `can_s*`/`can_d*` interfaces are placeholders.

## The camera

SystemCore's Limelight vision server only accepts **USB** cameras, on four hardcoded USB port paths. The OV9281 is on CSI, and this image's libcamera can't drive it: the Raspberry Pi IPA crashes with "A list of V4L2 controls requires a ControlInfoMap". So the kit skips libcamera and makes the vision server think a USB camera is plugged in. Nothing outside the vision server changes.

1. **Raw CSI path.** `p1-llcam-setup.sh` routes the CSI receiver straight to `/dev/video0` as 8-bit `GREY` at 1280×800, skipping the ISP. All ISP front-end links must be disabled, because rp1-cfe won't start the sensor until every video node with an enabled link is streaming.
2. **Fake USB identity.** The script builds a fake `/sys` tree in `/run/p1cam`. A systemd drop-in bind-mounts it over `/sys/class/video4linux` and the `usb3` hub, only inside `limelight_visionserver`'s mount namespace. The vision server sees `/dev/video0` on USB port `3-1.1` named "Arducam OV9281 USB Camera" and uses its built-in OV9281 profile (about 69 fps).
3. **`p1-v4l2-shim.so`**, loaded with `LD_PRELOAD`:
   - **Format:** the CSI node accepts *any* pixel format and only fails at stream start, and OpenCV picks BGR24 first. The shim advertises GREY only.
   - **Controls:** the CSI node has none, so the shim emulates UVC controls. `exposure_time_absolute` and `gain` go to the sensor subdev; the rest are stored so read-backs match.
   - **`sudo v4l2-ctl`:** the vision server shells out to it, and sudo drops `LD_PRELOAD`. Inside the namespace, `/usr/sbin/v4l2-ctl` is a wrapper that loads the shim again.

**Exposure/gain mapping:** Limelight exposure is in 10 µs steps and capped at 7.5 ms for this profile. Sensor Gain 128 → 1×, 255 → about 16×. There's no auto-exposure (no ISP). To set them without the UI (saves to the current pipeline):
```bash
curl -X POST -H "Content-Type: application/json" -d '{"exposure":750.0,"cam_gain":255}' "http://robot.local:5807/update-pipeline?flush=1"
```

**Rebuild the shim** with WPILib's SystemCore cross-compiler:
```bash
cd p1-files/camera && "C:/Users/Public/wpilib/2027_alpha7/systemcore/bin/aarch64-systemcore2027-linux-gnu-gcc.exe" -O2 -Wall -shared -fPIC -o p1-v4l2-shim.so p1-v4l2-shim.c -ldl
```
To log every control write, uncomment `P1SHIM_TRACE=1` in `camera/limelight_visionserver.service.d/p1-csicam.conf`.

## The robot code changes

Branch [`p1-flywheel`](https://github.com/Hemlock5712/Workshop-Code/tree/p1-flywheel) is `mech-4-ReadingState` with:

| Change | Why |
|---|---|
| `TalonFX(21, …)` → `TalonFX(31, …)` | our bench motor's ID |
| `kV 0.12`, `kP 0.1` | lesson gains are 0, so nothing spins until tuned |
| Motion Magic cruise 100 rps, accel 50 rps/s (were 0) | with cruise velocity 0, Motion Magic holds the flywheel still |
| Removed the Arm | only the flywheel is plugged in |
| `CommandNiDsXboxController` → `CommandXboxController` | the NiDs mapping only works with the NI Driver Station, not the FIRST one |

Configs are applied once at startup, which is why robot code must be restarted after a firmware update. They also overwrite anything set in Tuner X.

## Patching `config.txt` by hand

Instead of the installer's `config` part: after flashing, replug USB-C and run rpiboot again. Edit `config.txt` on **both** 64 MB boot partitions (the 16 MB one only holds `autoboot.txt`). Comment out `dtoverlay=spi2-2cs-pi5`, `spi3-1cs`, `spi1-2cs` and the five `dtoverlay=sc-mcp2518-can*` lines, then add `dtoverlay=ov9281,cam0` at the end. A finished copy is in [`p1-files/config.txt.p1`](../p1-files/config.txt.p1).

## Other notes

- **Re-flashing PhotonVision:** the SystemCore A/B layout uses the whole eMMC. Luma's image may not expand to the full 16 GB on its own; check the partition size afterwards.
- **Clock:** the P1 has no real-time clock, so the date is wrong (and logs `getSystemTimeValid` errors) until the Driver Station connects.

## References

- Luma P1 docs: https://docs.luma.vision/p1/
- SystemCore OS releases: https://github.com/LimelightVision/systemcore-os-public/releases
- SystemcoreTesting (CTRE section): https://github.com/wpilibsuite/SystemCoreTesting/blob/main/CTR-Phoenix.md
- WPILib SystemCore docs: https://docs.wpilib.org/en/latest/docs/software/systemcore-info/index.html
- Pi 5 SystemCore port (where the vcan approach comes from): https://github.com/netarcx/systemcore-rpi5-guide
- SystemCore image internals: https://dunkirk.sh/blog/frc-systemcore-image/
