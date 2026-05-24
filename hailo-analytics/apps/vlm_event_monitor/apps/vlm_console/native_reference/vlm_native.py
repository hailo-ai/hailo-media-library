#!/usr/bin/env python3
"""Native Qwen2-VL/Qwen3-VL reference for vlm_event_monitor.

Two subcommands:
  replay  — replay an on-device debug cycle (or batch) through the native
            model and emit native_metadata.json sidecars + a comparison summary.
  repl    — interactive REPL mirroring the apps/vlm_console command set
            (new / close / sessions / infer / oneshot / events / context / help / quit).

See README.md for usage and determinism caveats.
"""

from __future__ import annotations

import argparse
import os
import shlex
import sys
from pathlib import Path
from typing import Optional

# All model imports are lazy (inside command handlers) so --help works
# without torch/transformers installed.

MAX_REPL_EVENTS = 5
DEFAULT_MAX_TOKENS = 256
LETTERBOX_SIZE = 336


# ── Shared CLI plumbing ─────────────────────────────────────────────────────

def _build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="vlm_native",
        description="Native Qwen2-VL reference replayer for vlm_event_monitor",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--model-id", default=None,
                        help="HuggingFace model id. If omitted: replay derives it from the "
                             "first cycle's vlm_config.hef_path (e.g. Qwen2-VL-2B-Instruct.hef "
                             "→ Qwen/Qwen2-VL-2B-Instruct); repl falls back to "
                             "Qwen/Qwen2-VL-2B-Instruct. Pass explicitly to override "
                             "(e.g. Qwen/Qwen3-VL-2B-Instruct).")
    common.add_argument("--device", choices=["auto", "cuda", "cpu"], default="auto")
    common.add_argument("--dtype", choices=["auto", "bf16", "fp16", "fp32"], default="auto")
    common.add_argument("--no-strict-determinism", action="store_true",
                        help="Disable deterministic CUDA ops (faster but may jitter run-to-run)")
    common.add_argument("--keep-default-system", action="store_true",
                        help="Keep Qwen2-VL's auto-injected 'You are a helpful assistant.' "
                             "system block. Off by default to match the Hailo HEF path which "
                             "does not surface a system role.")
    common.add_argument("--video-fps", type=float, default=0.5,
                        help="Effective fps of the dumped frames (Qwen3-VL uses this for "
                             "frame-temporal embedding and for textual frame timestamps in "
                             "the prompt). Default 0.5 matches Hailo's on-device sampling: "
                             "1 FPS source with sample indices 2 s apart in both Performance "
                             "and Accuracy modes. Qwen2-VL is unaffected.")

    replay_p = sub.add_parser("replay", parents=[common],
                              help="Replay a debug cycle (or batch of cycles)")
    replay_p.add_argument("path", type=Path,
                          help="Cycle dir (containing metadata.json) or parent dir of cycles")
    replay_p.add_argument("--output", choices=["sidecar", "stdout", "both"], default="both")

    sub.add_parser("repl", parents=[common], help="Interactive REPL")

    return parser


def main(argv: Optional[list[str]] = None) -> int:
    args = _build_arg_parser().parse_args(argv)
    strict = not args.no_strict_determinism
    suppress_default_system = not args.keep_default_system

    if args.command == "replay":
        return _cmd_replay(args, strict_determinism=strict,
                           suppress_default_system=suppress_default_system)
    if args.command == "repl":
        return _cmd_repl(args, strict_determinism=strict,
                         suppress_default_system=suppress_default_system)
    return 2


# ── replay subcommand ───────────────────────────────────────────────────────

def _cmd_replay(args, strict_determinism: bool, suppress_default_system: bool) -> int:
    # The runner is created inside replay_path so it can peek the first
    # cycle's metadata to pick a model when --model-id was not supplied.
    from replay import replay_path

    summaries = replay_path(
        args.path,
        model_id=args.model_id,
        device=args.device,
        dtype=args.dtype,
        strict_determinism=strict_determinism,
        suppress_default_system=suppress_default_system,
        effective_video_fps=args.video_fps,
        output_mode=args.output,
    )
    total_disagree = sum(s.disagreements for s in summaries)
    return 0 if total_disagree == 0 else 1


