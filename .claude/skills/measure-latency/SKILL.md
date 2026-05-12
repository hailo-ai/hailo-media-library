---
name: measure-latency
description: Measure pipeline latency on H15 from a Perfetto trace recorded by `hailo-soc-profiler applications` — vision latency (ISP → first analytics stage), per-stage latency, and E2E latency (ISP → ZMQ/sink). Use when the user asks "how long does a frame take", "what's the analytics latency", "why is FPS fine but feels laggy", or hands over a `.trace` file. Reads the trace locally with Perfetto's `trace_processor`; no board interaction needed once the trace is captured.
tools: Bash, Read, Agent
---

# /measure-latency — extract per-stage and end-to-end latency from a SoC-profiler trace

The H15 SoC Profiler is a Hailo fork of Perfetto. `hailo-soc-profiler applications -t Ns -o /tmp/soc.trace` records every hailo-analytics stage as a `processing_<stage_name>` slice on the `Hailo Analytics > Processing` track, with the buffer's `isp_timestamp_ms` (millisecond ISP capture time) attached as a debug arg. Latency = `slice.ts - isp_timestamp_ms` for whichever stage you pick as the endpoint.

Authoritative references:
- **SoC Profiler User Guide** — Confluence `MSW/SoC Profiler User Guide` (pageId `2190999554`). Covers `hailo-soc-profiler` flags, the predefined configs (`applications`, `applications_detailed`, `noc-bandwidth-*`, `sched`, `mem_tracker`), and the web UI.
- **Expected Latency** — Confluence `TAP/Expected Latency` (pageId `2965733440`). Defines Vision / 1st-Stage / 2nd-Stage / 1st-Stage+Crop latency and shows the manual extraction recipe (click a slice → "Start time: raw (ns)" → subtract `isp_timestamp_ms × 1e6`).
- **Hailo OS User Guide v1.11.0** — `~/hailo/documentation/vpu/hailo_os_user_guide_1.11.0.pdf` §6.1 ("Hailo-15 Profiler") for the on-board tooling.

## Inputs you need

- A `.trace` file from `hailo-soc-profiler` (typically captured with the `applications` config). Either local or on the board.
- *Optional* — which endpoint counts as "frame done":
  - `processing_zmq_sender_stage` (default for face-recognition / analytics-only flows)
  - `processing_udp_sink<N>` (vision out)
  - `processing_landmarks_post`, `processing_face_recognition`, etc.

If the user hands you a trace path, skip straight to step 3.

## Procedure

### 1. (Optional) Capture a trace on the board

Only if the user doesn't already have one — and the board is connected (use **/connect** first):

```bash
ssh root@10.0.0.1 'hailo-soc-profiler applications -t 5s -o /tmp/soc.trace'
scp root@10.0.0.1:/tmp/soc.trace ./
```

5 s is enough to get ~150 frames at 30 FPS. Use `applications_detailed` only when the user asks for sub-stage breakdown — it grows the file noticeably and perturbs timing.

### 2. Make sure `trace_processor` is available

Perfetto's CLI ships as a tiny Python wrapper that pulls the native binary on first run:

```bash
[ -x /tmp/trace_processor ] || { curl -sSL https://get.perfetto.dev/trace_processor -o /tmp/trace_processor && chmod +x /tmp/trace_processor; }
```

First invocation does the binary download; later runs are instant. The trace files in `~/hailo/vpu/claude_integration/latency_measurements/` are ground-truth Perfetto traces — useful for dry-run testing.

### 3. Extract the latency table

**Collapse per-frame first.** Some stages emit **one slice per frame** (tiling, aggregators, sinks, zmq_sender) but others emit **one slice per tile/face** — e.g. `processing_detection_stage` and `processing_face_landmarks` produce 5 slices per frame when 5 faces are tracked. Averaging the raw `slice` table mixes per-frame and per-tile granularity. Always group by `isp_timestamp_ms` first to get a single "this stage's footprint for this frame" pair (entered = MIN(ts), exited = MAX(ts+dur)), then aggregate across frames:

