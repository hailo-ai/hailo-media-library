---
name: board-status
description: Snapshot the H15 SBC's runtime health — chip temperature, power consumption, CPU load, DRAM use, NN core utilization, DSP utilization, and the running app's PID. Use when the user asks "is the board OK", "is it overheating", "how loaded is the chip", "why is FPS low", or before/after a long demo to check how hot it's running and how much it's drawing. Read-only over SSH; no app restart.
tools: Bash, Read, Agent
---

# /board-status — read the H15's vital signs

Give the user one snapshot: thermals, power, CPU, memory, NN core utilization, running app PID. All canonical Hailo commands — nothing invented.

Authoritative reference: **Hailo OS User Guide v1.12.0** — `docs/guides/hailo_os_guide_1.12.0.pdf`. Relevant sections: §3.3 ("Temperature Monitoring"), §6.1 ("Hailo-15 Profiler"), §6.2 ("NoC Profiler"), §6.3 ("CMA Heap info").

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

The board identifies its own SBC revision via `/etc/build-info`'s `MACHINE` field. The procedure reads it inline and picks the right INA set automatically — no need to ask the user.

| `MACHINE`              | Board label         | INA addresses for SOC power                       | Rails measured                                  |
|------------------------|---------------------|---------------------------------------------------|-------------------------------------------------|
| `hailo15-sbc`          | SBC-Mercury-Rev2    | `1-42`, `1-43`, `1-40`                            | DDR_VDDQX, INA_0V8, INA_1V8                     |
| `hailo15-sbc-rev3-1`   | SBC-Mercury-Rev3.1  | `1-46`, `1-47`, `1-43`, `1-48`                    | INA_0V6, VDDQ_SOC, INA_0V8, INA_1V8             |
| `hailo15l-sbc`         | SBC-Pluto           | `1-40`, `1-41`, `1-44`, `1-46`, `1-47`            | INA_1V8, INA_0V8, INA_3V3, VDDQX_SOC, VDDQ_SOC  |

1. **Batch the read in a single SSH call.** The script auto-selects `INAS` from `MACHINE`:

   ```bash
   ssh root@<board> '
     M=$(awk -F" = " "/^MACHINE/{print \$2}" /etc/build-info)
     case "$M" in
       hailo15-sbc)        L="SBC-Mercury-Rev2";   INAS="1-42 1-43 1-40" ;;
       hailo15-sbc-rev3-1) L="SBC-Mercury-Rev3.1"; INAS="1-46 1-47 1-43 1-48" ;;
       hailo15l-sbc)       L="SBC-Pluto";          INAS="1-40 1-41 1-44 1-46 1-47" ;;
       *)                  L="unknown($M)";        INAS="" ;;
     esac

     echo "=== identity ==="
     uname -a; grep ^VERSION= /etc/os-release
     echo "Board: $L (MACHINE=$M)"

     echo "=== sensors (raw rails — thermals + per-INA power) ==="
     sensors

     echo "=== SOC power (sum of $INAS) ==="
     sensors 2>/dev/null | awk -v addrs="$INAS" "
       BEGIN { n = split(addrs, a, \" \"); for (i = 1; i <= n; i++) want[a[i]] = 1; total = 0 }
       /^ina231_precise-i2c-/ {
         split(\$0, p, \"i2c-\"); curr = p[2]
         keep = (curr in want) ? 1 : 0
       }
       /Power:/ && keep {
         val = \$3 + 0; unit = \$4
         if (unit == \"uW\") val /= 1000000
         else if (unit == \"mW\") val /= 1000
         total += val
       }
       END { printf \"SOC power: %.2f W\n\", total }
     "

     echo "=== cpu / mem ==="
     cat /proc/loadavg
     top -bn1 -w 200 | sed -n "1,5p"
     free -h | head -2

     echo "=== cma / dma-buf (OS Guide §6.3) ==="
     /usr/bin/hailo-dma-usage.sh -v 2>/dev/null

     echo "=== nn core (HailoRT CLI) ==="
     hailortcli scan 2>&1 | head -5
     hailortcli fw-control identify 2>&1 | grep -E "Firmware|Serial|Board" | head -5

     echo "=== dsp ==="
     (stdbuf -oL dsp-utilization 2>/dev/null & p=$!; sleep 2; kill $p 2>/dev/null) \
       | awk "/[0-9]+(\.[0-9]+)?[[:space:]]*%/{last=\$0} END{if(last)print last; else print \"N/A\"}"

     echo "=== running apps ==="
     pgrep -af "_case_study|hailort_server|hailoencodebin|hailofrontend|camera-viewer-server"
   '
   ```

   The aggregator parses each `ina231_precise-i2c-X-XX` chip's `Power:` line, normalizes the unit (µW / mW / W), and prints exactly `SOC power: N.NN W`. The full per-rail `sensors` output stays above it for debugging. If `MACHINE` is unrecognized, `INAS` is empty and SOC power will print `0.00 W` — flag that to the user instead of trusting the number.

2. **Capture one frame of `hailortcli monitor`** for live NN core utilization. The interactive form (`hailortcli monitor`) refreshes in place using ANSI screen-clear codes, so a plain piped/timeout'd ssh returns empty. Use this incantation instead — it forces line buffering, kills after 2 s, strips the ANSI escapes, and keeps just the first refresh:
   ```bash
   ssh root@<board> 'stdbuf -oL hailortcli monitor & p=$!; sleep 2; kill $p 2>/dev/null' \
     | sed -E "s/\x1b\[[?]?[0-9;]*[a-zA-Z]//g; s/\x1b[()][AB012]//g" \
     | awk "/Device ID/{found=1} found{print}" | head -20
   ```
   Empty output ⇒ the running app didn't export `HAILO_MONITOR=1`, or isn't using a `VDevice` (see Gotchas) — report that fact, don't make up a "0 %" reading.

3. **Summarize — extract numbers, don't dump raw output.** Use the **`SOC power: <N> W`** line from step 1 as the headline power figure (not per-rail). Highlight any line that's trending hot or unusually loaded *first*. The `(high = +X°C)` numbers inside the raw `sensors` block are sensor-chip trip points, **not** the SoC die's thermal envelope (which is SCU-managed — see thermal gotcha below).

## Output format

```
H15<H|L> rev<…> · SW <ver> · uptime <…>
Thermals:  H15 temp1 <X>°C  temp2 <Y>°C  near-SoC <Z>°C
SOC power: <N.NN> W
CPU (4c):  load <1m> / <5m> / <15m>   id <X>% sy <Y>%
Memory:    <avail> MiB available of <total> MiB
NN core:   <N> device(s), FW <ver>
DSP:       <X>% utilization
Running:   <app cmd line>
```

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
