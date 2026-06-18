---
name: add-overlay
description: Add, modify, or remove AI analytics overlay (bboxes, masks, landmarks, etc.) on a hailo-media-library encoded stream. Two paths — (A) board-side burn-in via OverlayStage, (B) host-side drawing by analytic_viewer. Use when the user asks for any analytics overlay or "see the bboxes/masks/landmarks". MUST ask whether the overlay should be drawn on board (burned into UDP stream) or on host (display-only by analytic_viewer) before proceeding.
tools: Read, Write, Edit, Bash, Grep, Agent
---

# /add-overlay — manage analytics overlays

"Analytics overlay" here covers any visual rendering of an AI pipeline's output: detection bboxes, segmentation masks, face/pose landmarks, dynamic privacy masks (DPM), license plate text, recognition labels, and so on. The mechanism is the same regardless of analytics type — only the data being drawn differs.

There are two different places an overlay can be drawn:

- **On the board** — burned into the encoded stream before it leaves the device. Visible to *anyone* who receives that UDP port.
- **On the host** — drawn by the viewer (analytic_viewer) at display time on top of the decoded video. The encoded stream itself is unmodified; only the local window shows the overlay.

These are not interchangeable. **You must clarify the user's intent before any work.**

## MANDATORY first step — ask

Before reading code, editing files, or pushing anything, ask the user:

**Where should the overlay be drawn — on the board (burned into the encoded UDP stream) or on the host (only in the viewer)?**

- **Board** → **Path A (OverlayStage)** — analytics burned into the encoded stream, every UDP receiver sees them.
- **Host** → **Path B (analytic_viewer)** — viewer draws analytics on the decoded frame, encoded stream is unchanged.

Do not skip this question even if the user said "OSD" — that word is overloaded. Ask.

## Quick reference — which path

| User wants | Path | Where rendered | Code change? | Visible in… |
|---|---|---|---|---|
| Analytics overlay (everyone sees) | A (OverlayStage) | Board, before encode | Yes (`main.cpp` rewire) | All viewers of UDP |
| Analytics overlay (host only) | B (analytic_viewer) | Host, on decoded frame | No board change | Only this viewer |

---

## Path A — OverlayStage (analytics burn-in on board)

**When to use:** the user wants AI analytics output **rendered on the board** so every UDP receiver sees it — including the plain `analytic_viewer` run without a ZMQ subscription, since the overlays are already baked into the encoded stream. This requires a `main.cpp` change + cross-compile + redeploy. Replaces (or augments) host-side analytics drawing.

### What it is

`hailo_analytics::analytics::overlay::generate_overlay_pipeline()` constructs a pipeline of `OverlayStage → encoder → UDP`. The OverlayStage receives analytics metadata (from a detection / tiling_detection / face_landmarks / dpm / segmentation / etc. pipeline) plus the original frame, draws the appropriate primitives (bboxes, polygons/masks, landmark points, text labels) using OpenCV, and hands the modified frame to the encoder.

### Wiring (example: detection case-study app)

The standard detection case-study `main.cpp` originally goes:
```
frontend.sinkN → tiling_detection_pipeline → analytic_metadata_zmq_sender → ZMQ
```
with `vision_config.outputs.erase(AI_SINK)` so the AI sink isn't encoded by the vision pipeline.

The same wiring shape applies for any other analytics pipeline (e.g. `dpm_pipeline`, `face_landmarks_pipeline`, `segmentation_pipeline`) — substitute the relevant `generate_*_pipeline` and connect its output into the overlay pipeline.

To switch to board-side burn-in:

1. **Replace the ZMQ sender with the overlay pipeline.** Keep `vision_config.outputs.erase(AI_SINK)` — the OverlayStage uses the *same* encoder via `m_encoders[AI_SINK]`, and you don't want the vision pipeline encoding the AI sink in parallel.