```bash
/tmp/trace_processor <trace_path> -q /dev/stdin <<'EOF'
WITH per_frame AS (
  SELECT a.int_value AS isp_ms, s.name AS stage,
         MIN(s.ts)/1e6 AS first_ms,
         MAX(s.ts+s.dur)/1e6 AS last_ms
  FROM slice s JOIN args a ON s.arg_set_id=a.arg_set_id
  WHERE a.key='debug.isp_timestamp_ms' AND s.name LIKE 'processing_%'
  GROUP BY a.int_value, s.name
)
SELECT stage,
       COUNT(*) AS frames,
       ROUND(AVG(first_ms - isp_ms), 1) AS entered_avg_ms,
       ROUND(AVG(last_ms  - isp_ms), 1) AS exited_avg_ms,
       ROUND(PERCENTILE(first_ms - isp_ms, 95), 1) AS entered_p95_ms
FROM per_frame
GROUP BY stage
ORDER BY entered_avg_ms;
EOF
```

**How to read it**: rows sort by when the stage first sees the frame. The **first row** is the Vision Latency (typically `processing_tiling_stage`, ISP → tiling start) and the **last row** is the E2E latency to that endpoint (typically `processing_zmq_sender_stage`). The gap `exited_avg − entered_avg` is the stage's wall-clock span for one frame (across its tiles/faces if any).

Quick diagnostic — how many slices each stage emits per frame, to know which stages are tile-fanned-out:

```bash
/tmp/trace_processor <trace_path> -q /dev/stdin <<'EOF'
SELECT s.name AS stage,
       COUNT(DISTINCT a.int_value) AS frames,
       COUNT(*) AS slices,
       ROUND(1.0 * COUNT(*) / COUNT(DISTINCT a.int_value), 2) AS slices_per_frame
FROM slice s JOIN args a ON s.arg_set_id=a.arg_set_id
WHERE a.key='debug.isp_timestamp_ms' AND s.name LIKE 'processing_%'
GROUP BY s.name ORDER BY slices_per_frame DESC;
EOF
```

Stages with `slices_per_frame > 1` are the per-tile / per-face stages (typical: `detection_stage`, `detection_post`, `face_landmarks`, `landmarks_post`). The per-frame query above already handles them correctly.

### 4. Map to canonical metrics

From the Expected-Latency wiki recipe, these are the names customers ask for:

| Metric | How to compute |
|---|---|
| **Vision Latency** | `processing_tiling_stage` avg (ISP → tiling start). Includes the ISP → DDR write and the front-end pipeline, excludes AI-ISP denoise. |
| **1st-Stage Latency** | `processing_detection_stage.start - processing_tiling_stage.start` — i.e. tiling + first inference. *Include tiling, exclude crop* per the wiki. |
| **1st-Stage + Crop** | `processing_bbox_crops.start - processing_tiling_stage.start`. |
| **2nd-Stage Latency** | `processing_face_landmarks.start - processing_bbox_crops.end` (or whichever 2nd-stage variant is in the trace). |
| **Analytics Latency** | `processing_zmq_sender_stage.start − processing_tiling_stage.start`. **This is the "latency (ms)" number quoted in the "Measured latency of the face recognition pipeline" wiki table** — it excludes Vision and AI-ISP. Use this as the headline number when reproducing wiki figures. |
| **E2E (ISP-relative)** | the avg of the last `processing_*` row in the per-stage table (e.g. `processing_zmq_sender_stage`). Includes Vision Latency. |
| **AI-ISP / Denoise** | the `Inference` slice on the MediaLibrary→Denoise track, or the `denoise latency (ms)` counter. Avg is typically 30–75 ms depending on profile (lowlight is slower). This is **only the NN compute** — the wiki's "+400 ms" estimate covers a wider envelope (sensor exposure + ISP front-end + buffering + denoise) that is *not* otherwise in the trace. |
| **Sensor → ZMQ (full pipeline)** | not directly measurable — see "Full-pipeline composition" below. |

