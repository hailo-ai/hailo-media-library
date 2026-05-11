---
name: board-status
description: Snapshot the H15 SBC's runtime health — chip temperature, power consumption, CPU load, DRAM use, NN core utilization, and the running app's PID. Use when the user asks "is the board OK", "is it overheating", "how loaded is the chip", "why is FPS low", or before/after a long demo to check how hot it's running and how much it's drawing. Read-only over SSH; no app restart.
tools: Bash, Read, Agent
---

# /board-status — read the H15's vital signs

Give the user one snapshot: thermals, power, CPU, memory, NN core utilization, running app PID. All canonical Hailo commands — nothing invented.

Authoritative reference: **Hailo OS User Guide v1.11.0** — `docs/guides/hailo_os_user_guide_1.11.0.pdf`. Relevant sections: §3.3 ("Temperature Monitoring"), §6.1 ("Hailo-15 Profiler"), §6.2 ("NoC Profiler"), §6.3 ("CMA Heap info").

### `hailortcli` cheat sheet (relevant subcommands)

The HailoRT CLI is preinstalled on the board image as `/usr/bin/hailortcli`. The subcommands this skill uses:

- **`hailortcli fw-control identify`** — returns firmware version, serial, board name. A health check that the SCU/FW side is alive.
- **`hailortcli monitor`** — prints three tables once per second:
  1. **Devices** — `Device Id`, total `Utilization (%)` (scheduler-runtime share spent on the NN core), `Architecture`.
  2. **Models** — per-loaded model: `FPS`, `Utilization (%)`, `PID` of the owning process.
  3. **Frames state** — per stream of each model: queue depths and pending counts.

  Sample interval defaults to 1 s — override with `HAILO_MONITOR_TIME_INTERVAL=<ms>`. Preconditions for non-empty output: see Gotchas.

- **`hailortcli measure-power`** — instantaneous wattage from the device side. Skip on every poll; perturbs the device briefly. Useful for spot-checks under a known workload.

## Preconditions

`/connect` succeeded — `ssh -o BatchMode=yes root@<board>` works (default `10.0.0.1`).

## Procedure

1. **Batch the read in a single SSH call**:
   ```bash
   ssh root@<board> '
     echo "=== identity ==="
     uname -a; grep ^VERSION= /etc/os-release

     echo "=== sensors ==="
     # H15 die temps 1 & 2, NEAR_H15L_SOC board temp, VDD_CORE power (0v8 rail),
     # 3v3 / 1v8 / 1v1 rail power, currents, voltages.
     sensors

     echo "=== cpu / mem ==="
     cat /proc/loadavg
     top -bn1 -w 200 | sed -n "1,5p"
     free -h | head -2   

     echo "=== cma / dma-buf (OS Guide §6.3) ==="
     # Add -v to also list usage per DMA-BUF exporter (useful when chasing leaks).
     /usr/bin/hailo-dma-usage.sh -v 2>/dev/null

     echo "=== nn core (HailoRT CLI) ==="
     hailortcli scan 2>&1 | head -5
     hailortcli fw-control identify 2>&1 | grep -E "Firmware|Serial|Board" | head -5

     echo "=== running apps ==="
     pgrep -af "_case_study|hailort_server|hailoencodebin|hailofrontend|camera-viewer-server"
   '
   ```

2. **Capture one frame of `hailortcli monitor`** for live NN core utilization. The interactive form (`hailortcli monitor`) refreshes in place using ANSI screen-clear codes, so a plain piped/timeout'd ssh returns empty. Use this incantation instead — it forces line buffering, kills after 2 s, strips the ANSI escapes, and keeps just the first refresh:
   ```bash
   ssh root@<board> 'stdbuf -oL hailortcli monitor & p=$!; sleep 2; kill $p 2>/dev/null' \
     | sed -E "s/\x1b\[[?]?[0-9;]*[a-zA-Z]//g; s/\x1b[()][AB012]//g" \
     | awk "/Device ID/{found=1} found{print}" | head -20
   ```
   Empty output ⇒ the running app didn't export `HAILO_MONITOR=1`, or isn't using a `VDevice` (see Gotchas) — report that fact, don't make up a "0 %" reading.

3. **Summarize — extract numbers, don't dump raw output.** Highlight any line that's trending hot or unusually loaded *first*. For thermal context see the CMA gotcha below — the `(high = +X°C)` numbers in `sensors` output are sensor-chip trip points, **not** the SoC die's thermal envelope, which is SCU-managed per OS Guide §3.3.

## Output format

```
H15L · SW <ver> · uptime <…>
Thermals:  H15 temp1 <X>°C  temp2 <Y>°C  near-SoC <Z>°C
Power:     VDD_CORE <P> W (<I> A @ <V> V)
CPU (4c):  load <1m> / <5m> / <15m>   id <X>% sy <Y>%
Memory:    <avail> MiB available of <total> MiB
NN core:   <N> device(s), FW <ver>
Running:   <app cmd line>
```

Typical idle-to-moderate observations: H15 die temps **70–74°C**, VDD_CORE **2–3 W** (≈2.2 W with AI-ISP + analytics, ≈3 W with detection-only). These are observations from healthy runs, not thermal limits — die throttling and the 120°C hard shutdown are SCU-side (§3.3); see the thermal gotcha below.

If anything looks off — load > 3 on 4 cores, MemAvailable < 200 MiB, no NN device found, app crash-looping, or die temps trending into the 80–90s — call it out as the **first** line of the report.

## When to delegate / dig deeper

- **`hailo-soc-profiler` (OS Guide §6.1)** for Perfetto traces — DDR bandwidth, media-library events, queue levels, Linux scheduler. Heavyweight; not for routine status:
  ```bash
  hailo-soc-profiler applications noc-bandwidth-vpu -t 10s -o /tmp/soc.trace
  scp root@<board>:/tmp/soc.trace .   # then open in ui.perfetto.dev
  ```
- **`htop`** when the user wants the live interactive CPU view rather than a one-shot snapshot.
- **`hailo-dma-usage.sh -u M`** to force MiB units explicitly, or **`-h`** for the full flag list. The snapshot already calls this with `-v`; usually you don't need anything else.
- **doc-explorer** for OS Guide §3.3 (thermal throttling internals, Hailo Thermal Engine) or §6.2 (NoC Profiler for memory bandwidth) — both inside the only doc the user is expected to have.
- **`hailortcli --help`** (or `hailortcli <subcommand> --help`) on the board itself for any sub-flag the cheat sheet doesn't cover.

## Gotchas

- **`hailortcli monitor` is silent unless the running app exported `HAILO_MONITOR=1` *and* uses a `VDevice`**. With either missing, you get empty output.
- **`(high = +X°C)` in `sensors` output is the per-sensor trip, not the SoC die envelope.** The die is SCU-managed: graduated throttling (states `S0`…`S4`) below 120°C, hard shutdown at 120°C — OS Guide §3.3.
- **CMA pressure looks like a pipeline failure, not a memory failure.** When video stages drop frames while `free -h` is healthy, check `hailo-dma-usage.sh -v` Use%. The `hailo_media_buf,cma` row is the contiguous-memory pool 4K / media-library pipelines allocate from — the first to fill up under heavy video load.
- **One-shot `top -bn1` reports CPU since boot** on its first sample. For a current spike, sample twice: `top -bn2 -d1 | tail -20` and use the second snapshot.
