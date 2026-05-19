"""NativeVlmRunner — wraps a HuggingFace Vision-Language Model (Qwen2-VL,
Qwen3-VL, or any AutoModelForVision2Seq-compatible checkpoint) for the
comparison/replay tool.

Mirrors the on-device VlmInferenceManager behaviour:
  * `do_sample=False` is hardcoded (matches `vlm_inference_manager.cpp:96`).
  * Multi-turn sessions are kept as a chat-template message list — equivalent
    to the on-device KV-cache continuity, just re-tokenised each turn.
  * Image vs video mode is per-call (`use_video_mode`) — image mode emits one
    {"type":"image"} per frame, video mode bundles all frames under a single
    {"type":"video"} content item.
  * Output has the `<|im_end|>` marker stripped to match
    `vlm_inference_manager.cpp:454`.

Model loading is via `AutoModelForVision2Seq` so the runner works for any
model family that registers with that auto-class. The default-system-prompt
strip logic detects what the loaded model's chat template auto-injects at
init time, so it adapts to model-specific template quirks without per-model
constants.

Strict determinism is set up here at import time of the runner so all later
generate() calls inherit it. CPU is deterministic regardless; the flag mostly
matters on CUDA.
"""

from __future__ import annotations

import os
import time
from dataclasses import dataclass, field
from threading import Thread
from typing import Optional


@dataclass
class InferenceResult:
    response: str
    tokens_generated: int
    total_ms: float
    ttft_ms: float
    context_tokens: int          # number of tokens in the prompt fed to generate()
    prompt_text: str = ""        # ChatML-rendered prompt actually sent to the model
                                 # (output of processor.apply_chat_template — includes
                                 # <|im_start|>system/user/assistant tags and vision
                                 # pad tokens). What the network sees, not the user prompt.


@dataclass
class _Session:
    session_id: int
    messages: list = field(default_factory=list)