To compute 1st-/2nd-stage spans precisely (not just relative to ISP), join slices by frame. Frames share a common `isp_timestamp_ms`:

```bash
/tmp/trace_processor <trace_path> -q /dev/stdin <<'EOF'
-- Per-frame: take MIN(ts) for each (frame, stage) pair so tile/face fan-out collapses cleanly.
WITH per_frame AS (
  SELECT a.int_value AS isp_ms, s.name AS stage, MIN(s.ts)/1e6 AS first_ms
  FROM slice s JOIN args a ON s.arg_set_id=a.arg_set_id
  WHERE a.key='debug.isp_timestamp_ms' AND s.name LIKE 'processing_%'
  GROUP BY a.int_value, s.name
)
SELECT
  ROUND(AVG(det.first_ms  - tile.first_ms),1) AS first_stage_ms,
  ROUND(AVG(crop.first_ms - tile.first_ms),1) AS first_plus_crop_ms,
  ROUND(AVG(land.first_ms - crop.first_ms),1) AS second_stage_ms
FROM per_frame tile
JOIN per_frame det  USING (isp_ms)
JOIN per_frame crop USING (isp_ms)
JOIN per_frame land USING (isp_ms)
WHERE tile.stage='processing_tiling_stage'
  AND det.stage ='processing_detection_stage'
  AND crop.stage='processing_bbox_crops'
  AND land.stage='processing_face_landmarks';
EOF
```

### Full-pipeline composition (sensor → ZMQ)

The Perfetto trace can't measure all the way back to the sensor — `isp_timestamp_ms` is stamped only when the ISP delivers the buffer to memory, *after* sensor exposure, readout, ISP front-end, and denoise pipelining. Two honest "sensor → ZMQ" numbers depending on what you mean by AI-ISP:

```bash
/tmp/trace_processor <trace_path> -q /dev/stdin <<'EOF'
-- Denoise NN inference duration (just the compute portion of AI-ISP)
SELECT 'denoise_inference_ms' AS metric,
       COUNT(*) AS frames,
       ROUND(AVG(dur)/1e6, 1) AS avg_ms,
       ROUND(MAX(dur)/1e6, 1) AS max_ms,
       ROUND(PERCENTILE(dur/1e6, 95), 1) AS p95_ms
FROM slice WHERE name='Inference';
EOF
```

Then combine with the analytics number from the per-stage query:

| Definition | Computation | Notes |
|---|---|---|
| **Trace lower bound** (NN compute only) | `denoise_inference_avg + isp_to_zmq_end_avg` | Honest, narrow — what the trace can actually prove. Useful for debugging where time goes. |
| **Customer-quoted** (wiki AI-ISP envelope) | `400 + isp_to_zmq_end_avg` (or `400 + analytics_avg + vision_avg`) | What customers feel as user-visible latency. The 400 ms is the wiki's typical AI-ISP envelope including sensor + ISP frontend + buffering, none of which are in the trace. |

For `pluto_678_4k_ll_r50_5_faces.trace` (lowlight 4K r50, 5 faces): denoise NN = 45 ms avg, ISP→ZMQ = 1851 ms avg → trace lower bound = ~1896 ms, customer-quoted = ~2252 ms.

## Output format

Three short blocks. Keep numbers — drop everything else from the raw query output:

```
Trace: <file> (<duration> s, <frame_count> frames)
Pipeline endpoint: <last processing_* stage>

Latency (avg / p95):
  Denoise NN inference          <a> / <b> ms     [Inference slice on MediaLibrary→Denoise]
  Vision (ISP → tiling)         <a> / <b> ms
  1st stage (tiling → det)      <a> / <b> ms     [includes tiling, excludes crop]
  1st + crop                    <a> / <b> ms
  2nd stage (crop → landmarks)  <a> / <b> ms
  Analytics (tiling → ZMQ)      <a> / <b> ms     ← matches wiki "Measured latency" column
  E2E (ISP → ZMQ end)           <a> / <b> ms

Full pipeline (sensor → ZMQ):
  Trace lower bound  (denoise NN + E2E)              ~<E2E + denoise_nn> ms
  Customer-quoted    (wiki +400 ms AI-ISP envelope)  ~<E2E + 400> ms
```