# ── repl subcommand ─────────────────────────────────────────────────────────

def _cmd_repl(args, strict_determinism: bool, suppress_default_system: bool) -> int:
    from native_runner import NativeVlmRunner

    # REPL has no metadata to derive from — fall back to Qwen2-VL-2B-Instruct
    # if --model-id wasn't supplied.
    repl_model_id = args.model_id or "Qwen/Qwen2-VL-2B-Instruct"
    runner = NativeVlmRunner(
        model_id=repl_model_id,
        device=args.device,
        dtype=args.dtype,
        strict_determinism=strict_determinism,
        suppress_default_system=suppress_default_system,
        effective_video_fps=args.video_fps,
    )
    _print_repl_help()
    while True:
        try:
            line = input("> ")
        except (EOFError, KeyboardInterrupt):
            print()
            break

        line = line.strip()
        if not line:
            continue

        try:
            tokens = shlex.split(line)
        except ValueError as exc:
            print(f"Parse error: {exc}")
            continue

        cmd = tokens[0].lower()
        if cmd in ("quit", "exit", "q"):
            break
        if cmd in ("help", "h"):
            _print_repl_help()
            continue
        try:
            _dispatch_repl_command(cmd, tokens, runner)
        except KeyError as exc:
            print(f"Error: {exc}")
        except RuntimeError as exc:
            print(f"Runtime error: {exc}")
    print("Exiting.")
    return 0


def _dispatch_repl_command(cmd: str, tokens: list[str], runner) -> None:
    if cmd == "new":
        sid = runner.new_session()
        print(f"Session {sid} created")
    elif cmd == "close":
        if len(tokens) < 2:
            print("Usage: close <session_id>")
            return
        runner.close_session(int(tokens[1]))
    elif cmd == "sessions":
        ids = runner.list_sessions()
        if ids:
            print("Active sessions: " + " ".join(map(str, ids)))
        else:
            print("No active sessions")
    elif cmd == "context":
        ids = runner.list_sessions()
        if not ids:
            print("No active sessions")
            return
        for sid in ids:
            messages = runner.session_messages(sid)
            print(f"  session {sid}: {len(messages)} message(s) in history")
    elif cmd == "infer":
        _repl_infer(tokens, runner)
    elif cmd == "oneshot":
        _repl_oneshot(tokens, runner)
    elif cmd == "events":
        _repl_events(tokens, runner)
    else:
        print(f"Unknown command: {cmd}. Type 'help' for available commands.")


def _print_repl_help() -> None:
    print(
        "\nCommands:\n"
        "  new                                              Create new session\n"
        "  close <session_id>                               Close a session\n"
        "  infer <session_id> <path> \"<prompt>\" [max]       Infer with session (new frames)\n"
        "  infer <session_id> \"<prompt>\" [max]              Follow-up infer (text-only, reuses session)\n"
        "  oneshot <path> \"<prompt>\" [max]                  One-shot inference (no session)\n"
        f"  events <path>                                    Event-detection on a JPEG dir (up to {MAX_REPL_EVENTS} events)\n"
        "  context                                          Show context length per session\n"
        "  sessions                                         List active sessions\n"
        "  help                                             Show this help\n"
        "  quit                                             Exit\n"
        "\n"
        "  <path> = JPEG file, directory of JPEGs, or \"none\" for text-only\n"
        "  Directory with 2+ JPEGs automatically uses video mode\n"
        f"  [max] = optional integer max_generated_tokens (default: {DEFAULT_MAX_TOKENS})\n"
    )


# ── REPL command handlers ───────────────────────────────────────────────────