class NativeVlmRunner:
    DEFAULT_MODEL_ID = "Qwen/Qwen2-VL-2B-Instruct"
    EOS_MARKER = "<|im_end|>"

    def __init__(
        self,
        model_id: str = DEFAULT_MODEL_ID,
        device: str = "auto",
        dtype: str = "auto",
        strict_determinism: bool = True,
        suppress_default_system: bool = True,
        effective_video_fps: float = 0.5,
    ):
        # effective_video_fps: rate the *dumped* frames represent (not the
        # source camera's native rate). Hailo's on-device sampler at 1 FPS
        # source picks frames 2 s apart in both Performance and Accuracy
        # modes (see event_check_runner.cpp:25-43), so 0.5 fps matches the
        # current device firmware. Qwen3-VL needs this to (a) avoid padding
        # our frames up to its min_frames=4 default and (b) inline correct
        # textual frame timestamps in the user message. Qwen2-VL ignores it.
        # Set determinism *before* importing torch so CUBLAS sees the env var.
        if strict_determinism:
            os.environ.setdefault("CUBLAS_WORKSPACE_CONFIG", ":4096:8")

        import torch
        from transformers import AutoProcessor

        # Class naming changed between transformers 4.x and 5.x:
        #   4.x: AutoModelForVision2Seq
        #   5.x: AutoModelForImageTextToText (Vision2Seq removed)
        # Try the 5.x name first (newer venvs likely have v5+) and fall back.
        try:
            from transformers import AutoModelForImageTextToText as AutoVlmModel
        except ImportError:
            from transformers import AutoModelForVision2Seq as AutoVlmModel

        self._torch = torch
        self._strict_determinism = strict_determinism

        if strict_determinism:
            torch.use_deterministic_algorithms(True, warn_only=False)
            torch.manual_seed(0)
            if torch.cuda.is_available():
                torch.cuda.manual_seed_all(0)

        resolved_device = self._resolve_device(device)
        resolved_dtype = self._resolve_dtype(dtype, resolved_device)

        print(f"[runner] loading {model_id} on {resolved_device} ({resolved_dtype})", flush=True)

        # AutoModelForImageTextToText (transformers 5.x) / AutoModelForVision2Seq
        # (transformers 4.x) dispatches on the checkpoint's config to the right
        # family-specific class (Qwen2VL / Qwen3VL / others). Some Qwen3 releases
        # ship custom modeling code that requires trust_remote_code.
        self._model = AutoVlmModel.from_pretrained(
            model_id,
            torch_dtype=resolved_dtype,
            device_map=resolved_device,
            trust_remote_code=True,
        )
        self._model.eval()
        self._processor = AutoProcessor.from_pretrained(model_id, trust_remote_code=True)
        self._device = resolved_device
        self._dtype = resolved_dtype

        self.actual_model_id: str = model_id
        self.effective_video_fps: float = effective_video_fps

        self._suppress_default_system = suppress_default_system
        # Probe the chat template once so the strip works for any model that
        # follows the ChatML pattern, not just Qwen2-VL.
        self._default_system_block = self._detect_default_system_block()
        if suppress_default_system and self._default_system_block:
            preview = self._default_system_block.replace("\n", "\\n")
            print(f"[runner] template auto-injects default system block "
                  f"({len(self._default_system_block)} chars): {preview}",
                  flush=True)

        self._sessions: dict[int, _Session] = {}
        self._next_session_id: int = 1

    # ── Session management ──────────────────────────────────────────────────

    def new_session(self) -> int:
        sid = self._next_session_id
        self._next_session_id += 1
        self._sessions[sid] = _Session(session_id=sid)
        return sid

    def close_session(self, sid: int) -> None:
        self._sessions.pop(sid, None)

    def list_sessions(self) -> list[int]:
        return sorted(self._sessions.keys())

    def session_messages(self, sid: int) -> list:
        if sid not in self._sessions:
            raise KeyError(f"session {sid} does not exist")
        return self._sessions[sid].messages

    def reset_session(self, sid: int) -> None:
        if sid in self._sessions:
            self._sessions[sid].messages = []

    # ── Inference ───────────────────────────────────────────────────────────

    def infer(
        self,
        session_id: Optional[int],
        frames=(),
        use_video_mode: bool = False,
        prompt: str = "",
        max_new_tokens: int = 256,
        system_prompt: str = "",
        stream_to_stdout: bool = False,
    ) -> InferenceResult:
        """Run one inference turn. session_id=None → one-shot (no history kept).

        `frames` is a list of PIL.Image (already at the model's expected
        input resolution; replay passes 336x336 frames as dumped on device).
        Empty frames + non-empty prompt = text-only follow-up.
        """
        from qwen_vl_utils import process_vision_info
        from transformers import TextIteratorStreamer

        # Build the message list for this turn
        if session_id is None:
            messages = []
            if system_prompt:
                messages.append({"role": "system", "content": system_prompt})
        else:
            session = self._sessions.get(session_id)
            if session is None:
                raise KeyError(f"session {session_id} does not exist")
            messages = list(session.messages)
            # System prompt only on the first turn — matches
            # vlm_inference_manager.cpp:273-276 ("first_inference" gate).
            if not messages and system_prompt:
                messages.append({"role": "system", "content": system_prompt})

        user_content: list = []
        if frames and use_video_mode:
            user_content.append({"type": "video", "video": list(frames)})
        elif frames:
            for frame in frames:
                user_content.append({"type": "image", "image": frame})
        user_content.append({"type": "text", "text": prompt})
        messages.append({"role": "user", "content": user_content})

        # Build processor inputs
        text = self._processor.apply_chat_template(
            messages, tokenize=False, add_generation_prompt=True
        )
        text = self._maybe_strip_default_system(text, messages)
        image_inputs, video_inputs = process_vision_info(messages)

        # Build per-call video_metadata so Qwen3-VL knows the temporal
        # spacing of the pre-sampled frames. Without this, Qwen3VL defaults
        # to fps=24, pads our 3 frames up to min_frames=4, and inlines
        # incorrect textual frame timestamps in the user message. Qwen2-VL
        # silently ignores this kwarg via processor_utils, so it's safe to
        # pass unconditionally.
        #
        # Critical: derive frame counts from `video_inputs` (the videos
        # actually being sent to the processor), NOT from the per-call
        # `frames` parameter. In a multi-turn session, a text-only follow-up
        # (frames=()) still has the original turn-0 video preserved in the
        # conversation history; process_vision_info extracts it, so
        # video_inputs is non-empty and the processor needs metadata for
        # those historical frames or we'd hit a division-by-zero in
        # smart_resize on Qwen3-VL.
        processor_kwargs: dict = {
            "text": [text],
            "images": image_inputs,
            "videos": video_inputs,
            "padding": True,
            "return_tensors": "pt",
        }
        if video_inputs:
            video_metadata = []
            for video in video_inputs:
                # video may be a list of PIL frames, a numpy ndarray, or a
                # torch tensor. Frame count is the leading length in all three.
                try:
                    n = len(video)
                except TypeError:
                    n = 1
                if n <= 0:
                    continue
                video_metadata.append({
                    "total_num_frames": n,
                    "fps": self.effective_video_fps,
                    "frames_indices": list(range(n)),
                })
            if video_metadata:
                processor_kwargs["video_metadata"] = video_metadata
        inputs = self._processor(**processor_kwargs)
        inputs = {k: v.to(self._model.device) for k, v in inputs.items()}
        context_tokens = int(inputs["input_ids"].shape[1])

        # Stream-based generation so we can measure TTFT
        streamer = TextIteratorStreamer(
            self._processor.tokenizer,
            skip_prompt=True,
            skip_special_tokens=False,
        )
        gen_kwargs = dict(
            **inputs,
            max_new_tokens=max_new_tokens,
            do_sample=False,
            streamer=streamer,
        )

        chunks: list[str] = []
        ttft_ms: Optional[float] = None
        start = time.perf_counter()

        thread = Thread(target=self._safe_generate, kwargs={"gen_kwargs": gen_kwargs})
        thread.start()
        try:
            for new_text in streamer:
                if ttft_ms is None and new_text:
                    ttft_ms = (time.perf_counter() - start) * 1000.0
                chunks.append(new_text)
                if stream_to_stdout:
                    print(new_text, end="", flush=True)
        finally:
            thread.join()
        total_ms = (time.perf_counter() - start) * 1000.0
        if stream_to_stdout:
            print()

        raw_response = "".join(chunks)
        response = self._strip_eos(raw_response)
        tokens_generated = len(
            self._processor.tokenizer.encode(raw_response, add_special_tokens=False)
        )

        # Append assistant turn to session history so next infer() sees it
        if session_id is not None:
            self._sessions[session_id].messages = messages + [
                {"role": "assistant", "content": response}
            ]

        return InferenceResult(
            response=response,
            tokens_generated=tokens_generated,
            total_ms=total_ms,
            ttft_ms=ttft_ms if ttft_ms is not None else total_ms,
            context_tokens=context_tokens,
            prompt_text=text,
        )

    # ── Internals ───────────────────────────────────────────────────────────

    def _safe_generate(self, gen_kwargs):
        """Wrap model.generate() so a deterministic-ops RuntimeError gets
        translated to an actionable message instead of dying inside a thread."""
        try:
            with self._torch.no_grad():
                self._model.generate(**gen_kwargs)
        except RuntimeError as exc:
            msg = str(exc)
            if "deterministic" in msg.lower() and self._strict_determinism:
                raise RuntimeError(
                    "Strict determinism hit a non-deterministic op. "
                    "Re-run with --no-strict-determinism to allow this op. "
                    f"Original error: {exc}"
                ) from exc
            raise

    @staticmethod
    def _resolve_device(device: str) -> str:
        if device != "auto":
            return device
        import torch
        return "cuda" if torch.cuda.is_available() else "cpu"

    @staticmethod
    def _resolve_dtype(dtype: str, device: str):
        import torch
        mapping = {
            "fp32": torch.float32,
            "fp16": torch.float16,
            "bf16": torch.bfloat16,
        }
        if dtype != "auto":
            return mapping[dtype]
        return torch.bfloat16 if device == "cuda" else torch.float32

    def _detect_default_system_block(self) -> str:
        """Render a probe message list and capture whatever the model's chat
        template auto-injects before the first user turn. That prefix is the
        default-system block we'll strip when the caller didn't pass an
        explicit system role. Works for any chat-template that follows the
        ChatML pattern (Qwen2-VL, Qwen3-VL, ...)."""
        try:
            rendered = self._processor.apply_chat_template(
                [{"role": "user", "content": "PROBE"}],
                tokenize=False,
                add_generation_prompt=True,
            )
        except Exception:
            return ""
        marker = "<|im_start|>user"
        idx = rendered.find(marker)
        return rendered[:idx] if idx > 0 else ""

    def _maybe_strip_default_system(self, rendered: str, messages: list) -> str:
        """When no explicit system message was provided by the caller, the
        model's chat template typically prepends a default system block (e.g.
        Qwen2-VL's "You are a helpful assistant."). The Hailo HEF path does
        not surface this default, so for an apples-to-apples prompt
        comparison we drop the exact prefix the template auto-injects.

        The block to strip is detected once at init (see
        `_detect_default_system_block`) so this works across model families."""
        if not self._suppress_default_system or not self._default_system_block:
            return rendered
        explicit_system = (
            bool(messages)
            and messages[0].get("role") == "system"
            and bool(messages[0].get("content"))
        )
        if explicit_system:
            return rendered
        if rendered.startswith(self._default_system_block):
            return rendered[len(self._default_system_block):]
        return rendered

    @classmethod
    def _strip_eos(cls, text: str) -> str:
        # Mirrors vlm_inference_manager.cpp:454 — drop the Qwen <|im_end|> tag
        # if it leaked through (skip_special_tokens=False above).
        idx = text.find(cls.EOS_MARKER)
        if idx >= 0:
            text = text[:idx]
        return text.rstrip()