If anything looks abnormal — Vision > 30 ms, E2E p95 > 2× avg (jitter), a stage with zero frames — call it out as the **first** line.

## Gotchas

- **`isp_timestamp_ms` is in milliseconds; `slice.ts` is in nanoseconds.** Always `s.ts/1e6 - a.int_value`. The wiki tells you to set Perfetto-UI's time picker to **Raw** for the same reason — `ms` view in the UI counts from app start, not from boot.
- **The trace's Denoise NN inference is *not* the full AI-ISP latency.** The `Inference` slice on MediaLibrary→Denoise (also surfaced as the `denoise latency (ms)` counter) measures only the NN compute — typically 30–75 ms. The wiki's "+400 ms" AI-ISP figure is broader: it covers sensor exposure + ISP front-end + denoise pipelining + buffering, all *before* `isp_timestamp_ms` is stamped. Use the wiki figure when quoting user-visible latency; use the trace's denoise value when debugging where time goes.
- **`isp_timestamp_ms` is the buffer's *post-ISP* timestamp, not sensor capture.** Anything before that point (sensor exposure, readout, ISP front-end) is invisible to the trace. There is no slice with an actual sensor-clock timestamp emitted by the H15 today.
- **`hailo-soc-profiler` is missing on production images.** It ships only on `core-image-minimal` and `core-image-hailo-dev`. If the user can't find it on the board, they're on the wrong image (OS Guide §6.1).
- **`applications_detailed` perturbs timing**, especially on heavier pipelines. Only use it when the user explicitly wants sub-stage events.
- **Stage names vary by app.** Face landmarks uses `processing_tiling_stage / processing_detection_stage / processing_bbox_crops / processing_face_landmarks / processing_zmq_sender_stage`. Face recognition adds `processing_face_recognition`. Detection-only apps have no `processing_bbox_crops`. Always list the actual `processing_*` stage names from the trace first, then build the metric expression around what's there.
- **`processing_*` events exist only if the app was built with `HAVE_PERFETTO`.** All shipped hailo-analytics apps are. A custom app that pre-dates the hailo-analytics SDK may have nothing — fall back to `udp_sink` or kernel trace events.
- **`-q /dev/stdin` only accepts SQL.** Perfetto dot-commands (`.mode column`, `.header on`) are sqlite-shell-only and will fail with `syntax error`. Use raw `SELECT`s.
- **Trace_processor downloads its native binary on first run** (the file at `/tmp/trace_processor` is a ~10 KB Python wrapper). First query takes ~5 s longer; later ones are sub-second.
- **One trace = one snapshot.** For latency-vs-load characterization, record several traces under controlled stress (e.g. swap profiles `Daylight_Bayer` ↔ `Lowlight_Bayer`, vary face count) and compare. The `~/hailo/vpu/claude_integration/latency_measurements/` corpus is exactly that — sweep across sensor/profile/resolution/model/face-count.

## When to delegate / dig deeper

- **/board-status** first if FPS is also low — die throttling and CMA pressure look like latency at the pipeline level.
- **doc-explorer** for OS Guide §6.1 internals (the `tracebox` consumer/producer model, custom Perfetto config protobufs, the NoC counter producer).
- **/explain-pipeline** to map `processing_<name>` slice names back to the C++ stage that emits them — useful when the trace has a stage you don't recognise.
- **Perfetto Web UI** (`http://10.41.100.153/` internally, or run `run_ui.sh` from the hailo-fork repo locally) for visual frame-by-frame inspection. Use it when the user asks "show me a frame" rather than "give me numbers".
