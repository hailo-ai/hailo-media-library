---
name: swap-model
description: Replace the AI model in a hailo-media-library app with a different HEF (detection, face landmarks, segmentation/privacy mask, OCR/LPR, etc.). Use when the user says "use model X instead", "swap to YOLOv8n", "swap the landmarks model", or "try a smaller/larger model". Requires source edit + cross-compile + redeploy because model paths are `string_view` constants in the analytics headers, not runtime config.
tools: Read, Write, Edit, Bash, Grep, Glob, Agent
---

# /swap-model — swap the AI model HEF + postprocess of any analytics task

Each analytics task in `hailo_analytics_api/include/hailo_analytics/analytics/` defines its own HEF/postprocess defaults as `string_view` constants:

| Task | Header | HEF constant | Post-function constant | Post-config constant |
|---|---|---|---|---|
| Detection (single or tiled) | `detection.hpp` | `DETECTION_BASE_HEF` | `DETECTION_POST_FUNCTION` | `DETECTION_POST_CONF` |
| Face landmarks | `face_landmarks.hpp` | `LANDMARKS_BASE_HEF` | `LANDMARKS_POST_FUNCTION` | `LANDMARKS_POST_CONF` (often empty) |
| Segmentation / dynamic privacy mask | `dynamic_privacy_mask.hpp` | `SEGMENTATION_BASE_HEF` | `SEGMENTATION_POST_FUNCTION` | `SEGMENTATION_POST_CONF` (often empty) |
| OCR / license plate recognition | `license_plate_recognition.hpp` | `OCR_BASE_HEF` | (similar pair) | (similar pair) |

Each task also has a `<task>_config_t` struct (inheriting from `ai_postprocess_pair_config_t`) with the same two override knobs: `.ai_config.hef_path` and `.post_config.function_name` / `.post_config.config_path`. So the swap pattern is *uniform across tasks* — only the type names and the `generate_*_pipeline` call differ.

The app's `main.cpp` calls one (or several) `generate_<task>_pipeline(name)` with no override, so the header defaults are what runs. To change the model, you have two choices:

1. **Override via `user_configs` in the app's `main.cpp`** (preferred). Touches one file; library stays untouched.
2. **Edit the analytics header and rebuild the library**. Use only if the change should affect *every* app, not just one.

Default to option 1 unless the user explicitly says otherwise.

## Inputs to ask the user

- **Which analytics stage** to swap (if the app has more than one — e.g. a face-landmarks pipeline has both a face-detector stage and a landmarks stage; either could be swapped).
- **Target model** (e.g. `yolov8n`, `yolov8m`, `yolov8n_personface`, `scrfd_10g`, a custom HEF).
- **HEF location on the board** — list the app's `resources/` dir (e.g. `/home/root/apps/<app>/resources/`) to see what's already there. If the requested HEF isn't there, ask whether they have the file locally to push, or want a different model.

## Procedure

0. **Always inspect the HEF first with `hailortcli parse-hef`.** Run this on every HEF — H15H *and* H15L — before doing any source edits:
   ```bash
   hailortcli parse-hef <path-to-hef>
   ```
   Check the `Input` line. The media-library pipeline feeds **NV12** frames into the AI stage, so the HEF input must be:
   ```
   Input  <name> UINT8, NV12(<H/2>x<W>x3)
   ```
   - **H15L HEFs** from the zoo ship as NV12 — they drop in as-is.
   - **All H15H HEFs from the zoo ship as `NHWC(HxWx3)`** (not NV12). They will *not* drop into a media-library pipeline without a format conversion. Flag this to the user before continuing — the pipeline needs an extra NV12→RGB/NHWC conversion stage, otherwise the AI stage will reject the frames.

   Also note the input resolution and output tensor name from `parse-hef` — they feed steps 1 and 2 below (postprocess function selection + tiling/resolution sanity check).

1. **Find the postprocess function name.** Each model has its own entry point in the matching postprocess `.so`:
   - Detection (yolo family): `hailo-postprocess/postprocesses/detection/yolo_hailortpp.cpp` → `hailo_yolov8n`, `hailo_yolov8s`, `hailo_yolov8m`, `yolov8n_personface`, `yolov5`, …
   - Face landmarks: `facial_landmarks_nv12` and similar in the landmarks postprocess source.
   - Segmentation (privacy mask): `linknet_post`, etc.
   - OCR / LPR: the LPR postprocess.

   The function name must match the HEF's tensor naming — e.g. `hailo_yolov8n` reads tensor `hailo_yolov8n_384_640/yolov8_nms_postprocess`. Mismatched function/HEF will silently produce no output (no detections, no landmarks, blank masks, …) with no error.

