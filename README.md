# Luma P1 → SystemCore bench robot

How we turned a **Luma P1** AprilTag camera into a bench **SystemCore** (FRC 2027 control system), using a **CTRE CANivore** as the only CAN bus and no radio. At the end, the FIRST Driver Station drives a Talon FX flywheel from an Xbox controller using the workshop `mech-4-ReadingState` lesson. The **P1's own camera runs in SystemCore's built-in Limelight vision**, so AprilTags, color pipelines and the rest of the Limelight UI work at the same time.

**Short version:** flash SystemCore (step 4), copy [`p1-files/`](p1-files/) to the P1, and run `sudo sh install.sh` (step 6).

> ⚠️ This is unofficial and for bench/learning use only. It is not a legal competition controller.

Tested 2026-11-17 with SystemCore OS **beta18**, WPILib **2027 alpha-7**, Phoenix 6 **26.70.0-alpha-2**, device firmware **26.70.0.0**.

---

## 1. What the P1 is, and what it's missing

| | Luma P1 | Real SystemCore |
|---|---|---|
| Compute | Raspberry Pi CM5, **2 GB RAM**, 16 GB eMMC | CM5, 4 GB RAM, 16 GB eMMC |
| CAN | none (we use a CANivore over USB) | 5× MCP2518FD CAN FD chips on SPI |
| I/O chip | none | RP2350 (DIO/PWM/analog/LEDs) |
| Camera | OV9281 global shutter on the CSI port (cam0) | USB cameras |
| Ports | Ethernet (passive PoE), Weidmuller power in, **one USB-A**, USB-C (flash only) | several USB, USB-C to DS, Wi-Fi AP |
| Power | 3.3–22.5 V via PoE or Weidmuller | battery |

What that means:
- Robot code, NetworkTables, Driver Station, CAN through the CANivore, and Limelight vision on the onboard camera all work.
- DIO/PWM/analog/onboard IMU don't exist. Harmless faults about them show up.
- The only USB port is taken by the CANivore.

## 2. Hardware you need

