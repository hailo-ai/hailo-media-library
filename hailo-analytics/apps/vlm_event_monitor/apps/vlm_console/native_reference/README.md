# Native VLM reference

Host-side replayer for the on-device `vlm_event_monitor`. Runs the **same VLM
the Hailo HEF was compiled from** — through the native PyTorch checkpoint
(HuggingFace Transformers) — so you can diff the on-device output against a
reference model on your dev box.

Supports both Qwen2-VL and Qwen3-VL families (and any other
`AutoModelForVision2Seq`-compatible checkpoint). By default the model is
auto-derived from the metadata's `vlm_config.hef_path`; an explicit
`--model-id` overrides it.

Two operating modes:

-   **`replay`** — point at a debug-cycle folder (or a parent of cycles) and the
    script replays every inference in order through the native model, writing a
    `native_metadata.json` sidecar next to each input `metadata.json` and
    printing a per-inference comparison summary.
-   **`repl`** — interactive shell that mirrors the `vlm_console` command set
    (`new`/`close`/`infer`/`oneshot`/`events`/...), but with the native model
    underneath. Useful for ad-hoc prompts and event lists.

## Install

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
```

First model load downloads ~4 GB to `~/.cache/huggingface/`. Override with
`HF_HOME=/path/to/cache` if needed.

## Usage

```bash
# Replay one cycle (path contains metadata.json)
python vlm_native.py replay \
    /path/to/Temp/performance_debug/2018-03-09_16-14-20-001

# Replay all cycles under a parent (auto-detected)
python vlm_native.py replay /path/to/downloaded/event/from/frontend

# Interactive REPL
python vlm_native.py repl
```

Common flags (all subcommands):

| Flag                      | Default              | Notes                                                                                                                                                                                                                                                                                           |
| ------------------------- | -------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `--model-id`              | _(auto-detected)_    | HF model id. Replay derives from metadata's `hef_path`; REPL falls back to `Qwen/Qwen2-VL-2B-Instruct`. Pass explicitly to override.                                                                                                                                                            |
| `--device`                | `auto`               | `auto` picks CUDA if available, else CPU                                                                                                                                                                                                                                                        |
| `--dtype`                 | `auto`               | `auto` = bf16 on CUDA, fp32 on CPU                                                                                                                                                                                                                                                              |
| `--no-strict-determinism` | off (i.e. strict ON) | See "Determinism" below                                                                                                                                                                                                                                                                         |
| `--keep-default-system`   | off (i.e. stripped)  | Keep the chat template's auto-injected default-system block. Off matches the Hailo HEF path.                                                                                                                                                                                                    |
| `--video-fps`             | `0.5`                | Effective fps of the dumped frames. Qwen3-VL uses this for frame-temporal embedding _and_ inlines per-frame textual timestamps in the prompt. Default `0.5` matches Hailo's on-device sampling (1 FPS source, frames 2 s apart in both Performance and Accuracy modes). Qwen2-VL is unaffected. |

## Supported models

Replay maps the HEF filename stem in `vlm_config.hef_path` to a HuggingFace
model id via this table (see `KNOWN_HEF_TO_HF` in `replay.py`):

| HEF stem               | HuggingFace model id        |
| ---------------------- | --------------------------- |
| `Qwen2-VL-2B-Instruct` | `Qwen/Qwen2-VL-2B-Instruct` |
| `Qwen3-VL-2B-Instruct` | `Qwen/Qwen3-VL-2B-Instruct` |

Unmapped stems fall back to `Qwen/<stem>` (works for any other Qwen-family
HEF that follows the same naming convention).

### Override examples

```bash
# Replay against Qwen3-VL even though metadata says Qwen2-VL HEF
python vlm_native.py replay <cycle_dir> --model-id Qwen/Qwen3-VL-2B-Instruct