2. **Construct the overlay pipeline targeting the AI sink:**
   ```cpp
   #include "hailo_analytics/analytics/overlay.hpp"

   // CRITICAL: pass AI_SINK to base_overlay_vision_output_config, otherwise the UDP
   // port defaults to sink0's port and collides with the real sink0 stream.
   auto overlay_user_cfg = hailo_analytics::analytics::overlay::base_overlay_vision_output_config(AI_SINK);
   overlay_user_cfg.vision_output_config.udp_config.host = host_ip;

   // Optional: change overlay color. cv::Scalar is RGB-as-passed (NOT BGR).
   overlay_user_cfg.overlay_config.color_selector =
       [](const HailoDetectionPtr &) { return cv::Scalar(255, 0, 0); }; // red

   auto overlay_pipeline_status = hailo_analytics::analytics::overlay::generate_overlay_pipeline(
       app_resources->media_library->m_encoders[AI_SINK],
       OVERLAY_PIPELINE,
       overlay_user_cfg);
   ```

3. **Connect analytics → overlay (replacing → ZMQ):**
   ```cpp
   pip_builder.add_stage(vision_pipeline, hailo_analytics::pipeline::StageType::SOURCE)
       .add_stage(tiling_pipeline)                 // or any other analytics pipeline
       .add_stage(overlay_pipeline, hailo_analytics::pipeline::StageType::SINK);

   pip_builder.connect_frontend(VISION_PIPELINE, AI_SINK, TILING_PIPELINE);
   pip_builder.connect(TILING_PIPELINE, OVERLAY_PIPELINE);
   ```

   If you want to keep ZMQ alongside (so host viewers also work), keep both sinks and connect the analytics pipeline → both.

4. **Cross-compile.** Hand off to `/cross-compile`. **OpenCV is required** — the OverlayStage transitively pulls in `opencv2/opencv.hpp`. Compile-time needs `pkg-config --cflags opencv4`. **Link with only the libs the board image actually has** — do not blanket `pkg-config --libs opencv4`, which adds `-lopencv_gapi -lopencv_videoio …` that aren't on a stock H15L image and will fail at runtime with `cannot open shared object file`. The minimal set:
   ```
   -lopencv_core -lopencv_imgproc -lopencv_freetype
   ```
   Verify the board's lib set with `ssh root@<board> 'ls /usr/lib/libopencv_*.so.405'` before linking anything else.

5. **Deploy.** Push the binary; configs are unchanged.

6. **Verify with `analytic_viewer` (no ZMQ subscription).** The overlay is already in the bitstream, so launch the viewer with only the UDP port — omit `--analytic-data-port` so it does no host-side drawing on top:
   ```bash
   source tools/analytic_viewer/.venv/bin/activate
   cd tools/analytic_viewer
   python app_analytic_draw_client.py --udp-port <udp_port>
   ```
   If `tools/analytic_viewer/.venv` doesn't exist and you don't know the user's venv, ask — and offer to create it (`python3 -m venv tools/analytic_viewer/.venv && tools/analytic_viewer/.venv/bin/pip install -r tools/analytic_viewer/requirements.txt`).
   Confirm the overlays are visible. Because nothing is being drawn host-side, anything you see is what every other UDP receiver gets too.

### overlay_config_t fields you can override

```cpp
struct overlay_config_t {
    std::optional<std::string>                                stage_name;
    std::optional<bool>                                       skip;
    std::optional<bool>                                       partial_landmarks;
    std::optional<std::unordered_set<size_t>>                 landmark_indices_to_draw;
    std::optional<size_t>                                     queue_size;
    std::optional<bool>                                       leaky;
    std::optional<std::unordered_set<int>>                    class_ids_to_draw;
    std::optional<std::function<cv::Scalar(const HailoDetectionPtr &)>>
                                                              color_selector;
    std::optional<bool>                                       trace;
};
```
- `class_ids_to_draw` filters which class IDs render (use to hide noisy classes — applies to detection-style outputs).
- `color_selector` is a per-detection lambda — return `cv::Scalar` (RGB order, see below). Useful for color-by-class or by-confidence.
- `partial_landmarks` + `landmark_indices_to_draw` apply when the analytics pipeline produces landmarks (face_landmarks, pose, etc.).

