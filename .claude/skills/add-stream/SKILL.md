---
name: add-stream
description: Add a new output stream (sinkN) to a hailo-media-library app — resolution, framerate, and encoder. Use when the user asks "add another stream", "add an FHD/4K/HD output", or "I need a Nth video output". Edits application_settings.json + the active profile JSON, generates per-sink encoder/osd/masking files, and pushes to the board.
tools: Read, Write, Edit, Bash, Grep, Agent
---

# /add-stream — add a new output stream

A "stream" here is a frontend output: a resolution+framerate that the ISP produces and that the app can either encode out as UDP or feed to AI. Adding one is **purely a config change** if the app's `vision_config` iterates over all frontend outputs (which the standard case-study apps do via `base_vision_config(output_streams)`). No recompile needed.

## Inputs to ask the user

- Width × height × framerate (e.g. `1920x1080@30`).
- What's it for: encoded UDP output, or feed to AI (replace existing AI sink), or both?

Use the next free `sinkN` (one greater than the current highest) as the stream ID — no need to ask.

## Procedure

1. **Identify the active medialib config & profile.** Read `MEDIALIB_CONFIG_PATH` from the app's `main.cpp`. Pull the JSON from the board, follow `profiles[].config_file` for the currently-default profile, and pull that too.

2. **Pull the templates.** From the same profile dir, pull the existing `application_settings.json`, profile JSON, and one set of per-sink files (`encoder_sink1.json`, `osd_sink1.json`, `masking_sink1.json`) to use as templates.

3. **Edit `application_settings.json`.** Add a new entry to `application_input_streams.resolutions[]`:
   ```json
   { "framerate": 30, "height": 1080, "pool_max_buffers": 12, "stream_id": "sinkN", "width": 1920 }
   ```
   Keep `pool_max_buffers` at 12 unless the user asks otherwise (15 is reasonable for AI sinks where frames may queue).

4. **Edit the profile JSON.** If the new stream should be encoded (UDP-out), append a `sinkN` entry to `encoded_output_streams` pointing at the three new per-sink files you'll create:
   ```json
   {
     "encoding": ".../encoder_sinkN.json",
     "masking": ".../masking_sinkN.json",
     "osd": ".../osd_sinkN.json",
     "stream_id": "sinkN"
   }
   ```
   If it's AI-only and the app erases this sink in `vision_config.outputs`, you can skip the encoded entry.

5. **Generate the per-sink files.** Copy the templates to `encoder_sinkN.json`, `osd_sinkN.json`, `masking_sinkN.json`. In `encoder_sinkN.json`, set `encoding.input_stream` to the new resolution/framerate, and pick a sensible `target_bitrate` (4K~25Mbps, FHD~12Mbps, HD~9Mbps, 720p~6Mbps). Keep all other fields from the template.

6. **Push.** `scp` the five edited/new files into the profile directory on the board, replacing the existing two and adding the three new ones. Back up the existing files first (`scp <file>{,.bak}` on board).

7. **Run with the hash bypass.** The medialib config validator will reject your edits because the SHA256 `content_hash` in the metadata block no longer matches. For demo/development runs:
   ```
   HAILO_MEDIA_LIB_SKIP_METADATA_CONFIG_VALIDATION=1 <app>
   ```

8. **Verify.** Run the app for ~10s with `-p` (print FPS) and confirm the new `sinkN` line appears in the FPS output and reaches a steady ~target framerate. Tell the user the UDP port: **`5000 + N*2`** (so sink3 = 5006).

## What to delegate

- The "is this resolution/framerate within the SoC's bandwidth budget" question → **perf-expert**.
- "What encoder settings are best for use case X" or sensor-side limits → **doc-explorer** with the media library user guide.

## Gotchas

- The pipe is **`PORT = base + sink_num*2`**, not `base + sink_num`. e.g it's 5006 for sink3.
- Total pixel-rate across all streams is bounded by ISP/DSP throughput. Adding a 4K@30 alongside an existing pipeline that also has 4K@30 may not fit. If unsure, check with **perf-expert**.
- If the app's `main.cpp` does `vision_config.outputs.erase("sinkN")` for the *new* stream you just added, it won't produce UDP output. Read `main.cpp` before declaring done.
- Profile directories differ by board+sensor — for H15L+imx678 it's `/etc/imaging/cfg/hailo15l/imx678/theia_sl410m/4k/profiles/<profile>/...`. Don't hardcode; resolve from the medialib config.

## Offer to run

When this skill's work is complete, ask the user (AskUserQuestion) whether they want to run the app now and see it live. If yes, invoke the **/run-app** skill.
