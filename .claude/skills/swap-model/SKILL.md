---
name: swap-model
description: Replace the AI model in a hailo-media-library app with a different HEF (e.g. yolov8s → yolov8n, swap detection for personface, etc.). Use when the user says "use model X instead", "swap to YOLOv8n", or "try a smaller/larger model". Requires source edit + cross-compile + redeploy because model paths are constants in detection.hpp, not runtime config.
tools: Read, Write, Edit, Bash, Grep, Glob, Agent
---

# /swap-model — swap the detection HEF + postprocess

The default HEF/postprocess for the case-study apps live in `hailo_analytics_api/include/hailo_analytics/analytics/detection.hpp` as `string_view` constants:
- `DETECTION_BASE_HEF` — the .hef path on the board
- `DETECTION_POST_FUNCTION` — the C++ entry point in `libyolo_hailortpp_post.so`
- `DETECTION_POST_CONF` — the JSON with thresholds + label list

The app's `main.cpp` calls `generate_tiling_detection_pipeline(name)` with no override, so those defaults are what runs. To change the model, you have two choices:

1. **Override via `user_configs` in the app's `main.cpp`** (preferred). Touches one file; library stays untouched. This is what we did for yolov8n.
2. **Edit `detection.hpp` and rebuild the library**. Use only if the change should affect *every* app, not just one.

Default to option 1 unless the user explicitly says otherwise.

## Inputs to ask the user

- Target model (e.g. `yolov8n`, `yolov8m`, `yolov8n_personface`, custom HEF).
- HEF location on the board — list `/home/root/apps/face_landmarks/resources/` to see what's already there. If the requested HEF isn't there, ask whether they have the file locally to push, or want a different model.

## Procedure

1. **Find the postprocess function name.** Each model has its own entry point in `hailo-postprocess/postprocesses/detection/yolo_hailortpp.cpp` (`hailo_yolov8n`, `hailo_yolov8s`, `hailo_yolov8m`, `yolov8n_personface`, `yolov5`, …). The function name must match the HEF's tensor naming — `hailo_yolov8n` reads tensor `hailo_yolov8n_384_640/yolov8_nms_postprocess`. Mismatched function/HEF will silently produce no detections.

2. **Find the JSON config.** `/home/root/apps/webserver/resources/configs/` on the board has `yolov5.json`, `yolov8n.json`, `yolov5_personface.json`, etc. The JSON's `detection_threshold` and `max_boxes` are honored; `iou_threshold`, `output_activation`, `label_offset` are loaded but ignored by the `hailo_yolov*` postprocess (which uses hardcoded label maps from the headers). For `yolov5*` postprocess, the JSON's `anchors` array is required.

3. **Edit the app's `main.cpp`** (option 1). In `create_pipeline()`, before the `generate_tiling_detection_pipeline` call:
   ```cpp
   hailo_analytics::analytics::tiling::tiling_detection_config_t tiling_user_cfg;
   tiling_user_cfg.detection_config.ai_config.hef_path = "<path on board>";
   tiling_user_cfg.detection_config.post_config.function_name = "<postprocess function>";
   tiling_user_cfg.detection_config.post_config.config_path = "<json path on board>";
   auto tiling_pipeline_status = ...generate_tiling_detection_pipeline(TILING_PIPELINE, tiling_user_cfg);
   ```
   The `*_config_t` fields are `std::optional<std::string>` — assign the bare string and the optional engages.

4. **Cross-compile.** Hand off to **/cross-compile** with the modified `main.cpp`.

5. **Deploy.** Hand off to **/deploy**. Only the binary needs replacement — the HEF and JSON usually already exist on the board image.

6. **Verify.** Run the app for ~10s and confirm no errors at startup. To confirm detections actually flow, subscribe to the ZMQ port (`tcp://<board-ip>:7000` by default) with the analytic_viewer tool.

## Common model picks (H15L)

| Use case | HEF | function | json |
|---|---|---|---|
| General detection (COCO, fast) | `hailo_yolov8n_384_640.hef` | `hailo_yolov8n` | `yolov8n.json` |
| General detection (COCO, accurate) | `hailo_yolov8s_384_640.hef` | `hailo_yolov8s` | `yolov5.json` |
| General detection (COCO, even more accurate) | `hailo_yolov8m_384_640.hef` | `hailo_yolov8m` | `yolov5.json` |
| Person+face only (5 classes) | (not on default H15L image — needs push) | `yolov8n_personface` | `yolov5_personface.json` |

For non-detection swaps (face landmarks, classification, OCR), the override surface is different — the corresponding `generate_*_pipeline` has its own config struct. Read its `analytics/<name>.cpp` first.

## Delegate

- **model-expert** for "what HEFs are available", postprocess compatibility, ONNX → HEF questions.
- **pipeline-expert** if the swap requires reshaping the pipeline (e.g. moving from non-tiled to tiled, or adding a classifier after the detector).

## Gotchas

- The HEF's input resolution must match the tiling output (default 640×384). yolov8n_384_640 is fine; a 640×640 model isn't.
- Postprocess function must match HEF tensor name. Wrong pairing = silent zero detections, no error.
- Don't override `iou_threshold` from the JSON expecting it to take effect for `hailo_yolov*` — it doesn't.
