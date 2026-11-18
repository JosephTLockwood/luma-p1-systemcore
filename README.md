# Luma P1 → SystemCore bench robot

Turn a **Luma P1** camera into a bench **SystemCore** (FRC 2027 robot controller). It drives CTRE motors through a **CANivore**, and the P1's own camera works in SystemCore's built-in Limelight vision.

> ⚠️ Unofficial, for bench/learning only. Not a legal competition controller.

Tested with SystemCore OS **beta18**, WPILib **2027 alpha-7**, Phoenix 6 **26.70.0-alpha-2**. Curious how it works? See [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md). You don't need it to follow the steps.

---

## What you need

**Hardware**
- Luma P1
- 12 V **passive** PoE injector. We use [this iCreatin kit](https://www.amazon.com/dp/B00YGLCQQK); use only the injector, not the splitter.
- Ethernet cable
- CTRE CANivore + USB cable (plugs into the P1's USB-A port)
- A CAN motor with its own 12 V power (we used a Talon FX on a flywheel)
- Xbox controller

**On the laptop (Windows)**
- [rpiboot](https://github.com/raspberrypi/usbboot/raw/master/win32/rpiboot_setup.exe) (installs to `C:\Program Files (x86)\Raspberry Pi`)
- [balenaEtcher](https://etcher.balena.io/)
- WPILib **2027 alpha-7**
- FIRST Driver Station (2027)
- Phoenix **Tuner X _Preview_** (Microsoft Store). The regular Tuner X won't work.

---

## Step 1: Flash SystemCore

1. Download [`limelightsystemcorebetacm5-limelightosr-2027.0.0-beta18.zip`](https://github.com/LimelightVision/systemcore-os-public/releases) (the plain `cm5` one, not `-usb-` or `.llupdate`). Unzip it to get `limelightsystemcorebetacm5.img`.
2. With the P1 **unpowered**, plug a USB-C cable from the laptop into the P1.
3. Open **PowerShell** and run rpiboot. Etcher can't see the P1 until you do this:
   ```powershell
   cd "C:\Program Files (x86)\Raspberry Pi"; .\rpiboot.exe -d mass-storage-gadget64
   ```
   Wait until it finishes. A drive called **mmcblk0** (about 14.6 GB) appears.
4. Open **balenaEtcher** → **Flash from file** → pick the `.img` → **Select target** → **mmcblk0** (check it's ~14.6 GB, not your laptop drive!) → **Flash**.
5. When it says **Flash Completed!**, unplug the USB-C. If Windows asks to format a drive, click **Cancel**.

## Step 2: Power on and connect

1. Plug in: 12 V supply → PoE injector → Ethernet cable → P1. Plug the injector's other port into the laptop.
2. Wait about 1 minute, then open **http://robot.local**. Log in with `systemcore` / `systemcore`.
3. Set your **team number** on the dashboard.

## Step 3: Install the P1 kit

1. Download this repo: **[Download ZIP](https://github.com/JosephTLockwood/luma-p1-systemcore/archive/refs/heads/main.zip)**, then right-click it → **Extract All**.
2. Download the CANivore driver **[canivore-usb_1.18_systemcoreos_alphabeta_17.ipk](https://ctre.download/files/systemcore/canivore-usb_1.18_systemcoreos_alphabeta_17.ipk)** and move it into the extracted **`p1-files`** folder.
3. Open **PowerShell** and go to the folder that **contains** `p1-files`. Windows extracts into a folder inside a folder, so it's usually:
   ```powershell
   cd "$env:USERPROFILE\Downloads\luma-p1-systemcore-main\luma-p1-systemcore-main"
   ```
   Type `dir`. You should see `p1-files` and `README.md`.
4. Copy the kit to the P1. When it asks for a password, type `systemcore` and press Enter. Nothing shows while you type, and you have about a minute before it gives up.
   ```powershell
   scp -r p1-files systemcore@robot.local:/tmp/
   ```
5. Run the installer on the P1 (same password):
   ```powershell
   ssh -t systemcore@robot.local 'cd /tmp/p1-files && sudo sh install.sh'
   ```
   It should end with `config.txt changed: reboot to apply`.
6. Reboot the P1:
   ```powershell
   ssh systemcore@robot.local 'sudo reboot'
   ```
   After the password, it should say `Connection to robot.local closed.` Wait about 1 minute.
7. Rename the CANivore to `canivore` (the robot code looks for that name):
   ```powershell
   ssh systemcore@robot.local 'caniv -a -n canivore && caniv -a -r && sleep 5 && caniv -l'
   ```
   The list at the end should show your CANivore named `canivore`.

✅ **Check:** http://robot.local/cameras shows **Arducam OV9281 USB (USB 3)**. That's the P1's camera.

## Step 4: Deploy the robot code

1. Download the flywheel code: **[Workshop-Code, branch `p1-flywheel`](https://github.com/Hemlock5712/Workshop-Code/archive/refs/heads/p1-flywheel.zip)**, then **Extract All**.
2. Open the extracted folder in **WPILib 2027 alpha-7 VS Code** (File → Open Folder).
3. Set your team number: `Ctrl+Shift+P` → **WPILib: Set Team Number**.
4. If your motor's CAN ID isn't **31**, change `TalonFX(31, …)` in `src/main/java/first/robot/mechanisms/Flywheel.java`.
5. `Ctrl+Shift+P` → **WPILib: Deploy Robot Code**. Wait for **BUILD SUCCESSFUL**.

## Step 5: Update CTRE firmware (once per device)

The 2027 code needs firmware **26.70.x** on the motor and the CANivore. If the Driver Station says *"Firmware version is not compatible"*, do this:

1. Make sure the robot code from Step 4 is running. Tuner X talks to the robot through it.
2. Open **Tuner X _Preview_** → connect to **`robot.local`** → sign in with `systemcore` / `systemcore`.
3. Update **both** the Talon FX and the CANivore to the newest **26.70.x** (under alpha-7 NOT 2026). Don't unplug power while it updates.
4. Restart the robot code:
   ```powershell
   ssh systemcore@robot.local 'sudo systemctl restart robot'
   ```

## Step 6: Drive it

1. Open the **FIRST Driver Station**, set your team number. **Communications** and **Robot Code** should turn green.
2. Plug the Xbox controller in. It must be in USB slot **0**.
3. Pick the **Teleop** op mode and **Enable**.

| Input | Flywheel |
|---|---|
| Enabled, nothing pressed | ⚠️ spins to **25 rps** right away |
| Hold right trigger | 75 rps |
| Hold A | 75 rps (stops when released) |

## Step 7: Use the camera

1. Open **http://robot.local/cam0**. It's the normal Limelight interface.
2. If the picture is **black**, go to the pipeline's **Input** tab and set:

   | You want to… | Exposure | Sensor Gain |
   |---|---|---|
   | just see the picture (dim room) | **750** | **255** |
   | see the picture (lights on) | **300** | **190** |
   | track AprilTags | **100–400** | **128–200** |

   Sensor Gain **128 is the lowest**, not the middle.
3. The stream may say **MJPEG** instead of **H.264(UDP)**. That's fine; it works the same.
4. For accurate 3D AprilTag positions, run Limelight's **calibration** first.

---

## If something goes wrong

| Problem | Fix |
|---|---|
| Etcher doesn't show the P1 | Run rpiboot (Step 1.3) first, every time you plug the USB-C in. |
| `Connection closed` right after the password | You were too slow typing it. Run the command again. |
| `WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED` (after re-flashing) | Run `ssh-keygen -R robot.local`, then try again. |
| Installer says `no canivore-usb_*.ipk` | The `.ipk` wasn't in `p1-files`. Do Step 3.2, then repeat Step 3.4 and 3.5. |
| Camera not on `/cameras` | Did you reboot (Step 3.6)? The camera only starts after a reboot. |
| Camera picture is black | Step 7.2. |
| Motor doesn't move | Check firmware (Step 5) and the CAN ID (Step 4.4). |
| Errors about time, RP2350, IMU, DIO or loop overruns | Normal on a P1. Ignore them. |
| You installed a SystemCore update (`.llupdate`) | It wipes the kit. Repeat Step 3. |

**To undo the kit:**
```powershell
ssh -t systemcore@robot.local 'cd /tmp/p1-files && sudo sh uninstall.sh all'
```
(Copy `p1-files` again first if you've rebooted, since `/tmp` is cleared on reboot.)

**To go back to PhotonVision:** flash Luma's image the same way as Step 1: https://docs.luma.vision/p1/flashing/
