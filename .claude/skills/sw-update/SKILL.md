---
name: sw-update
description: Flash a Hailo-15 SBC (H15L → eMMC, H15H → SPI flash + SD card) with a new Vision Processor SW Package (e.g. upgrade to 1.11.0). Use when the user says "update the board firmware", "flash the SBC", "reprogram eMMC/SPI flash", or "upgrade to release X". Hardware-in-the-loop: the skill drives host-side commands and pauses for the user to perform DIP-switch flips, cable/UART-adapter wiring, button presses, and U-Boot menu selection. Requires the user to have already downloaded the matching Vision Processor SW Package from the Developer Zone.
tools: Bash, Read, Write, Edit
---

# /sw-update — flash a Hailo-15 SBC with a new SW image

End-to-end driver for the SBC software image update. **Two board variants, two procedures** — pick the right one in Phase 0 and follow that column throughout:

- **H15L** — guide `docs/guides/hailo15l_sbc_2.x_quick_start_guide_1.2.pdf` §2.3. Boots from / flashes to **eMMC**. Serial over a **micro-USB cable** to the SBC (FTDI on the board).
- **H15H** — guide `docs/guides/hailo15h_sbc_2.x_quick_start_guide_1.2.pdf` §2.3. Boots U-Boot from **SPI flash**, flashes the image to an **SD card**. Serial over a **UART1 adapter board** wired to the J4 header.

Half host commands, half "please flip the DIP switch and press reset" — the skill stops at each physical step and waits for a typed `done`. Both guides carry a **hardware-damage warning**: follow the steps in the exact order, do not skip or reorder. **Flashing the wrong board's package will brick the SBC** — the Phase 0 target guard exists for this.

## Board parameter matrix — the single source of truth

Determine `$BOARD` ∈ {`h15l`, `h15h`} in Phase 0, then read every phase down that column.

| Parameter | **H15L** | **H15H** |
|---|---|---|
| Guide section | hailo15l guide §2.3 | hailo15h guide §2.3 |
| Boot storage / image target | eMMC / eMMC | **SPI flash / SD card** |
| Package filename token | `hailo15l` | `hailo15h` |
| os-release `NAME` / machine | `Hailo15l` / `Hailo-15l` | `Hailo15` / `Hailo-15` |
| Serial link | micro-USB → SBC **J1** (FTDI `0403:6015` **on the SBC**) | **UART1 adapter board** → SBC **J4** pins 14/16/18 (FTDI **on the adapter**, e.g. `0403:6001`) |
| `/dev/ttyUSB0` appears | at **SBC power-on** | when the **adapter's USB** is plugged into the host (independent of SBC power) |
| DIP SW1 — UART boot | `1=ON, 2=OFF` | `1=ON, 2=OFF` *(same)* |
| DIP SW1 — normal boot | `1=ON, 2=ON` (both ON) | **`1=OFF, 2=OFF` (both OFF)** |
| Recovery FW file | `hailo15l_uart_recovery_fw.bin` | `hailo15_uart_recovery_fw.bin` |
| Recovery loader flag | **`--h15l`** | *(none)* |
| Program command | `hailo15_emmc_program … --uart-baud-rate 921600` | `hailo15_spi_flash_program … --uart-load` (**no baud arg**) |
| SCU bins | `hailo15l_scu_bl.bin`, `hailo15l_scu_fw.bin` | `hailo15_scu_bl.bin`, `hailo15_scu_fw.bin` |
| Shared program bins | `scu_bl_cfg_a.bin`, `u-boot.dtb.signed`, `u-boot-spl.bin`, `u-boot-initial-env`, `customer_certificate.bin`, `u-boot-tfa.itb` | *(same names)* |
| swupdate payload | `fitImage`, `swupdate-image-hailo15l-sbc.ext4.gz`, `hailo-update-image-hailo15l-sbc.swu` | `fitImage`, `swupdate-image-hailo15-sbc.ext4.gz`, `hailo-update-image-hailo15-sbc.swu` |
| U-Boot menu pick | **"eMMC Board Init"** | **"SD Card Board Init"** |

