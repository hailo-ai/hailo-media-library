"""Replay an on-device debug cycle through a native VLM reference.

Auto-detects between:
  * single-cycle mode  — <path>/metadata.json exists
  * batch mode         — <path> contains immediate subdirs each holding their
                         own metadata.json (e.g. accuracy_debug/ as a parent)

For each cycle:
  1. Read metadata.json. Verify the recorded vlm_config matches what the
     native runner can faithfully reproduce (336x336 RGB, do_sample=false).
  2. Load all frames listed in `frames[]` as PIL Images. They are already
     336x336 (the on-device app dumps post-letterbox); pass through unchanged.
  3. Walk `inferences[]` in order on a single native session — accuracy mode
     keeps context across the description + per-event yes/no follow-ups.
  4. Capture native response, tokens, timings; compute yes/no agreement vs
     the Hailo-recorded verdicts.
  5. Write native_metadata.json sidecar; print a per-inference summary line
     and an aggregate cycle line.

Model selection:
  * If the caller passes an explicit model_id → use it (override path).
  * Otherwise derive from the first cycle's `vlm_config.hef_path` via
    `KNOWN_HEF_TO_HF`. For batch runs this means "use the model that the
    first cycle's HEF was compiled from"; later cycles in the batch that
    imply a different model trigger a warning but continue.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from PIL import Image

from native_runner import InferenceResult, NativeVlmRunner
from yesno_parser import parse_yesno, verdicts_to_dicts


# Maps the *stem* of vlm_config.hef_path (e.g. "Qwen2-VL-2B-Instruct") to a
# HuggingFace model id. New compatible models go here. For unmapped stems we
# fall back to "Qwen/<stem>" (a heuristic that holds for Qwen-family HEFs).
KNOWN_HEF_TO_HF = {
    "Qwen2-VL-2B-Instruct": "Qwen/Qwen2-VL-2B-Instruct",
    "Qwen3-VL-2B-Instruct": "Qwen/Qwen3-VL-2B-Instruct",
}

DEFAULT_MODEL_ID_FALLBACK = "Qwen/Qwen2-VL-2B-Instruct"


def model_id_to_filename_slug(model_id: str) -> str:
    """Make a filename-safe slug from a HuggingFace model id.
    Examples:
      "Qwen/Qwen2-VL-2B-Instruct" → "qwen2_vl_2b_instruct"
      "Qwen/Qwen3-VL-2B-Instruct" → "qwen3_vl_2b_instruct"
      "some-org/Custom.Model v2"  → "custom_model_v2"
    Used to disambiguate native_metadata sidecars when replaying the same
    cycle against multiple model variants (Qwen2 vs Qwen3, etc.) so each
    run lands in its own file instead of overwriting the previous one."""
    tail = model_id.rsplit("/", 1)[-1].lower()
    safe_chars = []
    prev_underscore = False
    for ch in tail:
        if ch.isalnum():
            safe_chars.append(ch)
            prev_underscore = False
        else:
            if not prev_underscore:
                safe_chars.append("_")
                prev_underscore = True
    slug = "".join(safe_chars).strip("_")
    return slug or "model"


def derive_model_id_from_metadata(metadata: dict) -> str:
    """Pick a HuggingFace model id from a cycle's metadata. Strips the .hef
    extension off `vlm_config.hef_path`, looks the stem up in KNOWN_HEF_TO_HF,
    and falls back to "Qwen/<stem>" if unmapped (or the default if missing)."""
    hef_path = metadata.get("vlm_config", {}).get("hef_path", "")
    stem = Path(hef_path).stem if hef_path else ""
    if stem in KNOWN_HEF_TO_HF:
        return KNOWN_HEF_TO_HF[stem]
    if stem:
        return f"Qwen/{stem}"
    return DEFAULT_MODEL_ID_FALLBACK


@dataclass
class ReplayContext:
    """Run-wide info recorded in every cycle's sidecar."""
    native_model_id: str
    native_model_overridden: bool


@dataclass
class CycleSummary:
    cycle_dir: Path
    total_inferences: int = 0
    agreements: int = 0
    disagreements: int = 0
    description_inferences: int = 0      # not counted toward agree/disagree
    disagree_details: list[str] = field(default_factory=list)


