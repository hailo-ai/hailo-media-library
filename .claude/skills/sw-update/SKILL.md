---
name: sw-update
description: Flash a Hailo-15 SBC (H15L → eMMC, H15H → SPI flash + SD card) with a new Vision Processor SW Package (e.g. upgrade to 1.11.0). Use when the user says "update the board firmware", "flash the SBC", "reprogram eMMC/SPI flash", or "upgrade to release X". Picks the path automatically. **Default is OTA** (OS Guide §§8.1–8.7): purely host-driven, no DIP switches, no buttons, no UART — ~6 min on H15L. **Fallback is UART recovery** (quick-start guide §2.3), hardware-in-the-loop with DIP flips, button presses, and U-Boot menu — used only when OTA isn't viable (board unreachable, OTA script absent, partition layout broken, or OTA fails mid-flash). Requires the user to have already downloaded the matching Vision Processor SW Package from the Developer Zone.
tools: Bash, Read, Write, Edit
---

# /sw-update — flash a Hailo-15 SBC with a new SW image

End-to-end driver for the SBC software image update. **Two paths, picked automatically by the Phase 0.4 probe:** Path A — OTA (DEFAULT, OS Guide §§8.1–8.7, board pulls the `.swu` over TFTP and applies it in-place) and Path B — UART recovery (FALLBACK, quick-start guides §2.3), used only when the OTA preflight probe fails or Path A flashes but the board never comes back.

**Hardware-damage warning: flashing the wrong board's package will brick the SBC** — the Phase 0.2 target guard runs before either path. The UART path additionally has wiring damage risks on H15H (see Phase B4).

## Board parameter matrix

Determine `$BOARD` ∈ {`h15l`, `h15h`} in Phase 0, then use that column wherever a step references the matrix. Hardware specifics (DIP positions, wiring, U-Boot menu, program commands) are spelled out per-board inside the Path B phases — they are not duplicated here.

| Parameter | **H15L** | **H15H** |
|---|---|---|
| Boot storage / image target | eMMC / eMMC | **SPI flash / SD card** |
| Package filename token | `hailo15l` | `hailo15h` |
| **Board ID (reliable, version-independent):** `/sys/devices/soc0/machine` | `Hailo-15l` | `Hailo-15` |
| os-release `NAME` (varies by release — **do NOT use for board detection**) | 1.11.0: `Hailo15l` • 1.10.1: `HAILO Hailo-15` • etc. | 1.11.0: `Hailo15` • older releases vary |
| **OTA `.swu` filename** | `hailo-update-image-hailo15l-sbc.swu` | `hailo-update-image-hailo15-sbc.swu` |
| --- | --- | --- |
| **(Path B only — package files, consumed by 0.2 + B2)** | | |
| Recovery FW file | `hailo15l_uart_recovery_fw.bin` | `hailo15_uart_recovery_fw.bin` |
| SCU bins | `hailo15l_scu_bl.bin`, `hailo15l_scu_fw.bin` | `hailo15_scu_bl.bin`, `hailo15_scu_fw.bin` |
| Shared program bins | `scu_bl_cfg_a.bin`, `u-boot.dtb.signed`, `u-boot-spl.bin`, `u-boot-initial-env`, `customer_certificate.bin`, `u-boot-tfa.itb` | *(same names)* |
| swupdate payload (Path B U-Boot pulls) | `fitImage`, `swupdate-image-hailo15l-sbc.ext4.gz`, `hailo-update-image-hailo15l-sbc.swu` | `fitImage`, `swupdate-image-hailo15-sbc.ext4.gz`, `hailo-update-image-hailo15-sbc.swu` |

## Inputs to ask the user

