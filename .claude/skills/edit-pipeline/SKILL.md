---
name: edit-pipeline
description: Add, remove, or edit stages in an existing hailo-media-library app's analytics pipeline. Use when the user says "add a stage to X", "join Y into the pipeline", "make detection app also do Z", "stream out the AI metadata to another sink". Edits `main.cpp`'s `create_pipeline()`, then hands off to /cross-compile and /deploy. NOT for adding a new output stream — that's /add-stream.
tools: Read, Write, Edit, Bash, Grep, Glob, Agent
---

# /edit-pipeline — modify an app's analytics processing chain

A pipeline edit is a change to the `PipelineBuilder` chain in an app's `create_pipeline()` — adding, removing, or rewiring `Stage`s. Always a C++ source change. Pure config edits go elsewhere: `/add-stream` for new resolutions, `/swap-model` for new HEFs, `/add-osd` for just drawing bboxes.

Authoritative reference: **Media Library User Guide §10 (AI Analytics API)** — read it for the catalog of stages (§10.6) and the `PipelineBuilder` API (§10.5). The skill below assumes you already know that.

## Procedure

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

- **/explain-pipeline** if the user needs help articulate the current topology.
- **pipeline-expert** for cross-sink frame correlation (`MuxerStage`/`DemuxerStage`).
- **doc-explorer** for a specific stage's parameter list (§10.6).
- **/swap-model** for HEF-only changes (config struct on `HailortAsyncStage`, not topology).
- **/add-stream** for a new `sinkN` (frontend config, not topology).

## Gotchas

- **Stage names must match exactly** between `add_stage` and `connect`. Mismatches throw at `build()` time, not edit time.
- **`send_to_subscribers(data)` is mandatory** in every custom `process()` — even if you only inspect and forward. Skip it once and the chain stalls silently with no error.
- **Stage type controls start/stop order** (§10.4.1). `SOURCE` starts last and stops first; `SINK` starts first and stops last; `GENERAL` is in between. Get it wrong and you'll drop the first/last frames or stall shutdown.
- **Each `ThreadedStage` spawns a thread.** The H15 has 4 CPU cores. Long chains with many threaded stages contend — prefer a pre-built §10.6 stage over a custom one when possible, and tune queue sizes (§10.2.4) so the cheap stages don't starve the expensive ones.
- **`build()` validates connections, not semantics.** It throws on unknown stage names, but it does NOT check that buffer payloads are compatible between connected stages. Wiring a stage that expects detections after one that produces only raw frames compiles fine and runs to silence.
- **Resource ownership across helpers.** When two `generate_*_pipeline` helpers can both consume the same underlying resource (e.g. a sink's `EncoderStage`, or a `FrontendStage` output stream), only one can own it. The common case: `generate_vision_pipeline` auto-claims encoders for every sink listed in `vision_config.outputs`; if another helper needs that encoder, leave the sink out of `outputs`.