### Color order in OverlayStage

`cv::Scalar` here is **RGB-as-passed**, not BGR. Reference: `hailo-postprocess/postprocess_tools/include/hailo_postprocess_tools/image_utils/overlay_utils.hpp` defines the per-class color table; entry 0 is `cv::Scalar(255, 0, 0)` and is rendered red. This is the opposite of OpenCV's normal `imshow` convention — don't carry BGR habits over from generic OpenCV code.

| Color | `cv::Scalar(...)` |
|---|---|
| Red | `(255, 0, 0)` |
| Green | `(0, 255, 0)` |
| Blue | `(0, 0, 255)` |
| Yellow | `(255, 255, 0)` |
| White | `(255, 255, 255)` |

### Critical gotchas

- **UDP port defaults to sink0.** `generate_overlay_pipeline()` does *not* derive the port from the encoder you pass — it builds its `vision_output_config` from `base_vision_output_config("sink0")` unless overridden. Always pass `base_overlay_vision_output_config(AI_SINK)` (with the actual AI sink ID) as `user_configs`. Symptom of forgetting: two FPS lines for `udp_sink0` in the app's `-p` output, one at 30 FPS (real sink0) and one at the AI sink's framerate (collision).
- **Don't double-encode the AI sink.** Keep `vision_config.outputs.erase(AI_SINK)` so the vision pipeline doesn't also build an encoder→UDP for that stream.
- **OpenCV link-set must match the board.** The host SDK's `opencv4.pc` lists every module; the board image ships only a subset. Link explicitly with `-lopencv_core -lopencv_imgproc -lopencv_freetype` (or whatever subset you confirm exists on the board).

### Heads-up about a different bug class

If the user is using the **older** API namespace `analytic_metadata_sender::generate_analytic_metadata_sender_pipeline` and the board has a newer `libhailo_analytics.so` (1.11.x), the symbol is now `analytic_metadata_zmq_sender::generate_analytic_metadata_zmq_sender_pipeline`. This has nothing to do with overlays directly, but the same `main.cpp` you're editing for OverlayStage may need this rename too. Check `nm -D /usr/lib/libhailo_analytics.so.* | grep generate_analytic_metadata` on the board to confirm which symbol exists.

---

## Path B — analytic_viewer (host-side analytics drawing)

**When to use:** the user wants analytics overlay only in their viewer window without modifying the encoded stream, or doesn't want to recompile the board app. Quickest path to "see the analytics" during demo dev.

### How it works

The board app keeps its `analytic_metadata_zmq_sender_pipeline` and publishes analytics output as ZMQ messages. The host runs `app_analytic_draw_client.py`, which:
- Decodes UDP H264/RTP from one chosen sink.
- Subscribes to ZMQ on the board's metadata port (default `tcp://10.0.0.1:7000`).
- Joins frames to analytics by timestamp and draws bboxes / masks / landmarks / labels at display time.

The encoded stream is unchanged — anyone *else* receiving the same UDP port sees raw video without overlay.

### Procedure

1. **Confirm the board app has a ZMQ metadata sender.** It should have a stage like:
   ```cpp
   #include "hailo_analytics/analytics/analytic_metadata_zmq_sender.hpp"
   ...
   hailo_analytics::analytics::analytic_metadata_zmq_sender::
       generate_analytic_metadata_zmq_sender_pipeline(ANALYTIC_META_SENDER_PIPELINE);
   ```
   wired downstream of the analytics pipeline. Default ZMQ bind: `tcp://*:7000`.

2. **Pick the UDP port to overlay onto.** Analytics coordinates are computed on whatever frontend stream the AI taps (`AI_SINK`, typically `sink2`). The viewer takes ANY UDP video and scales overlay coords to the viewer's frame, but for spatially-correct overlays without scaling artifacts use the same sink the AI consumed (i.e. if AI runs on sink2 → run viewer on `5000 + 2*2 = 5004`). If the AI sink isn't UDP-encoded, temporarily restore it to `vision_config.outputs` so it goes out as UDP in parallel with feeding AI.