# ── Cycle discovery ─────────────────────────────────────────────────────────

def find_cycles(path: Path) -> list[Path]:
    """Auto-detect single-cycle vs parent-batch."""
    if (path / "metadata.json").is_file():
        return [path]
    if not path.is_dir():
        raise FileNotFoundError(f"path is neither a cycle nor a parent: {path}")
    cycles = sorted(
        child for child in path.iterdir()
        if child.is_dir() and (child / "metadata.json").is_file()
    )
    if not cycles:
        raise FileNotFoundError(
            f"no metadata.json found under {path} (single-cycle or batch)"
        )
    return cycles


# ── Per-cycle replay ────────────────────────────────────────────────────────

def replay_cycle(
    runner: NativeVlmRunner,
    cycle_dir: Path,
    context: ReplayContext,
    output_mode: str = "both",      # "sidecar" | "stdout" | "both"
) -> CycleSummary:
    metadata_path = cycle_dir / "metadata.json"
    metadata = json.loads(metadata_path.read_text())

    _check_vlm_config_compatibility(metadata, cycle_dir)

    frames_paths = [cycle_dir / name for name in metadata["frames"]]
    frames = [Image.open(p).convert("RGB") for p in frames_paths]

    summary = CycleSummary(cycle_dir=cycle_dir)
    session_id = runner.new_session()
    output_inferences: list[dict] = []

    try:
        for index, inference in enumerate(metadata["inferences"]):
            kind = inference.get("kind", "unknown")
            attached = inference.get("frames_attached", [])
            use_video_mode = bool(inference.get("use_video_mode", False))
            prompt = inference.get("prompt", "")
            max_tokens = int(inference.get("max_tokens", 256))

            attached_frames = [frames[i] for i in attached]

            if output_mode != "sidecar":
                _print_pre_inference_banner(index, inference, len(attached_frames))

            result = runner.infer(
                session_id=session_id,
                frames=attached_frames,
                use_video_mode=use_video_mode,
                prompt=prompt,
                max_new_tokens=max_tokens,
                stream_to_stdout=False,
            )

            comparison = _compare_responses(inference, result, kind)
            output_inferences.append(_build_output_inference(inference, result, comparison))

            if kind == "description":
                summary.description_inferences += 1
            else:
                summary.total_inferences += 1
                if comparison["agreed"]:
                    summary.agreements += 1
                else:
                    summary.disagreements += 1
                    summary.disagree_details.append(
                        _disagreement_label(index, kind, comparison)
                    )

            if output_mode != "sidecar":
                _print_post_inference_summary(result, comparison)
    finally:
        runner.close_session(session_id)

    if output_mode != "stdout":
        slug = model_id_to_filename_slug(context.native_model_id)
        sidecar_path = cycle_dir / f"native_metadata_{slug}.json"
        sidecar_path.write_text(
            json.dumps(_build_sidecar(metadata, output_inferences, context), indent=2)
        )
        if output_mode != "sidecar":
            print(f"  → wrote {sidecar_path}")

    if output_mode != "sidecar":
        _print_cycle_footer(summary)

    return summary


