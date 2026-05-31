---
name: edit-pipeline
description: Modify the pipeline of an existing hailo-media-library app — either by editing the medialib JSON on the board (via /add-stream when the change is "more streams" / "different resolution" / "different sink") or by editing `main.cpp`'s `create_pipeline()` and cross-compiling when the stage topology must change. Use when the user says "make this app do X", "add a stage to X", "join Y into the pipeline", "make detection app also do Z", "stream out the AI metadata to another sink", or "run N streams instead of 1". Triages JSON vs C++ first; only goes to /cross-compile + /deploy on the C++ path.
tools: Read, Write, Edit, Bash, Grep, Glob, Agent
---

# /edit-pipeline — modify a hailo-media-library app's pipeline

Scope: any change to an existing app's pipeline behavior — number of streams, output destinations, resolutions/bitrates, *and* topology (adding/removing/rewiring stages). Two paths:

- **JSON path** (cheap, on-board): parameter or count changes that don't move stages around. Edit the medialib JSON on the board, re-run the binary — no source change, no recompile. Delegate to `/add-stream` (or the other JSON-edit siblings) instead of doing it here.
- **C++ path** (cross-compile): the `PipelineBuilder` chain in `create_pipeline()` actually changes shape — a new stage type, a fork, a tee, a custom `ThreadedStage`. This skill owns that path.

Authoritative reference for the C++ path: **Media Library User Guide §10 (AI Analytics API)** — catalog of stages (§10.6) and `PipelineBuilder` API (§10.5). The procedure below assumes you already know that.

## JSON path — try this first

Most apps in this repo (`single_stream`, the vision case studies, etc.) build their pipeline by calling `generate_vision_pipeline()` / `generate_*_pipeline()` helpers that read the **medialib JSON config** on the board (path is `#define`d at the top of `main.cpp`, e.g. `/etc/imaging/cfg/medialib_configs/...`). If the change is "more of the same" or "same topology, different parameters", it's a JSON edit on the board — **don't cross-compile, delegate**.

| Request                                | Skill to use                                                             |
| -------------------------------------- | ------------------------------------------------------------------------ |
| "N streams instead of M"               | **/add-stream** (adds an `application_input_streams` resolution + a profile `encoded_output_streams` entry, generates per-sink encoder/osd/masking files) |
| "different resolution / framerate"     | **/add-stream** for a new sink; for editing an existing one, edit `application_settings.json` directly |
| "stream to a different host or port"   | edit the `udp` sink's `host` / `port` in the profile JSON               |
| "change bitrate / encoder profile"     | edit the per-sink `encoder_sinkN.json`                                   |
| "use a different HEF"                  | **/swap-model**                                                          |
| "draw bboxes on the video"             | **/add-overlay**                                                         |

**Heuristic:** if the answer to "does the *topology* of stages change?" is no — only counts, parameters, or destinations change — it's a JSON edit. Read the app's `main.cpp` for ~30 seconds first: if `create_pipeline()` is a single `generate_*_pipeline()` call, the pipeline lives in JSON, not C++.

The trap is that "make this app do 3 streams" sounds like a topology change but isn't — it's just two more entries in `application_input_streams.resolutions` plus matching `encoded_output_streams` entries in the profile JSON. `/add-stream` already knows how to do that end-to-end (templates, per-sink files, `scp` to the board, hash-bypass env var). Hand off rather than reimplementing.

Only fall through to the C++ procedure below when adding/removing/rewiring stages is genuinely required.

## C++ path — adding, removing, or rewiring stages

1. **Read `create_pipeline()`.** The `PipelineBuilder` chain is define the pipeline topology and the constants at the top of `main.cpp` name the building blocks.

2. **Pick the stage to insert.** First check §10.6 — most edits map to a pre-built stage (`LightweightTrackerStage`, `BBoxCropStage`+`AggregatorStage`, `TeeStage`, `ValveStage`, `CallbackStage`, etc.) and don't need custom C++. 
Only subclass `ThreadedStage` when nothing fits. 
The canonical minimal example is `apps/case_studies/custom_stage/main.cpp` — copy its `CustomStage` class as your starting point.