- **Board variant** (required): `h15l` or `h15h`. Ask, or infer as in Phase 0.1 (board's machine string / package filename token).
- **Package path** (required): absolute path to `hailo_vision_processor_sw_package_<version>_<token>*.tar.gz` (token = `hailo15l` or `hailo15h`), or to a directory where it's **already extracted** (contains `tools/` + `prebuilt/sbc/`). A tarball extracts to `<package-dir>/sw-update-work/`; an already-extracted dir is used in place.

> Automated package download (Developer Zone auth) is out of scope. The user supplies the package path.

---

# Phase 0 — Preflight & path selection

Runs in both paths.

### 0.1 Determine `$BOARD`

Ask, or infer from the package filename token / a reachable board's machine string:
```bash
sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no -o ConnectTimeout=3 \
  root@10.0.0.1 'cat /sys/devices/soc0/machine'   # returns Hailo-15l → h15l, Hailo-15 → h15h
```
**Don't** infer from os-release `NAME=` — that value changes between releases. Everything below follows the matrix column for this value.

### 0.2 Validate the package — and **guard the board target**

**Target guard (do this first):** the package token must match `$BOARD`. Flashing an `hailo15h` package on an H15L (or vice-versa) bricks the board. Abort loudly on a mismatch:

```bash
# for $BOARD=h15l, the package must NOT be hailo15h (and vice versa)
case "<package-path>" in *hailo15h*) [ "$BOARD" = h15l ] && { echo "FATAL: hailo15h package on an H15L flow — would brick. ABORT."; exit 1; };; esac
case "<package-path>" in *hailo15l*) [ "$BOARD" = h15h ] && { echo "FATAL: hailo15l package on an H15H flow — would brick. ABORT."; exit 1; };; esac
```

**Pre-extracted packages:** if `<package-dir>` already contains `tools/` + `prebuilt/sbc/`, **skip `tar xzf`** and use it as `<work-dir>`. Otherwise:

```bash
tar tzf <package-path> | head -30
mkdir -p <package-dir>/sw-update-work && tar xzf <package-path> -C <package-dir>/sw-update-work
```

Confirm `prebuilt/sbc/` contains, **using the `$BOARD` filenames from the matrix**: the `.swu` file (required for Path A); for Path B also the recovery FW, the two SCU bins, the six shared program bins, and the `fitImage` / `swupdate-image-*.ext4.gz`. Plus `tools/hailo15_board_tools-*-py3-none-any.whl` (Path B only).

### 0.3 Check current board state

```bash
sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no -o ConnectTimeout=3 \
  root@10.0.0.1 'grep "^VERSION=" /etc/os-release; cat /sys/devices/soc0/machine' 2>/dev/null
```

- Use `VERSION=` for the version comparison (present on every release).
- Use `/sys/devices/soc0/machine` (not `NAME=`) to confirm board variant.
- If `VERSION` matches the target: report it and **ask whether to reflash anyway**. Same-version OTA is still a real flash (components are written, fw_env updated, rootfs resized) — useful for recovery-from-corruption scenarios. If the user declines, exit.
- If `VERSION` is older than the target → upgrade path. If newer than the target → downgrade path (works the same way; ~5 min). OTA is version-agnostic — both directions go through the same Path A.

### 0.4 **OTA viability probe (PATH SELECTOR)**

Run these three probes in order. They decide whether to take Path A or Path B.

```bash
sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no -o ConnectTimeout=5 \
  root@10.0.0.1 '
    echo "PROBE_SSH_OK"
    [ -x /etc/run_swupdate.sh ] && echo "PROBE_OTA_SCRIPT_OK" || echo "PROBE_OTA_SCRIPT_MISSING"
    # Single-image layout = one boot partition + one rootfs partition on the boot device.
    # H15L: mmcblk1p1 (64M) + mmcblk1p2 (rootfs).  H15H: same idea on the boot device.
    parts=$(grep -E "mmcblk[01]p[0-9]" /proc/partitions | awk "{print \$4}" | sort | tr "\n" "," )
    echo "PROBE_PARTS=$parts"
  ' 2>&1
```

**Branch logic:**

- `PROBE_SSH_OK` absent → board unreachable. **Path B (UART recovery).**
- `PROBE_OTA_SCRIPT_MISSING` → board is on a pre-OTA firmware. **Path B.**
- Partitions are NOT a clean single-image layout — i.e. blank / A/B-initialized / corrupt → **Path B.** A clean single-image layout = exactly one boot partition + one rootfs partition on the boot device. **Match the shape, not a hardcoded device index.** Both `mmcblk1p1,mmcblk1p2` and `mmcblk0p1,mmcblk0p2` are valid — the mmcblk number is just enumeration order and is NOT board-fixed (an H15H can legitimately probe as `mmcblk1`; hardcoding per-board indexes wrongly routes healthy boards to Path B).
- All three OK → **Path A (default, jump to Phase A1).**

If the user explicitly asks for UART recovery despite a passing probe (e.g. they want to validate the recovery procedure), honour it — go to Path B.

### 0.5 `sudo` constraint

> **`sudo` runs in the USER's terminal, not the skill's.** Commands the skill issues over its own non-interactive shell have no TTY/askpass, so `sudo` fails silently. Path A's only `sudo` need is the one-time tftpd-hpa install (and only if not already installed). Path B's `watch chmod` and `picocom` must be run by the user in their own terminal.

---

# Path A — OTA flash (DEFAULT)

OS Guide §§8.1–8.7. Purely host-driven, no DIP/buttons/UART. ~6 min total on H15L over gigabit. The `run_swupdate.sh` script on the board sets the SCU bootloader to `remote_update` mode and reboots; the update image then pulls the `.swu` over TFTP, applies it, and reboots back into the new system. Update logs stream over UDP to a port on the host (default 12345).

### A1 — Stage the `.swu` in the TFTP root + byte-verify

```bash
sudo apt-get install -y tftpd-hpa sshpass    # idempotent; skip if both present
sudo tee /etc/default/tftpd-hpa <<'EOF'
TFTP_USERNAME="tftp"
TFTP_DIRECTORY="/var/lib/tftpboot"
TFTP_ADDRESS="0.0.0.0:69"
TFTP_OPTIONS="--secure"
EOF
sudo mkdir -p /var/lib/tftpboot && sudo chmod -R 777 /var/lib/tftpboot
sudo systemctl restart tftpd-hpa

# Copy the .swu using the matrix filename for $BOARD, then verify byte-equality.
SWU=$(case $BOARD in h15l) echo hailo-update-image-hailo15l-sbc.swu;; h15h) echo hailo-update-image-hailo15-sbc.swu;; esac)
cp -v "<work-dir>/prebuilt/sbc/$SWU" /var/lib/tftpboot/
[ "$(sha256sum < /var/lib/tftpboot/$SWU)" = "$(sha256sum < <work-dir>/prebuilt/sbc/$SWU)" ] \
  && echo "  .swu sha256 MATCH" || { echo "  .swu sha256 MISMATCH — abort"; exit 1; }

systemctl is-active tftpd-hpa   # must print "active"
ss -lunp | grep ':69 '          # must show a listener on UDP 69
```

> Path A does **not** need `fitImage` or `swupdate-image-*.ext4.gz` in tftpboot — those are Path B's U-Boot pulls. Just the `.swu`.

### A2 — Host static IP 10.0.0.2/24

Only if `ip -br addr | grep 10.0.0.2` returns nothing. Same as `/connect`:

```bash
nmcli -t -f NAME,TYPE connection show | grep ethernet
nmcli connection modify "<name>" ipv4.method manual ipv4.addresses 10.0.0.2/24 ipv4.gateway ""
nmcli connection up "<name>"
```

### A3 — Start the UDP log listener (background)

The board streams swupdate's TFTP-progress + install/error lines to UDP 12345 during the post-reboot update phase (port customisable via `-p` to `run_swupdate.sh`).

```bash
nc -u -l -k 12345 > /tmp/swupdate_logs.txt &
LOG_PID=$!
```

Watch the file (`tail -f /tmp/swupdate_logs.txt`) while Phase A4 runs — that's the only window into what the update image is doing while SSH is down.

### A4 — Kick off the update

**Announce the flash with the literal phrase "ETA is ~X minutes", on its own prominent line** — e.g.:

> **Starting the SW update to \<version\>. ETA is ~7 minutes.**

Rules for the announcement:
- Use exactly **"ETA is ~X minutes"** — not paraphrases like "the board will be unreachable for ~X minutes".
- **Repeat the ETA line in the post-kickoff status summary** — that's the message the user actually reads; an ETA that only appears before the tool call is easy to miss.
- No confirmation pause needed — announce and proceed in the same turn. If the estimate changes mid-flight (e.g. slow TFTP), report the revised ETA immediately.

ETA values: ~6 min on H15L gigabit; ~7 min on H15H (larger multi-mode flash). Path B/UART runs longer — call it ~10–15 min plus the manual steps.

```bash
sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no \
  root@10.0.0.1 "/etc/run_swupdate.sh -b -r $SWU -s 10.0.0.2"
```

> **`-b` (batch mode) is MANDATORY** — without it the script blocks forever on an interactive yes/no prompt, flooding stdout. There is no `--yes` flag — only `-b`.

The script returns within ~1 second after setting U-Boot env (`swupdate_server_ip`) and calling `set_sw_image.sh remote_update`. Then the board reboots — SSH drops.

### A5 — Poll for the board to come back

The board: reboots → boots into update image → TFTP-downloads the `.swu` (~670 MB H15L = ~3 min on gigabit) → swupdate applies → reboots back into normal mode. Total ~6 min.

```bash
ssh-keygen -R 10.0.0.1 2>/dev/null   # post-flash host key resets
until sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o PreferredAuthentications=password -o PubkeyAuthentication=no -o ConnectTimeout=3 \
        root@10.0.0.1 'cat /etc/os-release; echo OFFSET=$(cat /sys/devices/soc0/boot_info/active_boot_image_offset); echo UPTIME=$(awk "{print int(\$1)}" /proc/uptime)' 2>/dev/null
do echo "[$(date +%H:%M:%S)] waiting…"; sleep 8; done
echo "BOARD BACK"
```

Healthy progress markers in `/tmp/swupdate_logs.txt` along the way:
- `Downloading <swu> from 10.0.0.2 via TFTP...` followed by `0%`…`100%` progress bars
- `Running: swupdate with mode init-partitions-single`
- `[INFO ] : SWUPDATE successful ! SWUPDATE successful !`
- `SWUpdate finished` → `Rebooting...`

> **H15H runs THREE swupdate modes in sequence, not one**: `init-partitions-single` → `init-scu-bl` → `copy-a` (the last writes the ~650 MB rootfs to slot A). Each mode emits its OWN `SWUPDATE successful ! / SWUpdate was successful !`, so **the first success is NOT "done"** — wait for `SWUpdate finished` → `Rebooting...` after the `copy-a` mode. (H15L is single-mode: just `init-partitions-single`.)
>
> **On H15H, each mode opens with a scary-but-BENIGN line — do NOT treat it as failure:**
> ```
> [ERROR] : SWUPDATE failed [0] ERROR mtd-interface.c : scan_ubi_partitions : 392 : cannot attach mtd0 - maybe not a NAND or raw device
> ```
> It's a UBI/NAND probe failing because H15H boots from SPI flash + SD card (no NAND/`mtd0` UBI). swupdate logs it, falls through to the correct handler, and the same mode still ends `SWUPDATE successful`. **Only treat the flash as failed if a mode ends WITHOUT a following `SWUPDATE successful`.** If you arm a Monitor grep for failures, exclude this exact line or you'll abort a healthy flash.

Lua trace lines about a missing `swupdate_handlers` module are harmless noise — swupdate falls through to native handlers.

### A6 — Verify

```bash
sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no root@10.0.0.1 '
    grep "^VERSION=" /etc/os-release
    cat /sys/devices/soc0/machine
    cat /sys/devices/soc0/boot_info/active_boot_image_offset
    uptime
    head -10 /etc/build-info'
```

Report:
- `VERSION=` matches target (use **`^VERSION=`**, not `VERSION_ID`).
- `/sys/devices/soc0/machine` = `Hailo-15l` / `Hailo-15` (matches `$BOARD`).
- `active_boot_image_offset = 0x00000000` for single-image (slot A).
- `uptime` shows a recent boot.
- `BUILD_TIME` / `IMAGE_NAME` in `/etc/build-info` confirms the fresh rootfs.
- `IMAGE_NAME` also signals whether this is a **dev image** vs a release build — drives the camera-viewer auto-offer in the final "Hailo Camera Viewer" section. Release rootfs recipe is `core-image-minimal`; a dev image carries a `dev` token (e.g. `core-image-dev-…` / `hailo-core-image-dev-…`).
- Board boots autonomously on subsequent power cycles.

### A7 — Cleanup

```bash
kill $LOG_PID 2>/dev/null   # stop the UDP log listener
```

Leave the `.swu` in `/var/lib/tftpboot/` (useful for re-flashes; tftpd-hpa stays running idle).

### A8 — If Path A FAILED

Failure modes and remediation:

- **Script returned non-zero / SSH command fails before reboot** → check `Failed to set swupdate_server_ip in U-Boot environment` or `/etc/fw_env.config` issues in the script output. Often just a path/permissions issue on the board, not a flash failure. Retry once.
- **TFTP progress stalls at 0% or never starts** → host firewall (`sudo ufw status`; if active `sudo ufw allow 69/udp`), `tftpd-hpa` not actually serving (`systemctl status tftpd-hpa`), `/var/lib/tftpboot` perms (`ls -la`), or `.swu` not at the path the script requested.
- **TFTP completes but swupdate errors** (e.g. `SWUPDATE failed`, `corrupted image`, `signature verification failed`) → wrong-board `.swu` or a truncated `.swu` (re-verify SHA-256 against the source). The target guard in 0.2 should have caught wrong-board, but verify.
- **`SWUPDATE successful` but board never returns** (SSH poll keeps timing out past ~8 min) → the new image isn't booting — **fall back to Path B (UART recovery)** to reflash from cold. The OS Guide §8.3 says SCU eventually drops to UART recovery automatically after exhausting retries, but driving Path B is faster than waiting. Keep `/var/lib/tftpboot` populated; Phase B2 needs the fuller payload (`fitImage` + `.ext4.gz` + `.swu`). **Don't burn time on serial *diagnosis* first** — go straight to Path B *recovery* flashing a known-good release back.

If Path A succeeds, skip Path B entirely — A6's checks define success; continue at "Next step — Hailo Camera Viewer".

---

# Path B — UART recovery (FALLBACK)

Hardware-in-the-loop. Half host commands, half "please flip the DIP switch and press reset" — pause at each physical step and wait for a typed `done`. Use only when Phase 0.4 routed here, or Path A failed past the point of returning.

## Phase B1 — Host setup (one-time per host, idempotent)

1. **Install board tools.**
   ```bash
   pip install <work-dir>/tools/hailo15_board_tools-*-py3-none-any.whl
   sudo apt-get update && sudo apt-get install -y u-boot-tools tftpd-hpa picocom sshpass
   ```
   > Tools may already be installed in a **virtualenv**. Check `command -v uart_boot_fw_loader hailo15_emmc_program hailo15_spi_flash_program` first; if they resolve, skip the `pip install`.

2. **FTDI udev rule** — best-effort only; **the real permission fix is the Phase B3 watch-chmod**, because the doc's rule targets `SUBSYSTEM=="usb"`, not the `tty` node `ftdi_sio` actually creates. If absent, add it:
   ```bash
   echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="0403", ATTR{idProduct}=="6015", MODE="0666"' | sudo tee -a /etc/udev/rules.d/99-usb.rules
   sudo udevadm control --reload && sudo udevadm trigger && sudo adduser $USER plugdev
   ```
   > On **H15H** the adapter's FTDI may be `0403:6001`, which this `6015` rule won't match — another reason to rely on watch-chmod, not the rule.

## Phase B2 — TFTP server with FULL swupdate payload

(If Path A already set up tftpd-hpa: it's still running, just add the extra files.)

> **Use plain `cp` (never `sudo`), overwrite ALL THREE files every time, and verify content — not presence.** `/var/lib/tftpboot` is `777` (A1 set it); a `sudo`-prefixed copy **fails silently** in the skill's non-interactive shell (see 0.5) and leaves a STALE payload. And the names collide two ways — `fitImage` across *boards*, all three files across *versions* — so a prior flash can leave a different-version payload under the exact same canonical name. The loop below copies-then-compares, so it self-heals; as a backstop, re-run the `cmp` on the `.swu` just before driving the U-Boot menu in B7 (it's the file that actually gets flashed).

```bash
# tftpd-hpa already configured by Path A; if coming here directly, run A1's sudo tee block first.
for f in fitImage <swupdate-image-...ext4.gz> <hailo-update-image-...swu>; do
  cp -v "<work-dir>/prebuilt/sbc/$f" /var/lib/tftpboot/
  cmp -s "<work-dir>/prebuilt/sbc/$f" "/var/lib/tftpboot/$f" && echo "  MATCH $f" || echo "  DIFFER $f"
done
```

## Phase B3 — Permission keeper (HARD PAUSE, set up BEFORE any USB)

The FTDI re-enumerates several times during the flash (recovery FW load, programming, post-flash reboot). Each new `/dev/ttyUSB0` comes up `root:dialout 660`, and the doc's udev rule never grants tty perms. A one-shot `chmod` doesn't survive the next re-bind. A `watch` loop, started **now**, does.

Print verbatim and wait for `done`:

```
Open a SECOND terminal window NOW and run this, then LEAVE IT RUNNING until I tell you to stop (through the end of Phase B7):

  watch sudo chmod -R 777 /dev/ttyUSB0

It will print "No such file or directory" until the device appears — that's expected. (Default 2s interval is fine; do NOT shorten to -n 0.5.)

Type 'done' once it's running in its own terminal.
```

## Phase B4 — PHYSICAL: switch to UART boot mode (HARD PAUSE)

DIP for UART boot is `SW1: 1=ON, 2=OFF` on **both** boards. Wiring differs — print the `$BOARD` block verbatim and wait for `done`.

**H15L** (hailo15l guide §2.3.3):
```
PHYSICAL STEPS — H15L UART boot mode:
  1. Power OFF the SBC (long press PWR).
  2. If orange tape covers SW1, peel it off with tweezers.
  3. Set DIP SW1:  1 = ON,  2 = OFF.
  4. Connect the USB-to-micro-USB cable from this PC to the SBC's uUSB connector (J1) — NOT the USB-A 3.1 port.
  5. Press PWR to power on (short press).
     ← /dev/ttyUSB0 appears HERE, at power-on (the FTDI is on the SBC, powered by the board).
  6. Press RESET once.
  7. Two adjacent green LEDs should light up.
Type 'done' when complete.
```

**H15H** (hailo15h guide §2.3.2 / 2.3.2.1) — ⚠️ **damage risks in the wiring**:
```
PHYSICAL STEPS — H15H UART boot mode:
  1. Power OFF the SBC (long press PWR).
  2. If orange tape covers SW1, peel it off with tweezers.
  3. Set DIP SW1:  1 = ON,  2 = OFF.
  4. On the UART1 adapter board: set the voltage JUMPER to 1.8V.   ← WRONG VOLTAGE DAMAGES THE DEVICE
  5. Wire ONLY GND/Rx/Tx to the SBC J4 header — DO NOT connect Vcc. ← CONNECTING Vcc DAMAGES THE DEVICE
        J4 pin 14  -> GND                 (black)
        J4 pin 16  (SBC Rx) -> adapter Tx (green)
        J4 pin 18  (SBC Tx) -> adapter Rx (orange)   [cable colors may vary by kit]
  6. Plug the UART adapter board's USB cable into THIS host.
        ← /dev/ttyUSB0 appears when the ADAPTER's USB is connected — NOT at SBC power-on.
  7. Press PWR to power on, then press RESET once.
Type 'done' when complete.
```

**Wait for `/dev/ttyUSB0`:**
```bash
for i in {1..30}; do [ -e /dev/ttyUSB0 ] && break; sleep 1; done
ls -l /dev/ttyUSB0; lsusb | grep -iE '0403:6015|0403:6001|serial|uart'
```
If absent on **H15L**: DIP wrong, cable in the USB-A port instead of uUSB J1, or board not powered on. On **H15H**: the **adapter's USB isn't plugged into the host** (most common), adapter unpowered, or routed through a dock that doesn't pass it. A **swapped Rx/Tx** (pins 16↔18) gives a present `/dev/ttyUSB0` but a dead link — the loader will then fail to connect.

## Phase B5 — Load recovery firmware + program storage

> **DO NOT MODIFY these commands — run them raw.** Never pipe `uart_boot_fw_loader` / `hailo15_emmc_program` / `hailo15_spi_flash_program` through `tail`, `head`, or any line-buffered consumer: their progress lines are the only evidence the transfer is alive. For a copy, append `2>&1 | tee <logfile>` — never a truncating filter.
>
> **Hang/no-connect signatures:**
> - Silent, process in `D` state, `/dev/ttyUSB0` gone → FTDI lost its tty. Check `lsusb` + `/dev/ttyUSB0`; reset + replug rather than blind retry. Some LED flicker / brief ttyUSB0 disappearance mid-transfer is normal re-enumeration.
> - **`could not connect to the recovery agent`** → the boot ROM only listens on UART for a short window after reset. **Hard-pause, have the user press RESET, then re-run the loader immediately.** (Also verify DIP `1=ON, 2=OFF`, and on H15H that Rx/Tx aren't swapped.) Expected on the first try if time elapsed since the last reset. **To dodge it entirely: run the loader as the very next action after B4's power-on+RESET `done`** — don't interleave other host checks; if the loader runs within ~30s of the RESET it connects first try.

**Load recovery FW over UART** (note: the `--h15l` flag is H15L-only):
```bash
cd <work-dir>
# H15L:
uart_boot_fw_loader --serial-device-name /dev/ttyUSB0 --firmware ./prebuilt/sbc/hailo15l_uart_recovery_fw.bin --h15l
# H15H:
uart_boot_fw_loader --serial-device-name /dev/ttyUSB0 --firmware ./prebuilt/sbc/hailo15_uart_recovery_fw.bin
```
Success ends with "loaded successfully to the device" (H15H also prints `flash detected, flash jedec_id: …`).

**Program storage** — the long one, run from `prebuilt/sbc/`:
```bash
cd <work-dir>/prebuilt/sbc

# ---- H15L: program eMMC (note --uart-baud-rate 921600) ----
hailo15_emmc_program \
  --scu-bootloader ./hailo15l_scu_bl.bin --scu-bootloader-config ./scu_bl_cfg_a.bin \
  --scu-firmware ./hailo15l_scu_fw.bin --uboot-device-tree ./u-boot.dtb.signed \
  --bootloader ./u-boot-spl.bin --bootloader-env ./u-boot-initial-env \
  --customer-certificate ./customer_certificate.bin --uboot-tfa ./u-boot-tfa.itb \
  --serial-device-name /dev/ttyUSB0 --uart-baud-rate 921600

# ---- H15H: program SPI flash (note --uart-load, NO baud-rate arg) ----
hailo15_spi_flash_program \
  --scu-bootloader ./hailo15_scu_bl.bin --scu-bootloader-config ./scu_bl_cfg_a.bin \
  --scu-firmware ./hailo15_scu_fw.bin --uboot-device-tree ./u-boot.dtb.signed \
  --bootloader ./u-boot-spl.bin --bootloader-env ./u-boot-initial-env \
  --customer-certificate ./customer_certificate.bin --uboot-tfa ./u-boot-tfa.itb \
  --uart-load --serial-device-name /dev/ttyUSB0
```
Each image prints "Storage program validation passed successfully". H15L talks to the recovery FW at **921600**; the later picocom uses **115200** — don't confuse the two.

## Phase B6 — PHYSICAL: switch to normal boot mode (HARD PAUSE)

The DIP is the **opposite** between boards. Print the `$BOARD` block:

**H15L** (eMMC boot, §2.3.6):
```
  1. Power OFF the SBC (long press PWR).
  2. Set DIP SW1:  1 = ON,  2 = ON.     ← BOTH ON
  3. Connect the Ethernet RJ45 cable (SBC <-> host).
  4. Keep the micro-USB cable connected — we still need the serial console.
  5. Press PWR to power on, then RESET once.
Type 'done' when complete.
```

**H15H** (SPI-flash boot + SD card, §2.3.4 / §2.3.5.1):
```
  1. Power OFF the SBC (long press PWR).
  2. Set DIP SW1:  1 = OFF,  2 = OFF.   ← BOTH OFF
  3. Connect the Ethernet RJ45 cable (SBC <-> host).
  4. Keep the UART adapter board wired + its USB to the host — we still need the serial console.
  5. Confirm the micro-SD card is seated in the SD slot (the image flashes to the SD card).
  6. Press PWR to power on, then RESET once.
Type 'done' when complete.
```

## Phase B7 — Drive U-Boot menu + swupdate

**Open serial console at 115200** (separate terminal; the watch-chmod terminal is fine — leave that loop running):
```bash
sudo picocom -b 115200 /dev/ttyUSB0
```
Permission-denied should clear within ~2s from the watch-chmod loop. Garbled text → wrong baud (must be `115200`, not the `921600` from H15L programming).

**PAUSE — press RESET (MANDATORY).** Print verbatim and wait for `done`:
```
With picocom open, press the RESET button ONCE.
The "*** U-Boot Boot Menu ***" only appears AFTER this manual reset — it will NOT show on its own.
(Required on both H15L and H15H.)
Type 'done' once you see the U-Boot Boot Menu.
```

**PAUSE — drive the menu.** Pick the `$BOARD` option:
- **H15L:** highlight **"eMMC Board Init"**, ENTER.
- **H15H:** highlight **"SD Card Board Init"**, ENTER.

U-Boot TFTPs `fitImage` + the swupdate files from the host (`10.0.0.2`) and runs swupdate. Wait for:
```
  ... SWUpdate was successful !
  Rebooting...
  [N.NNNNNN] reboot: Restarting system
```
Failure modes:
- **Menu doesn't appear** → reset not pressed, or DIP wrong (H15L both ON / H15H both OFF).
- **TFTP timeout** → host firewall (`sudo ufw status`; if active `sudo ufw allow 69/udp`), 10.0.0.2 not bound, or `tftpd-hpa` down.
- **swupdate fails / wrong-board image** → confirm the TFTP `fitImage` + swupdate files are the `$BOARD` ones (the `fitImage` name collides); re-stage and restart from Phase B2.

## Phase B8 — Verify

**Poll for SSH — NOT ping.** During the "Board Init" flash the swupdate **rescue image** has networking and answers ping at `10.0.0.1` while the flash is still running (and again at each reboot), so a `ping` loop reports "back" minutes before the real OS is up. Poll until **SSH returns the version**; that's the only signal the flashed OS actually booted. Post-flash SSH also fails twice at first (host key + password-auth resets), so clear the host key before looping:
```bash
ssh-keygen -f ~/.ssh/known_hosts -R 10.0.0.1 2>/dev/null
for i in $(seq 1 180); do
  OUT=$(sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
          -o PreferredAuthentications=password -o PubkeyAuthentication=no -o ConnectTimeout=4 \
          root@10.0.0.1 'grep "^VERSION=" /etc/os-release; cat /sys/devices/soc0/machine; cat /sys/devices/soc0/boot_info/active_boot_image_offset; uptime' 2>/dev/null)
  [ -n "$OUT" ] && { echo "=== BOARD UP ==="; echo "$OUT"; break; }
  echo "[$(date +%H:%M:%S)] waiting for booted OS (SSH)…"; sleep 5
done
```
Report the new `VERSION` (matches target — **`VERSION`, not `VERSION_ID`**), machine (`Hailo-15l`/`Hailo-15`), and active boot offset (`0x00000000` for single-image). The changed host key is expected, not a security event. See `/connect`. Adjacent green LEDs staying lit through boot is the visual healthy-boot sign; the board should boot autonomously on subsequent power cycles.

**Cleanup.** Leave the work dir in place. Tell the user the picocom session can close (`Ctrl-A Ctrl-X`) and the **Phase B3 watch-chmod loop can now stop** (`Ctrl-C`).

---

## Next step — Hailo Camera Viewer (guide §3.1, same on both boards, same after either path)

`/sw-update` ends at the version check. What happens next depends on whether a **dev image** or a **release image** was installed.

### Detect dev vs release image

The normal release rootfs recipe is **`core-image-minimal`**; a **development image** carries a `dev` token in its name (e.g. `core-image-dev-hailo15-sbc-…` / `hailo-core-image-dev-…`). Read it from `/etc/build-info` (already fetched in A6/B8 — equivalently the rootfs `core-image-*.ext4.gz` filename inside the package):
```bash
sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no root@10.0.0.1 \
  'grep "^IMAGE_NAME" /etc/build-info' \
  | grep -iqE 'core-image-dev|(^|[-_])dev([-_]|$)' && echo "DEV_IMAGE" || echo "RELEASE_IMAGE"
```

### RELEASE image → do nothing automatic

The viewer is still available manually (commands below); only bring it up if the user asks.

### DEV image → SUGGEST the camera viewer (suggest only — NEVER run without explicit permission)

Ask the user, e.g.: *"This is a dev image — want me to start the Hailo Camera Viewer so you can see the camera live?"*

- **If the user agrees**, run the two on-board commands, then open the viewer on the host:
  ```bash
  SSH="sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o PreferredAuthentications=password -o PubkeyAuthentication=no root@10.0.0.1"
  # 1. sensor + lens auto-detect / config symlink — MANDATORY and BLOCKING (must finish before the server starts)
  $SSH 'setup_hailo_sensor.sh'
  # 2. start the web server DETACHED so it survives the ssh session
  #    (setsid + </dev/null — a bare nohup/& still leaks SIGHUP when the ssh session ends)
  $SSH 'setsid camera-viewer-server >/var/log/camera-viewer.log 2>&1 </dev/null &'
  # 3. open the viewer in the host browser
  xdg-open 'http://10.0.0.1/#/' >/dev/null 2>&1 &
  ```
  Then the user clicks **Play** (§3.2). Don't claim the server "auto-started" — you launched it; if you find it already running, ask/check `systemd` rather than assuming.
- **If the user declines**, stop here and leave the board idle.

**Why `setup_hailo_sensor.sh` is mandatory:** without it `camera-viewer-server` crashes with `Configuration architecture '<other>' does not match current architecture '<this board>'`, because the shipped default config is for the other arch. Don't hand-pick a sensor/lens config — let the setup script detect it. Config symlinks live at `/etc/imaging/cfg/medialib_configs` and do **NOT** survive a reflash, so `setup_hailo_sensor.sh` must be re-run on every fresh image.

## Gotchas

(Traps are documented inline at their point of use; only these aren't.)
- **Ethernet must be 10.0.0.0/24, no DHCP switch**: the SBC is static `10.0.0.1`, no DHCP client.
- **U-Boot/swupdate TFTP only reads `/var/lib/tftpboot`** (tftpd-hpa `--secure` chroots there).
- **A/B dual-image (`-d`) is NOT supported on H15L eMMC** (OS Guide §8.11); on H15H it's permitted by the doc but out of scope — this skill stays single-image, so the post-flash boot offset is always `0x00000000` (don't expect it to "flip").
- **§8.5 partition-init version-match constraint does NOT apply** to subsequent OTAs — only to the one-time partition init (already done in production; this skill doesn't redo it).

## What to delegate

- "The board still doesn't come up after flash" → `doc-explorer` on the recovery-mechanism section (H15L §2.3.2 / H15H §2.3.2) — the recovery procedure is Path B, so a persistent failure even after running Path B is likely hardware (storage, power, DIP, or H15H adapter wiring), not software.