def replay_path(
    path: Path,
    *,
    model_id: Optional[str] = None,
    device: str = "auto",
    dtype: str = "auto",
    strict_determinism: bool = True,
    suppress_default_system: bool = True,
    effective_video_fps: float = 0.5,
    output_mode: str = "both",
) -> list[CycleSummary]:
    """Discover cycles, decide which native model to load (override or
    metadata-derived), instantiate the runner once, replay each cycle."""
    cycles = find_cycles(path)

    if model_id is not None:
        chosen_model_id = model_id
        overridden = True
        print(f"[replay] using user-specified model: {chosen_model_id} (overrides metadata)")
    else:
        first_metadata = json.loads((cycles[0] / "metadata.json").read_text())
        chosen_model_id = derive_model_id_from_metadata(first_metadata)
        overridden = False
        first_hef = first_metadata.get("vlm_config", {}).get("hef_path", "<missing>")
        print(f"[replay] metadata hef_path={first_hef} → using model {chosen_model_id}")

    runner = NativeVlmRunner(
        model_id=chosen_model_id,
        device=device,
        dtype=dtype,
        strict_determinism=strict_determinism,
        suppress_default_system=suppress_default_system,
        effective_video_fps=effective_video_fps,
    )
    context = ReplayContext(
        native_model_id=runner.actual_model_id,
        native_model_overridden=overridden,
    )

    summaries: list[CycleSummary] = []
    for index, cycle in enumerate(cycles, start=1):
        print(f"\n=== Cycle {index}/{len(cycles)}: {cycle.name} ===")
        # Per-cycle model-mismatch warning: only meaningful when we derived
        # the model from the *first* cycle's metadata. If subsequent cycles
        # imply a different model, replay still proceeds with the loaded one.
        if not overridden:
            cycle_metadata = json.loads((cycle / "metadata.json").read_text())
            cycle_derived = derive_model_id_from_metadata(cycle_metadata)
            if cycle_derived != chosen_model_id:
                print(f"  [warn] cycle metadata implies {cycle_derived}, but runner "
                      f"is loaded with {chosen_model_id} — replay continues with the "
                      "loaded model")
        summaries.append(replay_cycle(runner, cycle, context, output_mode=output_mode))
    if len(cycles) > 1 and output_mode != "sidecar":
        _print_batch_footer(summaries)
    return summaries


# ── Helpers ─────────────────────────────────────────────────────────────────

def _check_vlm_config_compatibility(metadata: dict, cycle_dir: Path) -> None:
    cfg = metadata.get("vlm_config", {})
    issues: list[str] = []
    if cfg.get("do_sample", None) is not False:
        issues.append(f"do_sample={cfg.get('do_sample')!r} (expected False)")
    if cfg.get("input_height") != 336 or cfg.get("input_width") != 336:
        issues.append(
            f"input shape {cfg.get('input_height')}x{cfg.get('input_width')} (expected 336x336)"
        )
    if cfg.get("input_channels") != 3:
        issues.append(f"input_channels={cfg.get('input_channels')!r} (expected 3)")
    if issues:
        print(
            f"[warn] {cycle_dir.name}: vlm_config mismatch — replay accuracy may suffer: "
            + ", ".join(issues)
        )


def _compare_responses(inference: dict, result: InferenceResult, kind: str) -> dict:
    """Computes per-inference Hailo-vs-native agreement using the same yes/no
    parser the C++ side uses (so neither side is penalized by parser drift)."""
    response_native = result.response
    if kind == "performance_aggregate":
        expected_count = len(inference.get("verdicts", []))
        verdicts_native = parse_yesno(response_native, expected_count)
        verdicts_hailo = inference.get("verdicts", [])
        match_flags = []
        for native_v, hailo_v in zip(verdicts_native, verdicts_hailo):
            match_flags.append(native_v.detected == bool(hailo_v.get("yes", False)))
        agreed = all(match_flags) if match_flags else True
        return {
            "kind": kind,
            "agreed": agreed,
            "verdicts_native": verdicts_to_dicts(verdicts_native),
            "verdicts_hailo": verdicts_hailo,
            "match_flags": match_flags,
        }

    if kind == "event":
        verdicts_native = parse_yesno(response_native, 1)
        yes_native = verdicts_native[0].detected if verdicts_native else False
        yes_hailo = bool(inference.get("yes", False))
        return {
            "kind": kind,
            "agreed": yes_native == yes_hailo,
            "yes_native": yes_native,
            "yes_hailo": yes_hailo,
            "matched_native": verdicts_native[0].matched if verdicts_native else False,
        }

    # description / unknown — free text, no agreement check
    return {"kind": kind, "agreed": True}