def _repl_infer(tokens: list[str], runner) -> None:
    """
    Accepted shapes (matches vlm_console main.cpp:526-573):
      infer <id> "<prompt>"                      text-only follow-up
      infer <id> "<prompt>" <max>                text-only + max
      infer <id> <path> "<prompt>"               with frames
      infer <id> <path> "<prompt>" <max>         with frames + max
    """
    if not 3 <= len(tokens) <= 5:
        print("Usage: infer <session_id> [<path>] \"<prompt>\" [max_tokens]")
        return

    session_id = int(tokens[1])
    path: str = ""
    prompt: str = ""
    max_tokens: int = 0

    if len(tokens) == 3:
        prompt = tokens[2]
    elif len(tokens) == 4:
        if _is_non_negative_int(tokens[3]):
            prompt, max_tokens = tokens[2], int(tokens[3])
        else:
            path, prompt = tokens[2], tokens[3]
    else:  # 5
        path, prompt = tokens[2], tokens[3]
        if not _is_non_negative_int(tokens[4]):
            print(f"Error: max_tokens must be a non-negative integer, got: {tokens[4]}")
            return
        max_tokens = int(tokens[4])

    frames, use_video_mode = _load_frames_for_repl(path) if path else ([], False)
    if path and path != "none" and not frames:
        print(f"Error: no JPEG frames found at: {path}")
        return

    print(f"Loading {len(frames)} frame(s) "
          f"({'video mode' if use_video_mode else 'image mode' if frames else 'text-only follow-up'})"
          f"{f' max_tokens={max_tokens}' if max_tokens > 0 else ''}...")

    result = runner.infer(
        session_id=session_id,
        frames=frames,
        use_video_mode=use_video_mode,
        prompt=prompt,
        max_new_tokens=max_tokens or DEFAULT_MAX_TOKENS,
        stream_to_stdout=True,
    )
    _print_infer_stats(result)


def _repl_oneshot(tokens: list[str], runner) -> None:
    if not 3 <= len(tokens) <= 4:
        print("Usage: oneshot <path> \"<prompt>\" [max_tokens]")
        return

    path, prompt = tokens[1], tokens[2]
    max_tokens = 0
    if len(tokens) == 4:
        if not _is_non_negative_int(tokens[3]):
            print(f"Error: max_tokens must be a non-negative integer, got: {tokens[3]}")
            return
        max_tokens = int(tokens[3])

    frames, use_video_mode = _load_frames_for_repl(path)
    if not frames:
        print(f"Error: no JPEG frames found at: {path}")
        return

    print(f"Loading {len(frames)} frame(s) "
          f"({'video mode' if use_video_mode else 'image mode'}) (one-shot)"
          f"{f' max_tokens={max_tokens}' if max_tokens > 0 else ''}...")

    result = runner.infer(
        session_id=None,
        frames=frames,
        use_video_mode=use_video_mode,
        prompt=prompt,
        max_new_tokens=max_tokens or DEFAULT_MAX_TOKENS,
        stream_to_stdout=True,
    )
    _print_infer_stats(result)


def _repl_events(tokens: list[str], runner) -> None:
    if len(tokens) != 2:
        print("Usage: events <path>   (path must be a directory of JPEGs)")
        return
    path = Path(tokens[1])
    if not path.is_dir():
        print(f"Error: path must be a directory of JPEGs: {path}")
        return

    frames, _ = _load_frames_for_repl(str(path))
    if not frames:
        print(f"Error: no JPEG frames found in directory: {path}")
        return

    events = _prompt_for_events()
    if not events:
        print("Error: no events entered, aborting.")
        return

    prompt = _compile_event_prompt(events)
    # Same per-event token budget as main.cpp:726 (TOKEN_PER_EVENT * count)
    max_new_tokens = len(events) * 5

    print(f"\nCompiled user prompt:\n{prompt}\n")
    print(f"Loading {len(frames)} frame(s) (video mode, events)...")

    result = runner.infer(
        session_id=None,
        frames=frames,
        use_video_mode=True,
        prompt=prompt,
        max_new_tokens=max_new_tokens,
        stream_to_stdout=True,
    )
    _print_infer_stats(result)

    from yesno_parser import parse_yesno, verdicts_to_dicts
    import json
    parsed = verdicts_to_dicts(parse_yesno(result.response, len(events)))
    print(f"Parsed JSON: {json.dumps(parsed)}")