# REPL with Qwen3-VL as the underlying model
python vlm_native.py repl --model-id Qwen/Qwen3-VL-2B-Instruct
```

### Cross-family caveat

When `--model-id` is set to a _different_ family than the HEF's source
model, the native↔Hailo "agreement" count is comparing **two different
models**, not just a precision/quantization gap. Disagreements then mix
three sources:

1. The model-family difference (e.g. Qwen2-VL ↔ Qwen3-VL behavioral
   differences trained from different data).
2. The quantization gap (HEF int8/int4 vs native fp32/bf16).
3. The image preprocessing pipeline differences (HEF compile-time
   normalization vs `AutoProcessor`'s runtime normalization).

For the cleanest precision-only signal, leave `--model-id` unset so the
native side replays against the same family the HEF was compiled from.

`replay`-only flags:

| Flag                             | Default | Notes                                                               |
| -------------------------------- | ------- | ------------------------------------------------------------------- |
| `--output {sidecar,stdout,both}` | `both`  | `sidecar`: write `native_metadata.json` only. `stdout`: print only. |

## What gets compared

For each inference in `metadata.json`:

-   **`kind: "description"`** — free text. Both responses are stored side by
    side; no automatic agreement check (Hailo and native almost always phrase
    things differently).
-   **`kind: "event"`** (Accuracy mode follow-ups) — yes/no parsed from the
    native response with the same parser the C++ side uses (faithful port of
    `apps/vlm_console/main.cpp:229-372`). Native vs Hailo verdict compared
    → `yes_match: true|false`.
-   **`kind: "performance_aggregate"`** (Performance mode) — multi-event
    yes/no parsed; per-event match flags emitted as `verdicts_match[]`.

## Determinism

Strict determinism is **on by default**. The runner sets, at startup:

```python
os.environ["CUBLAS_WORKSPACE_CONFIG"] = ":4096:8"
torch.use_deterministic_algorithms(True)
torch.manual_seed(0)
```

Why this is the default: `do_sample=False` (greedy decoding) eliminates the
sampling RNG, but on CUDA many matmul/scatter/reduction kernels still use
atomic FP ops whose execution order depends on thread-block scheduling. Two
runs of the same input on the same GPU can occasionally diverge at token
positions where top-1 and top-2 logits are close. For a comparison tool that
ambiguity is the worst kind of noise — you can't tell if a Hailo↔native
disagreement is a real HEF artifact or just native-side FP jitter. Strict
mode pins the native side so all observed disagreements are attributable to
the HEF / on-device preprocessing path.

What strict determinism does **not** give you: native ↔ Hailo identity. The
HEF is a quantized port of Qwen2-VL — quantization shifts logits in ways no
host-side flag can offset. Per-cycle disagreements are expected; this tool
is built to surface and quantify them.

If a deterministic op isn't implemented for an op Qwen2-VL uses (rare in
practice), `model.generate()` raises a `RuntimeError`. The runner catches
this and prints a clear "re-run with `--no-strict-determinism`" message
instead of dying inside a thread.

CPU runs are deterministic regardless of the flag.

## Sidecar JSON shape

The sidecar is named **`native_metadata_<model_slug>.json`** (e.g.
`native_metadata_qwen2_vl_2b_instruct.json`,
`native_metadata_qwen3_vl_2b_instruct.json`) so multiple replays of the same
cycle against different models land in separate files instead of overwriting
each other. The slug is derived from the model id used by the runner —
either the metadata-derived default or whatever was passed via `--model-id`.

The sidecar is a superset of the input `metadata.json`. Each inference dict
gets new fields:

```json
{
  "...": "all original Hailo fields preserved",
  "response_hailo": "1. No\n2. Yes\n3. Yes\n",
  "response_native": "1. No\n2. Yes\n3. No\n",
  "native_stats": {
    "total_ms": 1234.5,
    "ttft_ms": 234.0,
    "tokens_generated": 11,
    "context_tokens": 368
  },
  "verdicts_native": [{"id":1,"detected":false}, ...],
  "verdicts_match":  [true, true, false]
}
```

Per-cycle, an additional top-level `replay` block is added:

```json
"replay": {
  "schema_version": 2,
  "source_metadata": "metadata.json",
  "native_model_id": "Qwen/Qwen2-VL-2B-Instruct",
  "native_model_overridden": false
}
```

`native_model_id` is the model the runner actually loaded.
`native_model_overridden` is `true` when the user passed `--model-id`
explicitly, `false` when it was derived from the metadata.

## File layout

```
native_reference/
  vlm_native.py          # CLI entry point (replay / repl subcommands)
  native_runner.py       # NativeVlmRunner: model + session management
  replay.py              # cycle discovery + replay + sidecar emission
  yesno_parser.py        # faithful port of the C++ parser
  test_yesno_parser.py   # unit tests for the parser
  requirements.txt
  README.md              # this file
```

## Running tests

```bash
python3 -m pytest test_yesno_parser.py
# or, without pytest:
python3 -c "import test_yesno_parser as t, inspect; \
  [getattr(t, n)() for n, _ in inspect.getmembers(t, inspect.isfunction) if n.startswith('test_')]; \
  print('OK')"
```