> Wherever a step shows a value, take it from the `$BOARD` column. The **swupdate `fitImage` filename collides** between boards (both just `fitImage`) — when restaging the TFTP root, always overwrite it from the correct package.

## Inputs to ask the user

- **Board variant** (required): `h15l` or `h15h`. If unsure, the os-release `NAME`/machine from a reachable board (Phase 0 step 1) or the package filename token disambiguates.
- **Package path** (required): absolute path to `hailo_vision_processor_sw_package_<version>_<token>*.tar.gz` (token = `hailo15l` or `hailo15h`), or to a directory where it's **already extracted** (contains `tools/` + `prebuilt/sbc/`, e.g. `~/hailo/h15l_1_11/` or `~/hailo/h15h_1_11/`). A tarball extracts to `<package-dir>/sw-update-work/`; an already-extracted dir is used in place. If the user doesn't specify, glob `~/hailo/<board>_*/...<token>*.tar.gz` and present matches.
- **Serial device** (optional): defaults to `/dev/ttyUSB0`.

> Automated package download (Developer Zone auth) is out of scope. The user supplies the package path.

## Phase 0 — Preflight

0. **Determine `$BOARD`.** Ask, or infer from the package token / a reachable board's machine string. Everything below follows the matrix column for this value.

1. **Check if the board already runs the target version.**
   ```bash
   ssh -o ConnectTimeout=3 -o StrictHostKeyChecking=no root@10.0.0.1 'cat /etc/os-release' 2>/dev/null | grep '^VERSION='
   ```
   > os-release has `NAME="Hailo15l"`/`"Hailo15"` and `VERSION="1.11.0"` — there is **no `VERSION_ID` field**. Grep `^VERSION=`; `VERSION_ID` silently matches nothing. `cat /sys/devices/soc0/machine` returns `Hailo-15l` / `Hailo-15` and confirms `$BOARD`.
   - If unreachable: assume blank/corrupt storage, continue (recovery is exactly what this skill does). It can also just mean the board is off, in UART mode already, or the host isn't on `10.0.0.0/24` yet — not blocking.
   - If `VERSION` matches the target: report it and **ask whether to reflash anyway** (recovery from a corrupted-but-booting board is legitimate). If the user declines, exit.

2. **Validate the package — and guard the board target.**

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
   Confirm `prebuilt/sbc/` contains, **using the `$BOARD` filenames from the matrix**: the recovery FW, the two SCU bins, the six shared program bins, and the three swupdate-payload files. Plus `tools/hailo15_board_tools-*-py3-none-any.whl`. If any are missing, abort — package looks incomplete.