3. **Run the viewer:**
   ```bash
   source tools/analytic_viewer/.venv/bin/activate
   cd tools/analytic_viewer
   python app_analytic_draw_client.py --port <udp_port> \
       --analytic-data-ip <board_ip> --analytic-data-port 7000
   ```
   If `tools/analytic_viewer/.venv` doesn't exist and you don't know the user's venv, ask — and offer to create it (`python3 -m venv tools/analytic_viewer/.venv && tools/analytic_viewer/.venv/bin/pip install -r tools/analytic_viewer/requirements.txt`).

4. **Start the board app** (after the viewer is listening — UDP is fire-and-forget).

### Trade-offs vs Path A

| Aspect | Path A (board burn-in) | Path B (host viewer) |
|---|---|---|
| Visible to all UDP receivers | ✅ | ❌ (only this viewer) |
| Requires source change + recompile | ✅ | ❌ |
| Works with any video player | ✅ | ❌ (must use analytic_viewer) |
| Encoded stream is "clean" video | ❌ (overlay baked in) | ✅ |
| Viewer can be on any host | ✅ | ✅ (one per ZMQ sub) |
| Per-viewer customization | ❌ (all see same colors) | ✅ (each viewer own UI) |

If the user wants to ship a demo where customers see the analytics without installing tooling, Path A wins. If they want flexibility to inspect/record raw video and overlay on demand, Path B wins.

---

## Common error scenarios

### OverlayStage (Path A)

1. **`udp_sink0` line appears at the AI framerate, real sink0 looks degraded** — UDP port collision. You forgot to pass `base_overlay_vision_output_config(AI_SINK)` so overlay pipeline defaulted to sink0.
2. **`error while loading shared libraries: libopencv_gapi.so.405`** at runtime — linked too many OpenCV libs. Drop blanket `pkg-config --libs opencv4`; use the explicit minimal set.
3. **Overlay appears in wrong color** — assumed BGR. The API uses RGB-as-passed.
4. **Pipeline starts but nothing drawn** — verify the analytics postprocess function name matches the HEF tensor name (see `/swap-model` skill). Wrong pairing produces zero analytics output silently and the OverlayStage has nothing to draw.

### analytic_viewer (Path B)

1. **`AttributeError: 'memoryview' object has no attribute 'split'`** spam — known SEI parser bug. Patch `bytes(map_info.data).split(...)`.
2. **Window opens, video plays, no overlay** — ZMQ subscription failed silently. Verify `--analytic-data-ip` is the *board* IP (not host), board is publishing (`netstat -an | grep 7000` on board), and the board app's ZMQ pipeline wasn't replaced by an OverlayStage (Path A kills ZMQ unless wired in parallel).
3. **`Address already in use`** at board app start — a previous app instance still holds ZMQ port 7000. `pkill -f <app_name>` on the board.

## Delegate

- **pipeline-expert** for non-trivial OverlayStage wiring (e.g. fan-out to both ZMQ + overlay, multi-analytics pipelines, OverlayStage on a non-tiling pipeline).
- **/cross-compile** + **/deploy** after any Path A change.

## End-state checklist

- [ ] **Asked the mandatory question** (board vs host) and chose the right path.
- [ ] If A: `vision_config.outputs.erase(AI_SINK)` kept; overlay config built via `base_overlay_vision_output_config(AI_SINK)` (NOT default); minimal OpenCV link set; cross-compiled and deployed.
- [ ] If B: ZMQ sender pipeline still wired in `main.cpp`; viewer launched before board app; SEI parser bug patched if you see the spam.
- [ ] User visually confirmed the overlay on the right UDP port (`5000 + sink_num*2`).

## Offer to run

When this skill's work is complete, ask the user (AskUserQuestion) whether they want to run the app now and see it live. If yes, invoke the **/run-app** skill.
