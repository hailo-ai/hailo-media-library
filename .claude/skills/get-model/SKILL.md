---
name: get-model
description: Propose and download a HEF from the public Hailo Model Zoo — either a swap for the model currently in a pipeline, or a fresh pick for a new pipeline. Use when the user says "find me a model for X", "what can I use instead of yolov8s", "swap to something more accurate / higher FPS / lower-latency", or "I want to add a face-recognition stage". Picks per H15L/H15H and curls the HEF.
tools: WebFetch, Bash, Read, Agent
---

# /get-model — pick a HEF from the model zoo and download it

The public model zoo lists, per board (H15L / H15H) and task, all the compiled HEFs Hailo ships, with their **accuracy** (Float mAP / Hardware mAP), **throughput** (FPS @ batch 1/8), **input resolution**, **params (M)** and **OPS (G)**. Per-model **latency** is not in the table — it lives in a separate profile HTML linked as **PR** next to each HEF (see step 3 below). `/get-model` reads that table, narrows it to a few good candidates given the user's preference, and downloads the chosen HEF.

What the user preferences map to, concretely:
- **faster** = higher FPS, or lower latency (slower = lower FPS / higher latency)
- **more accurate** = higher mAP (less accurate = lower mAP)

## Inputs to ask the user

- **Board**: `h15l` or `h15h`. If `/connect` was run earlier, derive from `uname -a` (`hailo15l` → h15l). Otherwise ask.
- **Mode**:
  - *Swap* — replace the model currently used in a known pipeline (e.g. detection: `yolov8s` → ?). Need the current model name.
  - *New* — pick a model for a task not in the current pipeline (e.g. face_recognition, pose_estimation).
- **Preference**: "more accurate" / "faster (higher FPS)" / "lower latency" / "balanced". Don't ask if obvious from the user's request.
  - *faster* = higher FPS (and lower-is-better latency, when also relevant)
  - *more accurate* = higher mAP
  - FPS and mAP come from the table (step 2). Latency requires fetching the per-model profile HTML (step 3).

## Procedure

1. **Map task → modelzoo doc page.** Task pages live at:
   ```
   https://github.com/hailo-ai/hailo_model_zoo/blob/master/docs/public_models/HAILO15<L|H>/HAILO15<L|H>_<task>.rst
   ```
   `<task>` is one of: `classification`, `depth_estimation`, `face_attribute`, `face_detection`, `face_recognition`, `facial_landmark_detection`, `hand_landmark_detection`, `image_denoising`, `instance_segmentation`, `low_light_enhancement`, `object_detection`, `oriented_object_detection`, `person_attribute`, `person_re_id`, `pose_estimation`, `semantic_segmentation`, `single_person_pose_estimation`, `super_resolution`, `text_image_retrieval`, `text_recognition`, `video_classification`, `zero_shot_classification`.

2. **Fetch the table.** `WebFetch` the page and extract the model rows: Network Name, Float mAP, Hardware mAP, FPS (B1), FPS (B8), Input Resolution, Params (M), OPS (G). Note: per-model **latency is not in this table** — it comes from step 3.

3. **(Only if the user cares about latency) Fetch the per-model profile HTML.** Each row has a **PR** link next to the **HEF** link that points to a profiler results page. The URL pattern is:
   ```
   https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v<sdk-version>/hailo15<l|h>/<model_name>_profiler_results_compiled_runtime_data.html#/model-details
   ```
   Example for yolov8n on H15L (v5.3.0):
   ```
   https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v5.3.0/hailo15l/yolov8n_profiler_results_compiled_runtime_data.html#/model-details
   ```
   `WebFetch` that URL. The default page is **Details / Batch 1**, and the **latency** value (e.g. `5.02 ms`) is what you want. Fetch it for each shortlist candidate so you can compare latencies side by side. Skip this step if the user only cares about mAP / FPS — it's only needed when latency is the actual axis.

4. **Pick 2–3 candidates** for the user's preference. Bias by this order, because it minimises downstream work:
   - **Same family, different size** (preferred): `yolov8s → yolov8n` (faster/smaller) or `yolov8s → yolov8m / yolov8l` (more accurate). Same architecture = same postprocess function + JSON usually work unchanged.
   - **Newer architecture, same task** (secondary): `yolov8s → yolov11s` / `yolov26s`. Often a better profile on whichever axis the user cares about, but postprocess function and JSON may differ — flag that to the user.
   - Match the input resolution if pipeline geometry is fixed (e.g. tiling produces 384×640; only models with a 384×640 variant fit without re-tiling).

5. **Present the shortlist.** For each candidate, one line including only the metrics the user actually asked about:
   - faster → FPS@B1
   - lower latency → latency (ms) from step 3
   - more accurate → mAP
   Plus a one-sentence note ("same family, ~2× FPS", "same family, ~1.5pt mAP drop", "newer arch, ~30% lower latency", …). Wait for the user to pick.

6. **Download the HEF.** Public S3 URL pattern:
   ```
   https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v<sdk-version>/hailo15<l|h>/<model_name>.hef
   ```
   Default `<sdk-version>` is the latest the doc page advertises (e.g. `v5.3.0`). Save to `./hefs/<model_name>.hef`:
   ```bash
   mkdir -p ./hefs
   curl -fL -o ./hefs/<model_name>.hef \
     https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v<sdk-version>/hailo15<l|h>/<model_name>.hef
   ```
   On 404, check the SDK version — older HEFs sometimes only exist under earlier `v*` folders.

7. **Tell the user what's next.**
   - *Swap*: hand off to **/swap-model** with the new HEF path and the model family (so the right postprocess function + JSON get wired in).
   - *New pipeline*: hand off to **/edit-pipeline** with the HEF + task name.
   Mention that **/deploy** will be needed to push the HEF to the board (default destination is the app's `resources/` dir).

## Gotchas

- **Board mismatch will fail.** An H15H HEF won't run on H15L (different compiler target). Always pick from the matching `HAILO15<L|H>` folder.
- **Architecture switch ≠ drop-in.** `yolov8 → yolov11` changes the tensor output name → the postprocess function name (`hailo_yolov8s` → `hailo_yolov11s`) and sometimes the JSON keys. `/swap-model` knows about this; warn the user the rebuild is non-trivial.
- **Input resolution must match the pipeline.** Tiling detection emits 384×640 crops — only HEFs trained at 384×640 plug in cleanly. A 640×640 HEF needs the tiling stage re-sized.
- **FPS in the table is the model alone** (no pre/post, batch as listed). End-to-end pipeline FPS will be lower. Use it for comparison between models, not as an absolute throughput estimate.


## Quick examples

- *"Swap detection from yolov8s to something faster (higher FPS) on H15L"* → use the table; shortlist `yolov8n` (same family, ~2× FPS@B1) and `yolov6n` (different family, similar FPS). Skip step 3 — latency not asked for. Download `yolov8n.hef`, hand off to `/swap-model`.
- *"Swap detection to something with lower latency on H15L"* → use the table to shortlist 2–3 candidates, then `WebFetch` each candidate's `<model>_profiler_results_compiled_runtime_data.html#/model-details` page and read the Batch 1 latency. Present the shortlist with latency (ms) per candidate. Download chosen, hand off to `/swap-model`.
- *"Add face recognition to the face-landmarks pipeline on H15L"* → fetch `HAILO15L_face_recognition.rst`, shortlist `arcface_mobilefacenet` (high FPS) and `arcface_r50` (higher mAP). Download chosen HEF, hand off to `/edit-pipeline`.

