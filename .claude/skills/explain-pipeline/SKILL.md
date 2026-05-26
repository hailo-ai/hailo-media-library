---
name: explain-pipeline
description: Explain how a hailo-media-library application's pipeline is structured — sources, AI stages, sinks, and the medialib config that feeds it. Use when the user asks "what does this app do", "how is X pipeline wired", or before they modify an app. Reads the app's main.cpp, traces generate_*_pipeline calls, and maps it to the active medialib config.
tools: Read, Bash, Grep, Glob, Agent
---

# /explain-pipeline — trace and explain a media-library app

The mission is to give the customer a clear picture of an app's dataflow without making them read code. The hailo-media-library apps follow a very consistent pattern: **a sequence of `generate_*_pipeline()` calls plus a `PipelineBuilder` that connects them**. Find those, name the stages, and describe what each does.

## Inputs you need

- Which app? (e.g. `apps/case_studies/detection`, `apps/face_landmarks`, `apps/webserver`)
- Optionally: which medialib config it uses at runtime — usually a `MEDIALIB_CONFIG_PATH` `#define` in `main.cpp` pointing at `/etc/imaging/cfg/medialib_configs/*.json`.

If the user just says "the detection app" or similar, default to the case-study version under `hailo-analytics/apps/case_studies/`.

## Procedure

### Step 0 — Ask the user which approach to use

Before doing anything, ask the user (via `AskUserQuestion`) to pick:

- **Fast (source only)** — Source code reading only. Faster.
- **Thorough (Pipeline Viewer)** — Run the app on the board using the Pipeline Viewer tool to dump the real runtime information. More accurate, takes extra time.

Do not name specific files (no `main.cpp`, no `generate_*_pipeline`, no env var names) in the prompt. Say for the Fast option — only "faster". If the user asks what Pipeline Viewer is, explain in 1–2 sentences: *Pipeline Viewer is a hailo-media-library tool (section 12.1 of the user guide) that dumps the actual runtime pipeline graph as a Graphviz `.dot` file.*

If **Fast** → skip Step 1 and go straight to Step 2.
If **Thorough** → run Step 1 then continue.

### Step 1 — Pipeline Viewer first (the ground truth)

Per section 12.1 of the media library user guide, the runtime can dump the **actual** pipeline graph as a Graphviz `.dot` file. This is the authoritative topology — start here.

1. **Make sure /connect has been run** so we have SSH to the board (default `root@10.0.0.1`).

2. **Check whether a dump already exists on the board:**
   ```bash
   ssh root@10.0.0.1 'ls -la /tmp/*.dot 2>/dev/null'
   ```
   If files are there from a recent run, ask user if the dot files on board are of the requested pipeline, if so skip to step 4.

3. **Re-run the app with DOT export enabled.** The env var is `HAILO_ANALYTICS_DUMP_DOT=1`. App binaries live under `/home/root/apps/<group>/<app>/<binary>` — for case-study apps that's `/home/root/apps/case_studies/<app>/<app>_case_study` (e.g. `/home/root/apps/case_studies/detection/detection_case_study`). Stop the currently-running app first, then:
   ```bash
   ssh root@10.0.0.1 'HAILO_ANALYTICS_DUMP_DOT=1 /home/root/apps/case_studies/<app>/<app>_case_study &'
   # let it run a few seconds so the pipeline is constructed
   sleep 3
   ssh root@10.0.0.1 'ls /tmp/*.dot'
   ```
   One `.dot` file is produced per pipeline, named after the pipeline name passed to the builder (e.g. `/tmp/main_pipeline.dot`). If the binary path is unknown, `find /home/root/apps -type f -executable -name "*case_study*"` will list them.

4. **Pull the dot text and read it directly.** It's plain text — no need to render to PNG, just `Read` the file content:
   ```bash
   scp root@10.0.0.1:/tmp/<pipeline_name>.dot /tmp/
   ```
   Then use the Read tool on `/tmp/<pipeline_name>.dot`. The nodes are the stage names exactly as wired in `create_pipeline()` (`frontend_stage`, `enc_sink0`, `udp_sink0`, `tiling`, `detection_async`, `postprocess`, `aggregator`, `zmq_sender`, etc.), and the edges are the real connections.