- **Luma P1**
- **Power:** a *passive* PoE injector, **or** 12 V into the Weidmuller connector. The P1 does **not** run normally on USB-C; USB-C only puts it into flash mode.
  - **What we use:** [iCreatin 12V passive PoE injector + splitter kit](https://www.amazon.com/dp/B00YGLCQQK) (ASIN B00YGLCQQK), which comes with a 12 V power supply.
    - Use only the **injector**: 12 V supply → injector → Ethernet cable → P1. The P1 has PoE built in, so **leave the splitter out**.
    - The injector's other port goes to your laptop or router.
    - It's passive 12 V, not 802.3af/at, which is what the P1 needs (the P1 accepts 3.3–22.5 V).
    - The kit is rated 12 W. That's plenty for the P1 plus the CANivore's USB power on the bench.
    - The link runs at **100 Mbps**, because passive kits carry power on the spare pairs. That's fine for robot code and the Driver Station.
  - Don't use an *active* 802.3af/at injector. It only powers devices that ask for it, and the P1's PoE is passive.
- **Ethernet cable**, 24 AWG (not "slim").
- **CTRE CANivore** + USB-C→USB-A cable, plugged into the P1's USB-A port.
- **CAN devices** with their own 12 V power (we used one Talon FX on a flywheel) and bus termination (the CANivore's built-in termination is on).
- **Xbox controller** for the Driver Station.
- **No radio needed.**

## 3. Software on the laptop (Windows)

- **rpiboot** for CM4/CM5: https://github.com/raspberrypi/usbboot
- **balenaEtcher**: https://etcher.balena.io/
- **WPILib 2027 alpha-7** (installed at `C:\Users\Public\wpilib\2027_alpha7`)
- **FIRST Driver Station** (2027)
- **Phoenix Tuner X _Preview_** (Microsoft Store, 2026.10.x). The regular 2026 Tuner X can't talk to the 2027 alpha diagnostic server.

---

## 4. Flash SystemCore onto the P1

1. Download the CM5 full image from Limelight's releases, https://github.com/LimelightVision/systemcore-os-public/releases:
   `limelightsystemcorebetacm5-limelightosr-2027.0.0-beta18.zip` (854 MB). Use the plain `cm5` zip, not the `-usb-` variant and not the `.llupdate`. Unzip it to get `limelightsystemcorebetacm5.img` (2.3 GB).
2. With the P1 **unpowered**, plug USB-C from the laptop into the P1. Windows shows a **"BCM2712D0 Boot"** device.
3. **Run rpiboot before opening Etcher.** Until it runs, the P1 is only a "BCM2712D0 Boot" device and **Etcher won't list it**. rpiboot loads a small program that exposes the eMMC as a USB drive:
   ```bash
   cd "C:/Program Files (x86)/Raspberry Pi" && ./rpiboot.exe -d mass-storage-gadget64
   ```
   A disk named **mmcblk0** (about 14.6 GB) appears. If you unplug or power-cycle the P1, run rpiboot again.
4. In balenaEtcher: **Flash from file** → the `.img`, **Select target** → **mmcblk0** (check the size so you don't pick your laptop drive!) → **Flash**. Wait for **"Flash Completed!"**. If Windows offers to format the drive, click **Cancel**.

Then unplug USB-C. Etcher ejects the drive when it's done, and the installer in step 6 patches `config.txt` for you. If you'd rather do it by hand now, see [Patching `config.txt` by hand](#patching-configtxt-by-hand).

## 5. First boot and network (no radio)

1. **Unplug USB-C**, then power the P1 through PoE or the Weidmuller input.
2. Connect Ethernet, either way:
   - **Direct to laptop:** both sides self-assign `169.254.x.x` addresses, and `robot.local` resolves through mDNS. Ours was `169.254.109.79`. Leave the laptop on automatic until the kit is installed; see [Laptop address](#laptop-address-for-the-h264-stream) for the H.264 stream afterwards.
   - **Home router:** the P1 gets an address from the router (DHCP).
3. Open **http://robot.local**. The dashboard login is `systemcore` / `systemcore`.
4. **Set the team number** on the dashboard (we used **5712**). It applies live; no power cycle needed.

### SSH access (optional, handy)
```bash
ssh-keygen -t ed25519 -f ~/.ssh/p1_systemcore -N ""
```
```powershell
Get-Content $env:USERPROFILE\.ssh\p1_systemcore.pub | ssh systemcore@robot.local "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"
```
The password is `systemcore`. Nothing shows while you type it. The `systemcore` user has passwordless `sudo`. There's also a web terminal at http://robot.local:4901.

## 6. Run the P1 kit installer

[`p1-files/`](p1-files/) is a self-contained kit. `install.sh` runs on the P1, is safe to re-run, and does five things:

| Part | What it does | Why |
|---|---|---|
| `config` | In `config.txt` on **both** boot partitions: comments out the 8 SPI CAN-chip overlays the P1 doesn't have (`#P1-no-hw# `) and adds `dtoverlay=ov9281,cam0`. Backs up the original as `config.txt.pre-p1kit`. | The P1 has no MCP2518 chips; the camera overlay is the same line Luma uses in its own image |
| `can` | Creates virtual `can_s0`–`can_s4` at boot and two systemd drop-ins | Without them, **`mrccomm` (Driver Station comms) crashes over and over** |
| `canivore` | Installs the CTRE `canivore-usb_*.ipk` if you put one in the folder | CANivore is the P1's only real CAN bus |
| `camera` | Makes the onboard OV9281 show up in SystemCore's Limelight vision as a USB camera | See [step 10](#10-limelight-vision-on-the-onboard-camera) |
| `net` | Keeps the FRC robot address **10.TE.AM.2/24** on `eth0` (from the dashboard team number), next to whatever DHCP or link-local gives it | The Limelight **H.264(UDP)** stream never uses link-local `169.254.x` addresses, so on a direct cable it silently falls back to MJPEG |

1. **Get the CANivore package.** CTRE only publishes it for image **17**: `canivore-usb_1.18_systemcoreos_alphabeta_17.ipk` (155 KB) from https://ctre.download/files/systemcore/. Its kernel module was built for `6.12.112-v8-16k` PREEMPT_RT, the same kernel as beta18, so it loads fine. Drop it into `p1-files/`.
2. **Copy the kit and run it** (password `systemcore` if you skipped the SSH key):
   ```bash
   scp -r p1-files systemcore@robot.local:/tmp/
   ```
   ```bash
   ssh -t systemcore@robot.local 'cd /tmp/p1-files && sudo sh install.sh'
   ```
3. If it says `config.txt changed: reboot to apply`, reboot:
   ```bash
   ssh systemcore@robot.local 'sudo reboot'
   ```

To run only one part: `sudo sh install.sh camera` (or `config`, `can`, `canivore`, `net`). To undo: `sudo sh uninstall.sh camera` (or `can`, `canivore`, `config`, `net`, `all`).

The installer refuses to run on a real SystemCore (it checks for MCP2518 CAN chips), because commenting out those overlays would disable its CAN buses.

**Check:**
- `ip -br link | grep can_s` lists 5 interfaces, and `systemctl is-active mrccomm` says `active`.
- `caniv -l` lists your CANivore. A `can0` interface also appears and stays DOWN until Phoenix opens it, which is normal.
- http://robot.local/cameras shows **Arducam OV9281 USB (USB 3)**.
- `ip -br addr show eth0` includes `10.TE.AM.2/24` (team 5712 → `10.57.12.2`).

**Name the CANivore `canivore`** so it matches the workshop code (`new CANBus("canivore")`). Ours shipped named `CANivor`:
```bash
ssh systemcore@robot.local 'caniv -a -n canivore && caniv -a -r && sleep 5 && caniv -l'
```

### Laptop address (for the H.264 stream)

On a direct cable, the laptop must also have an address in `10.TE.AM.0/24` for the camera's H.264(UDP) stream. That's the same address the Driver Station expects on a robot network. In an **admin** terminal (Windows, team 5712):
```bash
netsh interface ip add address "Ethernet" 10.57.12.5 255.255.255.0
```
⚠️ This switches the adapter from automatic to static, which **drops its `169.254.x` link-local address**. Without that, `robot.local` and the P1's `169.254.x` address stop working from the laptop until you either:
- use `10.57.12.2` instead of `robot.local`, or
- add a link-local address back as a second one:
  ```bash
  netsh interface ip add address "Ethernet" 169.254.10.10 255.255.0.0
  ```

So run the installer **before** changing the laptop's address (the installer is reached over link-local). To go back to automatic:
```bash
netsh interface ip set address "Ethernet" dhcp
```
On a home router or an FRC radio, both sides get normal DHCP addresses and none of this is needed.

## 7. ⚠️ Upgrade the CTRE motor controller and CANivore firmware to 26.70

The 2027 alpha Phoenix 6 (`26.70.0-alpha-2`) **requires device firmware 26.70.x**. With the older firmware, the Driver Station shows:

> Talon FX 31 ("canivore"): Device firmware could not be retrieved…
> Talon FX 31 ("canivore") Apply Configs: Firmware version is not compatible with this version of Phoenix.

and the motor won't run. Our devices started at:

| Device | Before | After |
|---|---|---|
| Talon FX "Shooter" (CAN ID 31) | 26.1.1.1 | **26.70.0.0** |
| CANivore `canivore` | 26.0.0 | **26.70.0.0** |

How to update:
1. **Deploy robot code that uses a Phoenix 6 device first** (step 8). Tuner X can't start its own diagnostic server on SystemCore, so the running robot program serves it on port **1250**.
2. Open **Tuner X _Preview_** and connect to **`robot.local`**. When it asks, sign in with **`systemcore` / `systemcore`**.
3. Field-upgrade **both the Talon FX and the CANivore** to the latest **26.70.x**. Don't cut power during a flash.
4. **Restart the robot code afterwards.** The flywheel applies its config **once at startup**, so a program started on old firmware keeps the failed config (zero Motion Magic values, so the motor never moves) until it restarts:
   ```bash
   ssh -i ~/.ssh/p1_systemcore systemcore@robot.local 'sudo systemctl restart robot'
   ```
   Redeploying does this too.

## 8. Workshop flywheel code

Repo: https://github.com/Hemlock5712/Workshop-Code. It's already on WPILib 2027 alpha-7 and Phoenix `26.70.0-alpha-2`. Branch **[`p1-flywheel`](https://github.com/Hemlock5712/Workshop-Code/tree/p1-flywheel)** is `mech-4-ReadingState` with these changes:
```bash
git clone -b p1-flywheel https://github.com/Hemlock5712/Workshop-Code.git
```

| Change | File | Why |
|---|---|---|
| `TalonFX(21, …)` → `TalonFX(31, …)` | `mechanisms/Flywheel.java` | our bench motor's ID is 31 |
| `kV 0.12`, `kP 0.1` | `mechanisms/Flywheel.java` | lesson gains are all 0, so nothing spins until tuned |
| **Motion Magic cruise velocity 100 rps** (was 0) | `mechanisms/Flywheel.java` | **with cruise velocity 0, Motion Magic holds the flywheel still** |
| Motion Magic acceleration 50 rps/s (was 0) | `mechanisms/Flywheel.java` | about 1.5 s ramp to 75 rps |
| Removed the Arm | `Robot.java`, `opmode/MyTeleop.java` | only the flywheel is plugged in |
| **`CommandNiDsXboxController` → `CommandXboxController`** | `Robot.java` | the NiDs mapping is only correct on the **NI** Driver Station; with the **FIRST** Driver Station the controller does nothing |

Build and deploy (team number comes from `.wpilib/wpilib_preferences.json`, 5712):
```bash
cd Workshop-Code && JAVA_HOME="C:/Users/Public/wpilib/2027_alpha7/jdk" ./gradlew deploy --offline
```
Or use **Deploy Robot Code** in WPILib 2027 alpha-7 VS Code. An `IllegalStateException: Executor has been shut down` printed after `BUILD SUCCESSFUL` is harmless.

> Note: the Motion Magic gains are applied in code at every startup, so they overwrite anything you set in Tuner X. Paste tuned values into `Flywheel.java`.

## 9. Drive it

1. FIRST Driver Station → team **5712**. Communications and Robot Code should both be green.
2. Xbox controller in USB **slot 0**.
3. Select the **Teleop** op mode and enable.

| Input | Flywheel |
|---|---|
| Teleop enabled, nothing pressed | ⚠️ spins up to **25 rps** right away |
| Right trigger held | 75 rps |
| A held / released | 75 rps / stop |

## 10. Limelight vision on the onboard camera

After the installer, the P1's OV9281 shows up at http://robot.local/cameras as **Arducam OV9281 USB (USB 3)** on port 0. Open http://robot.local/cam0 for the normal Limelight UI: pipelines, AprilTags, crosshair, NetworkTables as `limelightsc0`. It runs at about **69 fps** at 1280×800 greyscale, alongside robot code (about 1.4 GB RAM still free).

**Exposure and gain** in the Limelight UI work:
- Exposure is in **10 µs** steps, and Limelight caps it at **7.5 ms** for this camera profile.
- Gain uses the sensor's analog gain, 1× to about 16×.

**Stream:** the UI tries **H.264(UDP)** (WebRTC) first and silently falls back to **MJPEG** after about 6 s if the video doesn't connect. On a direct cable that happens unless both sides have a `10.TE.AM.x` address (the `net` part plus [Laptop address](#laptop-address-for-the-h264-stream)). The reason: in its WHEP answer, the vision server only offers non-link-local addresses, `172.26/27.x` (USB-C) and `10.TE.AM.2`, never `169.254.x`.

**Auto-exposure doesn't exist.** The sensor has no on-chip auto-exposure and there's no ISP. Set exposure by hand; that's what you want for AprilTags anyway.

### Starting settings

New pipelines can come up **completely black**: the defaults are tuned for a lit field, and the P1 has no LEDs. Set **Exposure** and **Sensor Gain** on the pipeline's *Input* tab, starting from:

| Situation | Exposure | Sensor Gain | Notes |
|---|---|---|---|
| **Just seeing the picture, dim room** | **750** (7.5 ms, the max) | **255** (max, ~16×) | Dim but recognizable in a dark room at night (tested). Noisy; fps can drop to ~40 on a fiducial pipeline. |
| Seeing the picture, normal room lights | 300 (3 ms) | 180–200 | Rough starting point; adjust until it looks right. |
| AprilTags | 100–400 (1–4 ms) | 128–200 | Light the tag. Shorter exposure means less motion blur; the image can look dark and still detect fine. |

How the numbers map to the sensor:
- **Exposure** is in 10 µs steps (`300` = 3 ms). Anything above about 750 is clamped to 7.5 ms.
- **Sensor Gain 128 is the minimum** (1×), not the middle; 255 is about 16×.

To set them without the UI (pipeline currently selected; `?flush=1` saves it to disk):
```bash
curl -X POST -H "Content-Type: application/json" -d '{"exposure":750.0,"cam_gain":255}' "http://robot.local:5807/update-pipeline?flush=1"
```

### How it works

SystemCore's Limelight vision server only accepts **USB** cameras, and only on four hardcoded USB port paths. The OV9281 is on CSI, and this image's libcamera can't drive it: the Raspberry Pi IPA crashes with "A list of V4L2 controls requires a ControlInfoMap". So the kit skips libcamera and makes the vision server believe a USB camera is plugged in. Nothing outside the vision server is changed:

1. **Raw CSI path.** `p1-llcam-setup.sh` routes the CSI receiver straight to `/dev/video0` as 8-bit `GREY`, skipping the Pi's ISP. The ISP front-end links have to be unlinked: the rp1-cfe driver won't start the sensor until every video node with an enabled link is streaming.
2. **Fake USB identity.** The setup script builds a small fake `/sys` tree in `/run/p1cam`. A systemd drop-in bind-mounts it over `/sys/class/video4linux` and the `usb3` hub, **only inside `limelight_visionserver`'s mount namespace**. The vision server then sees `/dev/video0` on USB port `3-1.1` with the name "Arducam OV9281 USB Camera", and picks its built-in OV9281 profile.
3. **`p1-v4l2-shim.so`**, loaded into the vision server with `LD_PRELOAD`:
   - **Format:** the CSI node accepts *any* pixel format and only fails when streaming starts. OpenCV picks BGR24 first, so every frame grab fails. The shim makes the node advertise GREY only.
   - **Controls:** the CSI node has no controls, so the shim emulates UVC ones. `exposure_time_absolute` and `gain` are forwarded to the sensor (`/dev/v4l-subdev2`). The rest (brightness, white balance, …) are stored, so the vision server's read-back check passes.
   - **`sudo v4l2-ctl`:** the vision server also shells out to `sudo v4l2-ctl`, and sudo drops `LD_PRELOAD`. Inside the namespace, `/usr/sbin/v4l2-ctl` is a wrapper that loads the shim again.

To rebuild the shim, use WPILib's SystemCore cross-compiler:
```bash
cd p1-files/camera && "C:/Users/Public/wpilib/2027_alpha7/systemcore/bin/aarch64-systemcore2027-linux-gnu-gcc.exe" -O2 -Wall -shared -fPIC -o p1-v4l2-shim.so p1-v4l2-shim.c -ldl
```
To log every control write to the journal, uncomment `P1SHIM_TRACE=1` in `camera/limelight_visionserver.service.d/p1-csicam.conf`.

### Patching `config.txt` by hand

The installer does this, but you can also do it from the laptop after step 4. Unplug and replug USB-C and run rpiboot again (Etcher ejected the drive). The boot partitions appear as drive letters: the 16 MB one holds `autoboot.txt`, and the two **64 MB** ones are **boot A** and **boot B**. Edit `config.txt` on **both** 64 MB partitions. A finished copy is in [`p1-files/config.txt.p1`](p1-files/config.txt.p1).
- Comment out the 8 SPI CAN lines: `dtoverlay=spi2-2cs-pi5`, `spi3-1cs`, `spi1-2cs`, and the five `dtoverlay=sc-mcp2518-can*` lines.
- Add at the end:
  ```
  # Luma P1: onboard OV9281 global-shutter camera on CAM0
  dtoverlay=ov9281,cam0
  ```

---

## Known issues and gotchas

- **SystemCore OTA updates (`.llupdate`) wipe the P1 fixes.** The update writes the *other* A/B root partition and boot files. Afterwards, re-run `sudo sh install.sh` (step 6) and reboot.
- **The camera fakes a USB device on port 0**, so the P1's camera is always `limelightsc0` / `/cam0`. If you also plug a real USB camera into the P1, it won't be on one of SystemCore's port paths, so it won't be picked up anyway.
- **Camera FOV / calibration:** the vision server uses its stock Arducam OV9281 USB profile. The P1's lens differs, so run Limelight's calibration before trusting 3D AprilTag poses.
- **A/B layout uses the whole eMMC:** two 7 GB root filesystems plus small boot partitions. If you flash the PhotonVision image back, it may **not** expand to the full 16 GB on its own. Check the partition size afterwards and grow it if needed.
- **Harmless log noise:**
  - **`getSystemTimeValid` errors every 5 s.** The P1 has no real-time clock, so the date stays wrong (e.g. Oct 2025) until the Driver Station connects and sets the time.
  - **One-time "loop overrun" / "Watchdog not fed" at startup.**
  - **iodaemon/RP2350 faults.** That chip doesn't exist on the P1.
- **The real CANivore** is `can0`. Don't confuse it with the `can_s*` / `can_d*` placeholders.
- **2 GB RAM:** fine for robot code plus vision (about 1.4 GB still available with the flywheel code and the camera running).

## Going back to PhotonVision

Flash Luma's PhotonVision image the same way as step 4 (rpiboot + Etcher, from https://docs.luma.vision/p1/flashing/). That replaces everything. Then check that the root partition expanded to fill the eMMC.

## References

- Luma P1 docs: https://docs.luma.vision/p1/
- SystemCore OS releases: https://github.com/LimelightVision/systemcore-os-public/releases
- SystemcoreTesting (CTRE section): https://github.com/wpilibsuite/SystemCoreTesting/blob/main/CTR-Phoenix.md
- WPILib SystemCore docs: https://docs.wpilib.org/en/latest/docs/software/systemcore-info/index.html
- Pi 5 SystemCore port (where the vcan approach comes from): https://github.com/netarcx/systemcore-rpi5-guide
- SystemCore image internals: https://dunkirk.sh/blog/frc-systemcore-image/