2. **Find the JSON config (if the task uses one).** Detection-family tasks read JSONs from `/home/root/apps/webserver/resources/configs/` (`yolov5.json`, `yolov8n.json`, `yolov5_personface.json`, …). The JSON's `detection_threshold` and `max_boxes` are honored; `iou_threshold`, `output_activation`, `label_offset` are loaded but ignored by `hailo_yolov*` (hardcoded label maps in the headers). For `yolov5*`, the JSON's `anchors` array is required.

   Some tasks have **no JSON** — `LANDMARKS_POST_CONF` and `SEGMENTATION_POST_CONF` are empty strings by default. For those, leave `post_config.config_path` unset (don't assign anything to it).

3. **Edit the app's `main.cpp`** (option 1). Find the `generate_<task>_pipeline(...)` call in `create_pipeline()` and add a `<task>_config_t` override before it. The pattern is the same across tasks — only the type name and the field path change.

   **Detection (tiled), e.g. yolov8s → yolov8n:**
   ```cpp
   hailo_analytics::analytics::tiling::tiling_detection_config_t tiling_user_cfg;
   tiling_user_cfg.detection_config.ai_config.hef_path           = "<hef path on board>";
   tiling_user_cfg.detection_config.post_config.function_name    = "<postprocess function>";
   tiling_user_cfg.detection_config.post_config.config_path      = "<json path on board>";
   auto tiling_pipeline_status = ...generate_tiling_detection_pipeline(TILING_PIPELINE, tiling_user_cfg);
   ```

   **Detection (non-tiled):** same pattern, but `detection::detection_config_t` and `generate_detection_pipeline(...)`.

   **Face landmarks (only the landmarks stage):**
   ```cpp
   hailo_analytics::analytics::face_landmarks::face_landmarks_config_t lm_cfg;
   lm_cfg.ai_config.hef_path        = "<hef path on board>";
   lm_cfg.post_config.function_name = "<postprocess function>";
   // lm_cfg.post_config.config_path left unset — no JSON for this task
   auto status = ...generate_face_landmarks_pipeline(LANDMARKS_PIPELINE, lm_cfg);
   ```

   **Face landmarks (whole bbox-crop+landmarks bundle):** use `bbox_crop_landmarks_config_t` and assign into `.landmarks_config.ai_config.hef_path` etc.

   **Segmentation / dynamic privacy mask:** `segmentation_config_t` or `bbox_crop_segmentation_config_t`, same field path (`.ai_config.hef_path`, `.post_config.function_name`).

   **OCR / LPR:** `ocr_config_t` or `bbox_crop_ocr_config_t`.

   The `*_config_t` fields are `std::optional<std::string>` — assign the bare string and the optional engages.

4. **Cross-compile.** Hand off to **/cross-compile** with the modified `main.cpp`.

5. **Deploy.** Hand off to **/deploy**. Only the binary needs replacement — the HEF and JSON usually already exist on the board image.

6. **Verify.** Run the app for ~10s and confirm no errors at startup. To confirm the AI stage actually produces output, subscribe to the ZMQ port (`tcp://<board-ip>:7000` by default) with the analytic_viewer tool and check the relevant payload (detections, landmarks, segmentation mask, OCR text, …).

## Common model picks (H15L)

### Detection (most common swap)

| Use case | HEF | function | json |
|---|---|---|---|
| General detection (COCO, fast) | `hailo_yolov8n_384_640.hef` | `hailo_yolov8n` | `yolov8n.json` |
| General detection (COCO, accurate) | `hailo_yolov8s_384_640.hef` | `hailo_yolov8s` | `yolov5.json` |
| General detection (COCO, even more accurate) | `hailo_yolov8m_384_640.hef` | `hailo_yolov8m` | `yolov5.json` |
| Person+face only (5 classes) | (not on default H15L image — needs push) | `yolov8n_personface` | `yolov5_personface.json` |

### Other tasks

- **Face landmarks** — defaults live in `face_landmarks.hpp`; postprocess function `facial_landmarks_nv12`; no JSON.
- **Segmentation / dynamic privacy mask** — defaults in `dynamic_privacy_mask.hpp`; postprocess function `linknet_post`; no JSON.
- **OCR / LPR** — defaults in `license_plate_recognition.hpp`.

For any non-detection swap, read the matching `analytics/<name>.hpp` (constants) and `analytics/<name>.cpp` (defaults, helper config builders) first — the override field path inside the `*_config_t` may differ from the simple `.ai_config / .post_config` pair when the task wraps multiple stages (e.g. `bbox_crop_landmarks_config_t.landmarks_config.ai_config.hef_path`).

## Delegate

- **model-expert** for "what HEFs are available", postprocess compatibility, ONNX → HEF questions.
- **pipeline-expert** if the swap requires reshaping the pipeline (e.g. moving from non-tiled to tiled, or adding a classifier after the detector).

## Gotchas

- **HEF input resolution must match the upstream stage's output.** For tiled detection the tiling stage emits 384×640 crops — only HEFs trained at 384×640 plug in cleanly. For non-tiled detection / landmarks / segmentation, the upstream bbox-crop or frontend dictates the size. A mismatched resolution either fails at pipeline construction or silently produces garbage. Note the `Input` resolution from `hailortcli parse-hef` (step 0) and compare against the upstream stage before continuing.
- **Postprocess function must match HEF tensor name.** Wrong pairing = silent zero output (no detections, no landmarks, empty mask), no error.
- **Don't override `iou_threshold` from the JSON** expecting it to take effect for `hailo_yolov*` — it doesn't.
- **H15H HEFs are NHWC, not NV12.** The media library expects NV12 frames at the AI stage; an unchanged NHWC H15H HEF will not work in a media-library pipeline. Always verify with `hailortcli parse-hef` (step 0) and warn the user.

## Offer to run

When this skill's work is complete, ask the user (AskUserQuestion) whether they want to run the app now and see it live. If yes, invoke the **/run-app** skill.
