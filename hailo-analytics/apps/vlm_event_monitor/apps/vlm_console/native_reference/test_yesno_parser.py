"""Unit tests for yesno_parser. Cases pulled from the comments and behaviour
of the C++ parser at apps/vlm_console/main.cpp:229-372.

Run: python -m pytest test_yesno_parser.py
"""

from yesno_parser import parse_yesno, verdicts_to_dicts


def _flatten(response: str, n: int) -> list[tuple[bool, bool]]:
    return [(v.detected, v.matched) for v in parse_yesno(response, n)]


def test_numbered_three_events_newline_separated():
    assert _flatten("1. Yes\n2. No\n3. Yes\n", 3) == [
        (True, True), (False, True), (True, True),
    ]


def test_numbered_three_events_comma_separated():
    assert _flatten("1. Yes, 2. No, 3. Yes", 3) == [
        (True, True), (False, True), (True, True),
    ]


def test_numbered_with_trailing_text():
    assert _flatten("1. Yes. 2. No. 3. Yes. The activity is clear.", 3) == [
        (True, True), (False, True), (True, True),
    ]


def test_numbered_lowercase():
    assert _flatten("1. yes\n2. no\n3. yes\n", 3) == [
        (True, True), (False, True), (True, True),
    ]


def test_bare_no_falls_back_to_slot_zero():
    # Single-event case where the model drops the "1. " prefix.
    assert _flatten("No.", 1) == [(False, True)]
    assert _flatten("Yes, the activity is clearly present.", 1) == [(True, True)]


def test_bare_token_fills_in_order():
    # Multi-event case where numbered scan finds nothing and fallback kicks in.
    assert _flatten("Yes No Yes", 3) == [
        (True, True), (False, True), (True, True),
    ]


def test_numbered_partial_does_not_trigger_fallback():
    # Pass 1 matched event 1; even though slots 2 and 3 are empty, fallback
    # must NOT fill them from prose elsewhere in the response.
    result = _flatten("1. Yes. The other items are unclear.", 3)
    assert result[0] == (True, True)
    # Slots 2 and 3 stay matched=False (not contaminated by the prose).
    assert result[1] == (False, False)
    assert result[2] == (False, False)


def test_out_of_range_event_id_falls_through_to_bare_scan():
    # "5. Yes" with expected_count=3: Pass 1 drops the out-of-range digit
    # (no slot matched), then Pass 2 fallback kicks in and fills slot 0
    # from the bare "Yes" alpha token. Mirrors C++ behaviour at main.cpp:319-352.
    result = _flatten("5. Yes", 3)
    assert result == [(True, True), (False, False), (False, False)]


def test_zero_expected_count_returns_empty():
    assert parse_yesno("anything", 0) == []


def test_empty_response_returns_unmatched_slots():
    result = _flatten("", 2)
    assert result == [(False, False), (False, False)]


def test_dquote_wrapped_no_fallback():
    # Real metadata had "\"no\"." — alpha-token consumer skips quotes.
    assert _flatten('"no".', 1) == [(False, True)]


def test_dict_output_omits_matched_when_true():
    verdicts = parse_yesno("1. Yes\n2. No", 2)
    dicts = verdicts_to_dicts(verdicts)
    assert dicts == [
        {"id": 1, "detected": True},
        {"id": 2, "detected": False},
    ]


def test_dict_output_includes_matched_false_on_miss():
    verdicts = parse_yesno("1. Yes", 2)  # slot 2 unmatched
    dicts = verdicts_to_dicts(verdicts)
    assert dicts == [
        {"id": 1, "detected": True},
        {"id": 2, "detected": False, "matched": False},
    ]