# ── REPL helpers ────────────────────────────────────────────────────────────

def _is_non_negative_int(value: str) -> bool:
    return bool(value) and value.isdigit()


def _print_infer_stats(result) -> None:
    if result.prompt_text:
        print("\nNative prompt sent to model:")
        for line in result.prompt_text.splitlines() or [result.prompt_text]:
            print(f"  | {line}")
    print(f"\nNative response: {result.response}")
    print(
        f"Stats: TTFT={int(result.ttft_ms)}ms "
        f"Total={int(result.total_ms)}ms "
        f"Tokens={result.tokens_generated} "
        f"Ctx={result.context_tokens}"
    )


def _load_frames_for_repl(path_arg: str):
    """Mirrors load_frames in main.cpp:38-90: file → 1 frame, dir → multiple
    (video mode if 2+), 'none' or empty → text-only."""
    from PIL import Image
    if path_arg in ("", "none"):
        return [], False

    p = Path(path_arg)
    if p.is_dir():
        jpeg_paths = sorted(
            child for child in p.iterdir()
            if child.is_file() and child.suffix.lower() in (".jpg", ".jpeg")
        )
        frames = [_letterbox_to_target(Image.open(jp).convert("RGB")) for jp in jpeg_paths]
        use_video_mode = len(frames) > 1
        return frames, use_video_mode
    if p.is_file():
        return [_letterbox_to_target(Image.open(p).convert("RGB"))], False
    print(f"Error: path does not exist: {path_arg}")
    return [], False


def _letterbox_to_target(image, target: int = LETTERBOX_SIZE):
    """Mirrors vlm_frame_preprocessor.cpp:32-44 — preserve aspect, pad with
    black to target x target. Used only by the REPL's ad-hoc path; replay
    uses already-336x336 dump frames directly."""
    from PIL import Image
    width, height = image.size
    if width == target and height == target:
        return image
    scale = min(target / width, target / height)
    new_w = max(1, int(width * scale))
    new_h = max(1, int(height * scale))
    resized = image.resize((new_w, new_h), Image.BILINEAR)
    canvas = Image.new("RGB", (target, target), color=(0, 0, 0))
    canvas.paste(resized, ((target - new_w) // 2, (target - new_h) // 2))
    return canvas


def _prompt_for_events() -> list[str]:
    events: list[str] = []
    while len(events) < MAX_REPL_EVENTS:
        try:
            description = input(f"Event {len(events) + 1} description: ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        if not description:
            print("Description cannot be empty. Re-enter.")
            continue
        # Strip [] — they're reserved as event delimiters in compile_event_prompt.
        description = description.replace("[", "").replace("]", "")
        events.append(description)
        if len(events) >= MAX_REPL_EVENTS:
            print(f"Reached maximum of {MAX_REPL_EVENTS} events.")
            break
        try:
            answer = input("Add another event? (y/n): ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        if not answer or answer[0].lower() != "y":
            break
    return events


def _compile_event_prompt(events: list[str]) -> str:
    # Mirrors compile_event_prompt at main.cpp:211-224 + kEventQuestionPrefix
    prefix = ("tell me if there is any activity of the following, answer "
              "yes or no separately for each activity listed as follow: ")
    parts = [f"{i + 1}.[{desc}]" for i, desc in enumerate(events)]
    return prefix + " ".join(parts)


if __name__ == "__main__":
    sys.exit(main())