def _build_output_inference(inference: dict, result: InferenceResult, comparison: dict) -> dict:
    item = dict(inference)  # shallow copy preserves all original Hailo fields
    item["response_hailo"] = inference.get("response", "")
    item["response_native"] = result.response
    item["native_prompt_text"] = result.prompt_text
    item["native_stats"] = {
        "total_ms": round(result.total_ms, 3),
        "ttft_ms": round(result.ttft_ms, 3),
        "tokens_generated": result.tokens_generated,
        "context_tokens": result.context_tokens,
    }
    if comparison["kind"] == "performance_aggregate":
        item["verdicts_native"] = comparison["verdicts_native"]
        item["verdicts_match"] = comparison["match_flags"]
    elif comparison["kind"] == "event":
        item["yes_native"] = comparison["yes_native"]
        item["yes_match"] = comparison["agreed"]
    return item


def _build_sidecar(metadata: dict, output_inferences: list[dict],
                   context: ReplayContext) -> dict:
    sidecar = dict(metadata)
    sidecar["inferences"] = output_inferences
    sidecar["replay"] = {
        "schema_version": 2,
        "source_metadata": "metadata.json",
        "native_model_id": context.native_model_id,
        "native_model_overridden": context.native_model_overridden,
    }
    return sidecar


def _disagreement_label(index: int, kind: str, comparison: dict) -> str:
    if kind == "performance_aggregate":
        flags = comparison.get("match_flags", [])
        mismatched = [str(i + 1) for i, ok in enumerate(flags) if not ok]
        return f"#{index}({kind})[events {','.join(mismatched)}]"
    if kind == "event":
        return f"#{index}({kind})[Hailo={comparison['yes_hailo']} native={comparison['yes_native']}]"
    return f"#{index}({kind})"


# ── Stdout formatting ───────────────────────────────────────────────────────

def _print_pre_inference_banner(index: int, inference: dict, frame_count: int) -> None:
    kind = inference.get("kind", "unknown")
    print(f"  [{index}] kind={kind} frames={frame_count} "
          f"max_tokens={inference.get('max_tokens')} "
          f"video_mode={inference.get('use_video_mode', False)}")
    prompt = inference.get("prompt", "")
    if len(prompt) > 80:
        prompt = prompt[:77] + "..."
    print(f"      prompt: {prompt}")


def _print_post_inference_summary(result: InferenceResult, comparison: dict) -> None:
    # Rendered ChatML prompt actually fed to the model — shows the system/user
    # tags, vision pad tokens, and any prior assistant turns the network saw.
    if result.prompt_text:
        print("      native prompt sent to model:")
        for line in result.prompt_text.splitlines() or [result.prompt_text]:
            print(f"        | {line}")
    response_preview = result.response.replace("\n", " ⏎ ")
    if len(response_preview) > 100:
        response_preview = response_preview[:97] + "..."
    print(f"      native response: {response_preview}")
    print(
        f"      stats: total={int(result.total_ms)}ms ttft={int(result.ttft_ms)}ms "
        f"tokens={result.tokens_generated} ctx={result.context_tokens}"
    )
    kind = comparison.get("kind")
    if kind == "performance_aggregate":
        flags = comparison.get("match_flags", [])
        agree = sum(1 for f in flags if f)
        print(f"      verdicts: {agree}/{len(flags)} agree with Hailo")
    elif kind == "event":
        marker = "✓" if comparison["agreed"] else "✗"
        print(f"      yes/no: native={comparison['yes_native']} hailo={comparison['yes_hailo']} {marker}")


def _print_cycle_footer(summary: CycleSummary) -> None:
    if summary.total_inferences == 0:
        return
    print(
        f"  cycle: {summary.agreements}/{summary.total_inferences} agree "
        f"({summary.disagreements} disagree)"
    )
    if summary.disagree_details:
        print(f"    disagreements: {', '.join(summary.disagree_details)}")


def _print_batch_footer(summaries: list[CycleSummary]) -> None:
    print("\n=== Batch summary ===")
    total_inf = sum(s.total_inferences for s in summaries)
    total_agree = sum(s.agreements for s in summaries)
    total_disagree = sum(s.disagreements for s in summaries)
    print(f"cycles={len(summaries)} inferences={total_inf} "
          f"agree={total_agree} disagree={total_disagree}")
    for summary in summaries:
        marker = "✓" if summary.disagreements == 0 else "✗"
        print(f"  {marker} {summary.cycle_dir.name}: "
              f"{summary.agreements}/{summary.total_inferences} agree")