3. **Wire it in.** Three patterns cover almost everything:

   **Insert in series** between two connected stages — replace one `.connect(A, B)` with two:
   ```cpp
   pip_builder.add_stage(new_stage)
              .connect(A_NAME, NEW_NAME)
              .connect(NEW_NAME, B_NAME);
   ```

   **Fork a stage's output to two downstreams** — call `connect()` twice from the same source. `send_to_subscribers` broadcasts.
   ```cpp
   .connect(SRC, DOWNSTREAM_1)
   .connect(SRC, DOWNSTREAM_2);
   ```

   **Use a sub-pipeline helper** — `generate_*_pipeline()` returns a pre-wired `Pipeline`; insert it via `add_stage` as `SOURCE` / `GENERAL` / `SINK`. Library code on top of §10, not in the user guide:

   | Helper                                                  | Builds                          |
   | ------------------------------------------------------- | ------------------------------- |
   | `vision::generate_vision_pipeline`                      | frontend + optional encoder/UDP |
   | `overlay::generate_overlay_pipeline`                    | overlay → encoder → UDP         |
   | `detection::` / `tiling::generate_*_detection_pipeline` | AI inference                    |
   | `analytic_metadata_zmq_sender::generate_*_pipeline`     | packager + ZMQ sink             |

4. **For a custom `ThreadedStage`**, the only required override is `process()`:
   ```cpp
   AppStatus process(BufferPtr data) override {
       // inspect / mutate data->get_roi() (metadata) and / or data->get_buffer() (pixels)
       send_to_subscribers(data);
       return AppStatus::SUCCESS;
   }
   ```
   If the stage writes to pixel data, wrap the writes in `DmaMemoryAllocator::get_instance().dmabuf_sync_start/end` on both NV12 planes — `OverlayStage::process` (`hailo_analytics_api/src/pipeline/overlay/overlay_stage.cpp`) is the in-tree reference.

5. **Hand off to /cross-compile.** Only `main.cpp` changed; no library rebuild.

6. **Hand off to /deploy.** Run with `-p`; verify FPS at the sink is unchanged and `Starting.` / `Stopping.` bracket the run cleanly.

## When to delegate

- **/add-stream** — first stop for anything that's just "more streams" / "new sinkN" / different resolution. Handles JSON edits, per-sink file generation, and the on-board push.
- **/swap-model** — HEF-only changes (config struct on `HailortAsyncStage`, not topology).
- **/add-overlay** — just drawing bboxes / labels on the frame.
- **/explain-pipeline** — if the user needs help articulating the current topology before deciding.
- **pipeline-expert** — cross-sink frame correlation (`MuxerStage`/`DemuxerStage`).
- **doc-explorer** — a specific stage's parameter list (§10.6).

## Gotchas

- **Stage names must match exactly** between `add_stage` and `connect`. Mismatches throw at `build()` time, not edit time.
- **`send_to_subscribers(data)` is mandatory** in every custom `process()` — even if you only inspect and forward. Skip it once and the chain stalls silently with no error.
- **Stage type controls start/stop order** (§10.4.1). `SOURCE` starts last and stops first; `SINK` starts first and stops last; `GENERAL` is in between. Get it wrong and you'll drop the first/last frames or stall shutdown.
- **Each `ThreadedStage` spawns a thread.** The H15 has 4 CPU cores. Long chains with many threaded stages contend — prefer a pre-built §10.6 stage over a custom one when possible, and tune queue sizes (§10.2.4) so the cheap stages don't starve the expensive ones.
- **`build()` validates connections, not semantics.** It throws on unknown stage names, but it does NOT check that buffer payloads are compatible between connected stages. Wiring a stage that expects detections after one that produces only raw frames compiles fine and runs to silence.
- **Resource ownership across helpers.** When two `generate_*_pipeline` helpers can both consume the same underlying resource (e.g. a sink's `EncoderStage`, or a `FrontendStage` output stream), only one can own it. The common case: `generate_vision_pipeline` auto-claims encoders for every sink listed in `vision_config.outputs`; if another helper needs that encoder, leave the sink out of `outputs`.