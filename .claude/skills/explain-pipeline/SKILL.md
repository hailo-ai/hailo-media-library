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

1. **Read `main.cpp`.** Pay attention to:
   - The constants at the top — `VISION_PIPELINE`, `TILING_PIPELINE`, `MEDIALIB_CONFIG_PATH`, `VISION_SINK`, `AI_SINK`. These name the building blocks.
   - The `create_pipeline()` function — that's the single source of truth for how stages are wired.
   - Any `vision_config.outputs.erase(...)` calls — they tell you which frontend streams are consumed by AI rather than encoded out.
   - The final `PipelineBuilder` chain (`add_stage`, `connect_frontend`, `connect`) — those edges are the actual graph.

2. **Resolve each `generate_*_pipeline` call.** For each one, read `hailo-analytics/hailo_analytics_api/src/analytics/<name>.cpp` to learn what stages it instantiates. Common ones:
   - `generate_vision_pipeline` → `frontend → encoder → UDP` per frontend output stream. Defaults: encoder is H264, UDP host `10.0.0.2`, port = `base_port + sink_num*2`.
   - `generate_tiling_detection_pipeline` → `TilingCropStage → DetectionSubPipeline (HailortAsyncStage → PostprocessStage) → AggregatorStage`. Default model is whatever `DETECTION_BASE_HEF` says in `analytics/detection.hpp`.
   - `generate_detection_pipeline` → just `HailortAsyncStage → PostprocessStage` (no tiling).
   - `generate_analytic_metadata_zmq_sender_pipeline` → publishes detections over ZMQ (default `tcp://*:7000`).

3. **Pull the runtime config.** SSH into the board (if connected, use the /connect skill) and read the actual medialib JSON the app loads. Trace it: top-level config → `profiles[].config_file` → `application_settings.json` → the `application_input_streams.resolutions[]` array. Each entry's `stream_id` (e.g. `sink0`, `sink2`) is one frontend output.

4. **Cross-reference.** Combine source + runtime config to produce something like:
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

- A frontend stream that is `erase()`d from `vision_config.outputs` will *not* produce UDP output.
- The default detection HEF/postprocess constants live in `hailo_analytics_api/include/hailo_analytics/analytics/detection.hpp`, not in `main.cpp`. If the app doesn't override them via a user_configs override, those are what runs.
- `PORT_FROM_ID` is `base + sink_num*2`, **not** `base + sink_num`. So sink3 = port 5006 with default base 5000.