3. **Host OS sanity check.** Verified on Ubuntu 22.04 LTS. Warn (don't abort) on other distros.

> **`sudo` runs in the USER's terminal, not the skill's.** Commands the skill issues over its own non-interactive shell have no TTY/askpass, so `sudo` fails silently. Any `sudo` step — especially `watch chmod` and `picocom` — must be run by the user in their own terminal. Print them; don't try to execute them yourself.

## Phase 1 — Host setup (one-time per host, idempotent)

4. **Install board tools.**
   ```bash
   pip install <work-dir>/tools/hailo15_board_tools-*-py3-none-any.whl
   sudo apt-get update && sudo apt-get install -y u-boot-tools tftpd-hpa picocom sshpass
   ```
   > Tools may already be in a **virtualenv** (on the reference host: `~/hailo/venv1`). Check `command -v uart_boot_fw_loader hailo15_emmc_program hailo15_spi_flash_program` first; if they resolve, skip the `pip install`.

5. **FTDI udev rule** — best-effort only; **the real permission fix is the Phase 3.5 watch-chmod**, because the doc's rule targets the wrong subsystem (see Gotchas). If absent, add it (H15L doc uses `MODE="0666"`, H15H doc uses `GROUP="plugdev", MODE="0664"` — either is fine):
   ```bash
   echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="0403", ATTR{idProduct}=="6015", MODE="0666"' | sudo tee -a /etc/udev/rules.d/99-usb.rules
   sudo udevadm control --reload && sudo udevadm trigger && sudo adduser $USER plugdev
   ```
   > On **H15H** the adapter's FTDI may be a different PID (e.g. `0403:6001`), which this `6015` rule won't match at all — another reason to rely on watch-chmod, not the rule.

## Phase 2 — TFTP server

6. **Configure tftpd-hpa.**
   ```bash
   sudo tee /etc/default/tftpd-hpa <<'EOF'
   TFTP_USERNAME="tftp"
   TFTP_DIRECTORY="/var/lib/tftpboot"
   TFTP_ADDRESS="0.0.0.0:69"
   TFTP_OPTIONS="--secure"
   EOF
   sudo mkdir -p /var/lib/tftpboot && sudo chmod -R 777 /var/lib/tftpboot
   sudo systemctl restart tftpd-hpa
   sudo systemctl status tftpd-hpa --no-pager | head -5   # confirm "active (running)"
   ```
   > Once `/var/lib/tftpboot` is `777`, the skill **can** copy payloads into it without `sudo`.

7. **Copy the `$BOARD` swupdate payload into the TFTP root, then byte-verify.** Use the matrix filenames; **overwrite `fitImage`** (the name collides across boards):
   ```bash
   for f in fitImage <swupdate-image-...ext4.gz> <hailo-update-image-...swu>; do
     cp -v "<work-dir>/prebuilt/sbc/$f" /var/lib/tftpboot/
     cmp -s "<work-dir>/prebuilt/sbc/$f" "/var/lib/tftpboot/$f" && echo "  MATCH $f" || echo "  DIFFER $f"
   done
   ```

## Phase 3 — Host network: 10.0.0.2/24

8. **Set host static IP** (only if `ip -br addr | grep 10.0.0.2` returns nothing). Same as `/connect`:
   ```bash
   nmcli -t -f NAME,TYPE connection show | grep ethernet
   nmcli connection modify "<name>" ipv4.method manual ipv4.addresses 10.0.0.2/24 ipv4.gateway ""
   nmcli connection up "<name>"
   ```

## Phase 3.5 — Permission keeper (HARD PAUSE, set up BEFORE any USB)

The FTDI re-enumerates several times during the flash (recovery FW load, programming, post-flash reboot). Each new `/dev/ttyUSB0` comes up `root:dialout 660`, and the doc's udev rule never grants tty perms. A one-shot `chmod` doesn't survive the next re-bind. A `watch` loop, started **now**, does.

Print verbatim and wait for `done`:

```
Open a SECOND terminal window NOW and run this, then LEAVE IT RUNNING until I tell you to stop (through the end of Phase 7):

  watch sudo chmod -R 777 /dev/ttyUSB0

It will print "No such file or directory" until the device appears — that's expected. (Default 2s interval is fine; do NOT shorten to -n 0.5.)

Type 'done' once it's running in its own terminal.
```

## Phase 4 — PHYSICAL: switch to UART boot mode (HARD PAUSE)

DIP for UART boot is `SW1: 1=ON, 2=OFF` on **both** boards. The wiring differs — print the `$BOARD` block verbatim and wait for `done`.

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

9. **Wait for `/dev/ttyUSB0`.**
   ```bash
   for i in {1..30}; do [ -e /dev/ttyUSB0 ] && break; sleep 1; done
   ls -l /dev/ttyUSB0; lsusb | grep -iE '0403:6015|0403:6001|serial|uart'
   ```
   If absent on **H15L**: DIP wrong, cable in the USB-A port instead of uUSB J1, or board not powered on. On **H15H**: the **adapter's USB isn't plugged into the host** (most common), adapter unpowered, or routed through a dock that doesn't pass it — try a direct port. A **swapped Rx/Tx** (pins 16↔18) gives a present `/dev/ttyUSB0` but a dead link — the loader will then fail to connect (see Phase 5).

## Phase 5 — Load recovery firmware + program storage

> **DO NOT MODIFY these commands — run them raw.** Never pipe `uart_boot_fw_loader` / `hailo15_emmc_program` / `hailo15_spi_flash_program` through `tail`, `head`, or any line-buffered consumer: their progress lines are the only evidence the transfer is alive (a pipe is what made an early run look "stuck"). For a copy, append `2>&1 | tee <logfile>` — never a truncating filter.
>
> **Hang/no-connect signatures:**
> - Silent, process in `D` state, `/dev/ttyUSB0` gone → FTDI lost its tty. Check `lsusb` + `/dev/ttyUSB0`; reset + replug rather than blind retry. Some LED flicker / brief ttyUSB0 disappearance mid-transfer is normal re-enumeration.
> - **`could not connect to the recovery agent`** → the boot ROM only listens on UART for a short window after reset. **Hard-pause, have the user press RESET, then re-run the loader immediately.** (Also verify DIP `1=ON, 2=OFF`, and on H15H that Rx/Tx aren't swapped.) This is expected on the first try if time elapsed since the last reset.

10. **Load recovery FW over UART** (`$BOARD` filename + flag from the matrix):
    ```bash
    cd <work-dir>
    # H15L:
    uart_boot_fw_loader --serial-device-name /dev/ttyUSB0 --firmware ./prebuilt/sbc/hailo15l_uart_recovery_fw.bin --h15l
    # H15H:
    uart_boot_fw_loader --serial-device-name /dev/ttyUSB0 --firmware ./prebuilt/sbc/hailo15_uart_recovery_fw.bin
    ```
    Success ends with "loaded successfully to the device" (H15H also prints `flash detected, flash jedec_id: …`).

11. **Program storage** — the long one, run from `prebuilt/sbc/`:
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

## Phase 6 — PHYSICAL: switch to normal boot mode (HARD PAUSE)

The DIP is the **opposite** between boards — read the matrix. Print the `$BOARD` block:

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

## Phase 7 — Drive U-Boot menu + swupdate

12. **Open serial console at 115200** (separate terminal; the watch-chmod terminal is fine — leave that loop running):
    ```bash
    sudo picocom -b 115200 /dev/ttyUSB0
    ```
    Permission-denied should clear within ~2s from the watch-chmod loop. Garbled text → wrong baud (must be `115200`, not the `921600` from H15L programming).

12.5 **PAUSE — press RESET (MANDATORY).** Print verbatim and wait for `done`:
    ```
    With picocom open, press the RESET button ONCE.
    The "*** U-Boot Boot Menu ***" only appears AFTER this manual reset — it will NOT show on its own.
    (Required on both H15L and H15H.)
    Type 'done' once you see the U-Boot Boot Menu.
    ```

13. **PAUSE — drive the menu.** Pick the `$BOARD` option:
    - **H15L:** highlight **"eMMC Board Init"**, ENTER.
    - **H15H:** highlight **"SD Card Board Init"**, ENTER.

    U-Boot TFTPs `fitImage` + the swupdate files from the host (`10.0.0.2`) and runs swupdate. Wait for:
    ```
      ... SWUpdate was successful !
      Rebooting...
      [N.NNNNNN] reboot: Restarting system
    ```
    Tell the user to type `done` at the reboot line. Failure modes up front:
    - **Menu doesn't appear** → reset not pressed, or DIP wrong (H15L both ON / H15H both OFF).
    - **TFTP timeout** → host firewall (`sudo ufw status`; if active `sudo ufw allow 69/udp`), 10.0.0.2 not bound, or `tftpd-hpa` down.
    - **swupdate fails / wrong-board image** → confirm the TFTP `fitImage` + swupdate files are the `$BOARD` ones (the `fitImage` name collides); re-stage and restart from Phase 4.

## Phase 8 — Verify

14. **Wait for the board on Ethernet.**
    ```bash
    for i in {1..90}; do ping -c 1 -W 1 10.0.0.1 >/dev/null 2>&1 && break; sleep 1; done
    ```

15. **Confirm the version.** A fresh flash **regenerates the SSH host key** and the new image accepts **password auth** (`root`), so the first SSH fails twice unless you clear the old key and supply the password non-interactively:
    ```bash
    ssh-keygen -f ~/.ssh/known_hosts -R 10.0.0.1
    sshpass -p root ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@10.0.0.1 \
      'grep "^VERSION=" /etc/os-release; cat /sys/devices/soc0/machine; uname -a'
    ```
    Report the new `VERSION` (matches target — **`VERSION`, not `VERSION_ID`**) and machine (`Hailo-15l`/`Hailo-15`). The changed host key is expected, not a security event. See `/connect`.

16. **Cleanup.** Leave the work dir in place. Tell the user the picocom session can close (`Ctrl-A Ctrl-X`) and the **Phase 3.5 watch-chmod loop can now stop** (`Ctrl-C`).

## Next step — Hailo Camera Viewer (guide §3.1, optional, same on both boards)

`/sw-update` ends at the version check. To see the camera afterward, §3.1 is three on-board commands:
```bash
setup_hailo_sensor.sh    # auto-detects sensor + lens, symlinks /etc/imaging/cfg/medialib_configs to the right hailo15l/hailo15h tree
camera-viewer-server     # serves the web UI on board port 80; launch detached (setsid … </dev/null &)
```
Then open `http://10.0.0.1/#/` in a host browser; click **Play** (§3.2). **`setup_hailo_sensor.sh` is mandatory** — without it `camera-viewer-server` crashes with `Configuration architecture '<other>' does not match current architecture '<this board>'`, because the shipped default config is for the other arch. Don't hand-pick a sensor/lens config — let the setup script detect it (it found imx678 + theia_sl410m on both reference boards).

## What success looks like

- `sshpass -p root ssh ... root@10.0.0.1 'grep "^VERSION=" /etc/os-release'` returns the new version (after clearing the old host key).
- Board boots autonomously on power cycle (no UART-mode dependency).
- Adjacent green LEDs stay lit.

## Gotchas

- **Wrong board's package bricks the SBC.** The Phase 0 target guard (token vs `$BOARD`) is non-negotiable. `hailo15l` ≠ `hailo15h` binaries.
- **DIP for normal boot is opposite per board:** H15L = both **ON**, H15H = both **OFF**. UART-boot DIP is the same (`1=ON, 2=OFF`).
- **H15H serial is on the adapter board, not the SBC.** `/dev/ttyUSB0` depends on the **adapter's** USB to the host (appears independent of SBC power); the adapter FTDI may be `0403:6001` (not the SBC's `6015`). **1.8V jumper + GND/Rx/Tx only, never Vcc** — wrong voltage or Vcc damages the device. A **swapped Rx/Tx** gives a live `/dev/ttyUSB0` but a dead link.
- **"could not connect to the recovery agent"** (H15H, but applies generally): the boot-ROM UART listen window is short — RESET then re-run the loader immediately.
- **921600 vs 115200** (H15L): `hailo15_emmc_program` uses 921600; picocom uses 115200. H15H `hailo15_spi_flash_program` takes no baud arg (`--uart-load`).
- **Ethernet must be 10.0.0.0/24, no DHCP switch**: the SBC is static `10.0.0.1`, no DHCP client.
- **U-Boot TFTP only reads `/var/lib/tftpboot`** (tftpd-hpa `--secure` chroots there). The **`fitImage` name collides across boards** — always overwrite from the correct package and byte-verify.
- **The doc's udev rule does NOT fix tty permissions** (`SUBSYSTEM=="usb"` vs the `tty` node `ftdi_sio` creates; and on H15H the PID may differ). The **Phase 3.5 watch-chmod** is the real fix and must survive every re-enumeration.
- **os-release uses `VERSION=`, not `VERSION_ID=`** (`NAME` is `Hailo15l` / `Hailo15`). Grep `^VERSION=`.
- **Post-flash SSH fails twice (expected):** changed host key → `ssh-keygen -R 10.0.0.1`; then password-only auth → `sshpass -p root` with `UserKnownHostsFile=/dev/null`. See `/connect`.
- **Camera viewer is a follow-on, not out of scope.** §3.1 sequence above; let `setup_hailo_sensor.sh` detect the sensor (imx678 on both reference kits) rather than hand-picking a config.

## What to delegate

- "The board still doesn't come up after flash" → `doc-explorer` on the recovery-mechanism section (H15L §2.3.2 / H15H §2.3.2) — the recovery procedure is this skill, so a persistent failure is likely hardware (storage, power, DIP, or H15H adapter wiring), not software.
- "Which sensor modules can I plug in next?" → §5.1 (supported modules / Approved Vendor List) — out of scope here.