5. **Optionally render** for the user if they ask for a picture:
   ```bash
   dot -Tpng /tmp/<pipeline_name>.dot -o /tmp/<pipeline_name>.png
   ```
   Requires `graphviz` on the host (`apt-get install graphviz`). Skip this unless asked — the text form is enough for explanation.

**If the dot dump is unavailable** (app not on the board yet, board not connected, env var unsupported in the deployed build), say so explicitly and fall through to Step 2. Do not silently skip the Pipeline Viewer.

### Step 2 — Fill in what the dot doesn't show (source)

The `.dot` gives topology and stage names but **not** model paths, resolutions, frame rates, or UDP/ZMQ endpoints. Get those from source.

1. **Read `main.cpp`.** Pay attention to:
   - The constants at the top — `VISION_PIPELINE`, `TILING_PIPELINE`, `MEDIALIB_CONFIG_PATH`, `VISION_SINK`, `AI_SINK`. These name the building blocks.
   - The `create_pipeline()` function — confirm it matches the edges you saw in the dot.
   - Any `vision_config.outputs.erase(...)` calls — they tell you which frontend streams are consumed by AI rather than encoded out.
   - The final `PipelineBuilder` chain (`add_stage`, `connect_frontend`, `connect`).

2. **Resolve each `generate_*_pipeline` call.** For each one, read `hailo-analytics/hailo_analytics_api/src/analytics/<name>.cpp` to learn what stages it instantiates. Common ones:
   - `generate_vision_pipeline` → `frontend → encoder → UDP` per frontend output stream. Defaults: encoder is H264, UDP host `10.0.0.2`, port = `base_port + sink_num*2`.
   - `generate_tiling_detection_pipeline` → `TilingCropStage → DetectionSubPipeline (HailortAsyncStage → PostprocessStage) → AggregatorStage`. Default model is whatever `DETECTION_BASE_HEF` says in `analytics/detection.hpp`.
   - `generate_detection_pipeline` → just `HailortAsyncStage → PostprocessStage` (no tiling).
   - `generate_analytic_metadata_zmq_sender_pipeline` → publishes detections over ZMQ (default `tcp://*:7000`).

### Step 3 — Runtime config (resolutions and stream IDs)

SSH into the board and read the actual medialib JSON the app loads. Trace it: top-level config → `profiles[].config_file` → `application_settings.json` → the `application_input_streams.resolutions[]` array. Each entry's `stream_id` (e.g. `sink0`, `sink2`) is one frontend output. Map those IDs back to the nodes you saw in the dot.

### Step 4 — Cross-reference

Combine dot topology + source defaults + runtime config to produce something like:
```
frontend.sink0 (4K@30)  → vision_pipeline → enc → UDP 10.0.0.2:5000
frontend.sink1 (720p@30)→ vision_pipeline → enc → UDP 10.0.0.2:5002
frontend.sink2 (1080p@15)→ tiling_detection_pipeline (yolov8s) → ZMQ tcp://*:7000
```

## Output format

Three short blocks:
1. **What the app does** (one sentence).
2. **Pipeline diagram** in the format above — concrete stream IDs, resolutions, output ports, model name. Cite source lines (`main.cpp:NN`, `tiling.cpp:NN`).
3. **Tunables** — what the user is most likely to change next (model, streams, output ports), with file paths.


## When to delegate

If the user wants the full architecture across multiple apps or asks about postprocess library internals, hand off to **pipeline-expert** with the specific question. For "what does the imaging side do" (sensor/IQ/dewarp), hand off to **doc-explorer** with the imaging user guide.

## Gotchas

- A frontend stream that is `erase()`d from `vision_config.outputs` will *not* produce UDP output — and it also won't appear in the `.dot`.
- The default detection HEF/postprocess constants live in `hailo_analytics_api/include/hailo_analytics/analytics/detection.hpp`, not in `main.cpp`. If the app doesn't override them via a user_configs override, those are what runs. None of this is visible in the `.dot` — read the source.
- `PORT_FROM_ID` is `base + sink_num*2`, **not** `base + sink_num`. So sink3 = port 5006 with default base 5000.
- `HAILO_ANALYTICS_DUMP_DOT=1` writes to `/tmp/<pipeline_name>.dot`. The pipeline name comes from the builder, so a multi-pipeline app produces multiple files — list `/tmp/*.dot` rather than guessing the name.
- The dump happens at pipeline construction. If you re-run the app, the file is overwritten.
