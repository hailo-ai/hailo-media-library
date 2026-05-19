"""Faithful Python port of the C++ yes/no parser used by the on-device app.

Mirrors `parse_yesno_to_json` and `consume_alpha_token` in
`apps/vlm_console/main.cpp:229-372`. Keeping the two parsers behaviorally
identical is what lets us treat any Hailo-vs-native disagreement as a real
model-output difference rather than a parser quirk.

Two-pass strategy:
  1. Numbered scan: match "<digit>. Yes|No" anywhere in the string and key
     into the slot indexed by the digit (1-based, mapped to 0-based).
  2. Bare-token fallback: only triggers when pass 1 matched nothing — fills
     unmatched slots in order from the first N bare yes/no alpha tokens.

The fallback is gated on "all slots unmatched" rather than "any slot
unmatched" to avoid contaminating partially-matched multi-event runs with
prose elsewhere in the response.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass
class Verdict:
    id: int          # 1-based event id
    detected: bool   # True if the parser saw "yes", False otherwise
    matched: bool    # True if a yes/no token was actually located for this slot


def _consume_alpha_token(response: str, scan: int) -> tuple[str, int]:
    """Skip non-alpha chars, then consume a contiguous run of alpha chars
    (lowercased). Returns (token, new_scan_position). Empty token if no
    alpha run before end of string."""
    while scan < len(response) and not response[scan].isalpha():
        scan += 1
    start = scan
    while scan < len(response) and response[scan].isalpha():
        scan += 1
    return response[start:scan].lower(), scan


def parse_yesno(response: str, expected_count: int) -> list[Verdict]:
    """Parse a yes/no response into exactly `expected_count` verdicts.

    Slots default to detected=False, matched=False. The numbered-scan pass
    fills slots whose event id appears as "<digit>. Yes|No". The fallback
    pass only runs if the numbered scan found nothing, and it fills slots
    in order from bare yes/no alpha tokens anywhere in the response.
    """
    if expected_count <= 0:
        return []

    verdicts = [Verdict(id=i + 1, detected=False, matched=False)
                for i in range(expected_count)]

    # ── Pass 1: numbered scan ("<digit>. Yes|No") ────────────────────────────
    scan = 0
    n = len(response)
    while scan < n:
        if not response[scan].isdigit():
            scan += 1
            continue

        digit_start = scan
        while scan < n and response[scan].isdigit():
            scan += 1
        if scan >= n or response[scan] != '.':
            continue
        try:
            event_id = int(response[digit_start:scan])
        except ValueError:
            continue
        scan += 1  # skip '.'
        while scan < n and response[scan].isspace():
            scan += 1
        if scan >= n:
            break

        token, scan = _consume_alpha_token(response, scan)
        if event_id < 1 or event_id > expected_count:
            continue
        slot = event_id - 1
        if token == "yes":
            verdicts[slot].detected = True
            verdicts[slot].matched = True
        elif token == "no":
            verdicts[slot].detected = False
            verdicts[slot].matched = True

    # ── Pass 2: fallback when pass 1 matched nothing ─────────────────────────
    if not any(v.matched for v in verdicts):
        slot_to_fill = 0
        scan = 0
        while scan < n and slot_to_fill < expected_count:
            token, scan = _consume_alpha_token(response, scan)
            if not token:
                break
            if token == "yes":
                verdicts[slot_to_fill].detected = True
                verdicts[slot_to_fill].matched = True
                slot_to_fill += 1
            elif token == "no":
                verdicts[slot_to_fill].detected = False
                verdicts[slot_to_fill].matched = True
                slot_to_fill += 1

    return verdicts


def verdicts_to_dicts(verdicts: list[Verdict]) -> list[dict]:
    """JSON-friendly representation. Omits `matched` when True, matching the
    C++ output shape (which only emits "matched": false on misses)."""
    out: list[dict] = []
    for v in verdicts:
        item: dict = {"id": v.id, "detected": v.detected}
        if not v.matched:
            item["matched"] = False
        out.append(item)
    return out
