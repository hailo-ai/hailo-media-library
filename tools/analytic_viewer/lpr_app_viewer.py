"""
LPR (License Plate Recognition) Viewer Application

Specialized viewer for the Hailo-15 LPR application. Extends the base analytic viewer
with a right-side panel displaying cropped license plate images and their OCR text.

When high-confidence OCR results arrive, the viewer:
1. Crops the license plate region from the decoded video frame
2. Enlarges it for readability using bilinear scaling
3. Displays it in a rolling 5-slot panel alongside the OCR text

Usage:
    python3 lpr_app_viewer.py --udp-port 5000
    python3 lpr_app_viewer.py --udp-port 5000 --output-resolution 1080p --debug-perf
"""

import sys
import os
import gi
gi.require_version('Pango', '1.0')
gi.require_version('PangoCairo', '1.0')
from gi.repository import Pango, PangoCairo
import cairo
import threading
import argparse
import time
import math
import select
import termios
import tty
from dataclasses import dataclass, field
from typing import List, Optional, Dict, Tuple, NamedTuple

from viewer_common import (
    BaseVideoPlayer, DrawTimer,
    OUTPUT_SIZE_PRESETS, DEFAULT_ANALYTIC_DATA_IP, DEFAULT_ZMQ_PORT, DEFAULT_UDP_IP,
    Gst, GLib,
)

# ============================================================================
# Per-Class Bounding Box Colors
# ============================================================================
CLASS_COLORS = {
    "person":        (0.0, 1.0, 0.0, 0.8),   # green
    "vehicle":       (0.2, 0.4, 1.0, 0.8),   # blue
    "license_plate": (1.0, 0.0, 0.0, 0.8),   # red
    "face":          (1.0, 1.0, 0.0, 0.8),   # yellow
    "truck":         (1.0, 0.5, 0.0, 0.8),   # orange
    "bus":           (0.6, 0.2, 1.0, 0.8),   # purple
    "motorcycle":    (0.0, 1.0, 1.0, 0.8),   # cyan
}
DEFAULT_CLASS_COLOR = (0.85, 0.85, 0.85, 0.8)  # light gray

# ============================================================================
# LPR Panel Constants
# ============================================================================
SLOT_COUNT = 5
PANEL_WIDTH_RATIO = 0.20
PANEL_MARGIN_PX = 8
SLOT_PADDING_PX = 6
SLOT_CORNER_RADIUS = 6
PANEL_BG_COLOR = (0.0, 0.0, 0.0, 0.7)
TEXT_BG_COLOR = (1.0, 1.0, 1.0, 0.9)
TEXT_COLOR = (0.0, 0.0, 0.0, 1.0)
EMPTY_SLOT_BORDER_COLOR = (1.0, 1.0, 1.0, 0.2)
EMPTY_SLOT_BORDER_WIDTH = 1.0
OCR_FONT_SIZE_RATIO = 0.70
OCR_FONT_MIN_PX = 14

PLATE_NETWORK_WIDTH = 320
PLATE_NETWORK_HEIGHT = 48
PLATE_ASPECT_RATIO = PLATE_NETWORK_WIDTH / PLATE_NETWORK_HEIGHT
MAX_PLATE_UPSCALE = 3.0
TEXT_AREA_HEIGHT_PX = 48

DEFAULT_OCR_CONFIDENCE_THRESHOLD = 0.8

# ============================================================================
# Snapshot Overlay Constants
# ============================================================================
SNAPSHOT_BADGE_BG_COLOR = (0.9, 0.2, 0.2, 0.85)
SNAPSHOT_BADGE_TEXT_COLOR = (1.0, 1.0, 1.0, 1.0)
SNAPSHOT_BADGE_FONT_SIZE = 28
SNAPSHOT_BADGE_PADDING_X = 20
SNAPSHOT_BADGE_PADDING_Y = 12
SNAPSHOT_BADGE_CORNER_RADIUS = 10

# ============================================================================
# Snapshot Button Constants
# ============================================================================
BUTTON_FONT_SIZE = 22
BUTTON_PADDING_X = 28
BUTTON_PADDING_Y = 14
BUTTON_CORNER_RADIUS = 12
BUTTON_MARGIN_BOTTOM = 24

BUTTON_INACTIVE_BG_COLOR = (0.15, 0.15, 0.15, 0.75)
BUTTON_INACTIVE_TEXT_COLOR = (1.0, 1.0, 1.0, 1.0)
BUTTON_INACTIVE_LABEL = "Snapshot"

BUTTON_ACTIVE_BG_COLOR = (0.85, 0.15, 0.15, 0.90)
BUTTON_ACTIVE_TEXT_COLOR = (1.0, 1.0, 1.0, 1.0)
BUTTON_ACTIVE_LABEL = "Resume"

# ============================================================================
# Live Count Overlay Constants
# ============================================================================
VEHICLE_LABELS = {"vehicle", "truck", "bus", "motorcycle"}
COUNT_OVERLAY_BG_COLOR = (0.0, 0.0, 0.0, 0.65)
COUNT_OVERLAY_TEXT_COLOR = (1.0, 1.0, 1.0, 1.0)
COUNT_OVERLAY_FONT_SIZE = 36
COUNT_OVERLAY_PADDING_X = 24
COUNT_OVERLAY_PADDING_Y = 16
COUNT_OVERLAY_MARGIN = 16
COUNT_OVERLAY_CORNER_RADIUS = 12


# ============================================================================
# LPR Panel Data Structures
# ============================================================================
class PanelGeometry(NamedTuple):
    """Pre-computed pixel positions and sizes for the LPR panel."""
    panel_x: float
    panel_y: float
    panel_width: float
    panel_height: float
    slot_width: float
    slot_height: float
    text_area_height: float
    plate_area_height: float
    slot_positions: List[Tuple[float, float]]


@dataclass
class PlateSlot:
    """A single slot in the LPR panel holding a cropped plate image and OCR text."""
    tracking_id: int
    ocr_text: str
    confidence: float
    surface: object = None
    timestamp: float = 0.0

class LPRPanelManager:
    """Manages the rolling display of cropped license plates in the LPR panel."""

    def __init__(self, dedup_window_seconds: float = 0.0):
        self.slots: List[Optional[PlateSlot]] = [None] * SLOT_COUNT
        self.next_slot_index: int = 0
        self.tracking_id_to_slot: Dict[int, int] = {}
        self.ocr_to_slot: Dict[str, int] = {}
        self.dedup_window_seconds: float = dedup_window_seconds

    def _is_dedup_enabled(self) -> bool:
        return self.dedup_window_seconds > 0

    def _is_within_dedup_window(self, slot: PlateSlot) -> bool:
        if not self._is_dedup_enabled():
            return False
        return (time.monotonic() - slot.timestamp) < self.dedup_window_seconds

    def _cleanup_expired_entries(self):
        if not self._is_dedup_enabled():
            return
        now = time.monotonic()
        expired_ocr_keys = []
        for ocr_text, slot_index in self.ocr_to_slot.items():
            slot = self.slots[slot_index]
            if slot is None or (now - slot.timestamp) >= self.dedup_window_seconds:
                expired_ocr_keys.append(ocr_text)
        for key in expired_ocr_keys:
            del self.ocr_to_slot[key]

    def should_update(self, tracking_id: int, ocr_text: str, confidence: float = 0.0) -> bool:
        if tracking_id in self.tracking_id_to_slot:
            slot_index = self.tracking_id_to_slot[tracking_id]
            existing_slot = self.slots[slot_index]
            if existing_slot is not None and existing_slot.ocr_text == ocr_text:
                return False

        if self._is_dedup_enabled():
            self._cleanup_expired_entries()
            if ocr_text in self.ocr_to_slot:
                dup_slot_index = self.ocr_to_slot[ocr_text]
                dup_slot = self.slots[dup_slot_index]
                if dup_slot is not None and self._is_within_dedup_window(dup_slot):
                    if confidence <= dup_slot.confidence:
                        return False

        return True

    def add_or_update_plate(self, tracking_id: int, ocr_text: str,
                            confidence: float, cropped_surface) -> int:
        now = time.monotonic()

        if tracking_id in self.tracking_id_to_slot:
            slot_index = self.tracking_id_to_slot[tracking_id]
        elif self._is_dedup_enabled() and ocr_text in self.ocr_to_slot:
            dup_slot_index = self.ocr_to_slot[ocr_text]
            dup_slot = self.slots[dup_slot_index]
            if dup_slot is not None and self._is_within_dedup_window(dup_slot):
                slot_index = dup_slot_index
                if dup_slot.tracking_id in self.tracking_id_to_slot:
                    del self.tracking_id_to_slot[dup_slot.tracking_id]
            else:
                slot_index = self._allocate_new_slot()
        else:
            slot_index = self._allocate_new_slot()

        old_slot = self.slots[slot_index]
        if old_slot is not None and old_slot.ocr_text in self.ocr_to_slot:
            if self.ocr_to_slot[old_slot.ocr_text] == slot_index:
                del self.ocr_to_slot[old_slot.ocr_text]

        self.slots[slot_index] = PlateSlot(
            tracking_id=tracking_id,
            ocr_text=ocr_text,
            confidence=confidence,
            surface=cropped_surface,
            timestamp=now,
        )
        self.tracking_id_to_slot[tracking_id] = slot_index
        if self._is_dedup_enabled():
            self.ocr_to_slot[ocr_text] = slot_index
        return slot_index

    def _allocate_new_slot(self) -> int:
        slot_index = self.next_slot_index
        old_slot = self.slots[slot_index]
        if old_slot is not None:
            if old_slot.tracking_id in self.tracking_id_to_slot:
                del self.tracking_id_to_slot[old_slot.tracking_id]
            if old_slot.ocr_text in self.ocr_to_slot and self.ocr_to_slot[old_slot.ocr_text] == slot_index:
                del self.ocr_to_slot[old_slot.ocr_text]
        self.next_slot_index = (self.next_slot_index + 1) % SLOT_COUNT
        return slot_index

    def get_filled_slots(self) -> List[Tuple[int, PlateSlot]]:
        return [(i, slot) for i, slot in enumerate(self.slots) if slot is not None]


# ============================================================================
# Geometry Helpers
# ============================================================================
def compute_panel_geometry(video_width: int, video_height: int,
                           position: str = "right") -> PanelGeometry:
    """Compute pixel positions and sizes for the LPR panel given video dimensions."""
    inner_width = video_width * PANEL_WIDTH_RATIO - 2 * SLOT_PADDING_PX
    slot_width = inner_width

    plate_area_height = slot_width / PLATE_ASPECT_RATIO
    max_plate_height = PLATE_NETWORK_HEIGHT * MAX_PLATE_UPSCALE
    if plate_area_height > max_plate_height:
        plate_area_height = max_plate_height
        slot_width = plate_area_height * PLATE_ASPECT_RATIO

    text_area_height = TEXT_AREA_HEIGHT_PX
    slot_height = plate_area_height + text_area_height

    panel_width = slot_width + 2 * SLOT_PADDING_PX
    panel_height = (SLOT_COUNT * slot_height
                    + (SLOT_COUNT - 1) * SLOT_PADDING_PX
                    + 2 * SLOT_PADDING_PX)

    if position == "left":
        panel_x = PANEL_MARGIN_PX
    else:
        panel_x = video_width - panel_width - PANEL_MARGIN_PX
    panel_y = PANEL_MARGIN_PX

    slot_positions = []
    for i in range(SLOT_COUNT):
        sx = panel_x + SLOT_PADDING_PX
        sy = panel_y + SLOT_PADDING_PX + i * (slot_height + SLOT_PADDING_PX)
        slot_positions.append((sx, sy))

    return PanelGeometry(
        panel_x=panel_x,
        panel_y=panel_y,
        panel_width=panel_width,
        panel_height=panel_height,
        slot_width=slot_width,
        slot_height=slot_height,
        text_area_height=text_area_height,
        plate_area_height=plate_area_height,
        slot_positions=slot_positions,
    )


# ============================================================================
# Plate Cropping
# ============================================================================
def crop_plate_surface(source_surface, bbox_x: float, bbox_y: float,
                       bbox_w: float, bbox_h: float,
                       target_width: float, target_height: float):
    """Crop a license plate region from the source surface and scale it to target size."""
    if bbox_w <= 0 or bbox_h <= 0:
        return None

    target_w = int(target_width)
    target_h = int(target_height)

    cropped = cairo.ImageSurface(cairo.FORMAT_ARGB32, target_w, target_h)
    ctx = cairo.Context(cropped)

    scale_x = target_w / bbox_w
    scale_y = target_h / bbox_h
    ctx.scale(scale_x, scale_y)
    ctx.set_source_surface(source_surface, -bbox_x, -bbox_y)
    ctx.get_source().set_filter(cairo.FILTER_BILINEAR)
    ctx.paint()

    return cropped


# ============================================================================
# Detection Counting
# ============================================================================
def count_detections(detections) -> Tuple[int, int]:
    """Recursively count vehicles and license plates in a detection tree."""
    vehicle_count = 0
    plate_count = 0
    for det in detections:
        if det.label in VEHICLE_LABELS:
            vehicle_count += 1
        if det.label == "license_plate":
            plate_count += 1
        if det.detections:
            nested_vehicles, nested_plates = count_detections(det.detections)
            vehicle_count += nested_vehicles
            plate_count += nested_plates
    return vehicle_count, plate_count


# ============================================================================
# Drawing Helpers
# ============================================================================
def _draw_rounded_rect(ctx, x: float, y: float, w: float, h: float, radius: float):
    """Draw a rounded rectangle path on the given cairo context."""
    r = min(radius, w / 2, h / 2)
    ctx.new_sub_path()
    ctx.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    ctx.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    ctx.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    ctx.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    ctx.close_path()


def draw_count_overlay(ctx, vehicle_count: int, plate_count: int):
    """Draw a semi-transparent badge at the top-left showing vehicle and plate counts."""
    text = f"Vehicles: {vehicle_count}  |  Plates: {plate_count}"
    ctx.set_font_size(COUNT_OVERLAY_FONT_SIZE)
    extents = ctx.text_extents(text)

    badge_x = COUNT_OVERLAY_MARGIN
    badge_y = COUNT_OVERLAY_MARGIN
    badge_w = extents.width + 2 * COUNT_OVERLAY_PADDING_X
    badge_h = extents.height + 2 * COUNT_OVERLAY_PADDING_Y

    _draw_rounded_rect(ctx, badge_x, badge_y, badge_w, badge_h, COUNT_OVERLAY_CORNER_RADIUS)
    ctx.set_source_rgba(*COUNT_OVERLAY_BG_COLOR)
    ctx.fill()

    text_x = badge_x + COUNT_OVERLAY_PADDING_X
    text_y = badge_y + COUNT_OVERLAY_PADDING_Y + extents.height
    ctx.set_source_rgba(*COUNT_OVERLAY_TEXT_COLOR)
    ctx.move_to(text_x, text_y)
    ctx.show_text(text)


def draw_snapshot_button(ctx, width: int, height: int, is_active: bool) -> Tuple[float, float, float, float]:
    """Draw a clickable snapshot button at the bottom-center of the video."""
    if is_active:
        label = BUTTON_ACTIVE_LABEL
        bg_color = BUTTON_ACTIVE_BG_COLOR
        text_color = BUTTON_ACTIVE_TEXT_COLOR
    else:
        label = BUTTON_INACTIVE_LABEL
        bg_color = BUTTON_INACTIVE_BG_COLOR
        text_color = BUTTON_INACTIVE_TEXT_COLOR

    ctx.set_font_size(BUTTON_FONT_SIZE)
    extents = ctx.text_extents(label)

    button_w = extents.width + 2 * BUTTON_PADDING_X
    button_h = extents.height + 2 * BUTTON_PADDING_Y
    button_x = (width - button_w) / 2
    button_y = height - button_h - BUTTON_MARGIN_BOTTOM

    _draw_rounded_rect(ctx, button_x, button_y, button_w, button_h, BUTTON_CORNER_RADIUS)
    ctx.set_source_rgba(*bg_color)
    ctx.fill()

    _draw_rounded_rect(ctx, button_x, button_y, button_w, button_h, BUTTON_CORNER_RADIUS)
    ctx.set_source_rgba(1.0, 1.0, 1.0, 0.3)
    ctx.set_line_width(1.5)
    ctx.stroke()

    text_x = button_x + BUTTON_PADDING_X
    text_y = button_y + BUTTON_PADDING_Y + extents.height
    ctx.set_source_rgba(*text_color)
    ctx.move_to(text_x, text_y)
    ctx.show_text(label)

    return (button_x, button_y, button_w, button_h)


# ============================================================================
# LPR Plate Extraction
# ============================================================================
def find_license_plates(detections,
                        ocr_confidence_threshold: float):
    """Recursively find all license_plate detections with valid OCR classifications.

    Returns: list of (Detection proto, Classification proto) pairs.
    """
    results = []
    for det in detections:
        if det.label == "license_plate" and det.classifications:
            best_ocr = _get_best_ocr_classification(det.classifications, ocr_confidence_threshold)
            if best_ocr is not None:
                results.append((det, best_ocr))
        if det.detections:
            results.extend(find_license_plates(det.detections, ocr_confidence_threshold))
    return results


def _get_best_ocr_classification(classifications, threshold: float):
    """Return the highest-confidence OCR classification above threshold, or None.

    classifications is an iterable of hailo_analytics.Classification proto messages.
    """
    best = None
    for cls in classifications:
        if cls.type != "ocr":
            continue
        if cls.confidence < threshold:
            continue
        if best is None or cls.confidence > best.confidence:
            best = cls
    return best


# ============================================================================
# Panel Drawing
# ============================================================================
def draw_lpr_panel(ctx, geom: PanelGeometry, lpr_manager: LPRPanelManager):
    """Draw the LPR panel overlay with all filled plate slots."""
    _draw_rounded_rect(ctx, geom.panel_x, geom.panel_y,
                       geom.panel_width, geom.panel_height, SLOT_CORNER_RADIUS)
    ctx.set_source_rgba(*PANEL_BG_COLOR)
    ctx.fill()

    filled_slots = lpr_manager.get_filled_slots()
    filled_indices = {idx for idx, _ in filled_slots}

    for i in range(SLOT_COUNT):
        if i in filled_indices:
            continue
        sx, sy = geom.slot_positions[i]
        _draw_rounded_rect(ctx, sx, sy, geom.slot_width, geom.slot_height, SLOT_CORNER_RADIUS)
        ctx.set_source_rgba(*EMPTY_SLOT_BORDER_COLOR)
        ctx.set_line_width(EMPTY_SLOT_BORDER_WIDTH)
        ctx.stroke()

    for slot_index, slot in filled_slots:
        sx, sy = geom.slot_positions[slot_index]
        _draw_filled_slot(ctx, sx, sy, geom, slot)


def _draw_filled_slot(ctx, sx: float, sy: float, geom: PanelGeometry, slot: PlateSlot):
    """Draw a single filled slot: white text area with OCR text, then the plate crop."""
    slot_w = geom.slot_width
    text_h = geom.text_area_height
    plate_h = geom.plate_area_height

    ctx.save()
    _draw_rounded_rect(ctx, sx, sy, slot_w, geom.slot_height, SLOT_CORNER_RADIUS)
    ctx.clip()

    ctx.set_source_rgba(*TEXT_BG_COLOR)
    ctx.rectangle(sx, sy, slot_w, text_h)
    ctx.fill()

    ocr_font_size = max(OCR_FONT_MIN_PX, text_h * OCR_FONT_SIZE_RATIO)
    ctx.set_source_rgba(*TEXT_COLOR)

    combined_text = f"{slot.ocr_text}  ({slot.confidence:.2f})"
    layout = PangoCairo.create_layout(ctx)
    font_desc = Pango.FontDescription.new()
    font_desc.set_absolute_size(ocr_font_size * Pango.SCALE)
    layout.set_font_description(font_desc)
    layout.set_text(combined_text, -1)
    _, ext = layout.get_pixel_extents()
    text_x = sx + (slot_w - ext.width) / 2
    text_y = sy + (text_h - ext.height) / 2

    ctx.move_to(text_x, text_y)
    PangoCairo.show_layout(ctx, layout)

    if slot.surface is not None:
        plate_y = sy + text_h
        ctx.set_source_surface(slot.surface, sx, plate_y)
        ctx.paint()

    ctx.restore()


# ============================================================================
# LPR Video Player
# ============================================================================
class LPRVideoPlayer(BaseVideoPlayer):
    """GStreamer-based video player with LPR panel overlay, snapshot, and count display."""

    def __init__(self, ocr_confidence_threshold=DEFAULT_OCR_CONFIDENCE_THRESHOLD,
                 panel_position="right", no_panel=False, dedup_window_seconds=0.0,
                 **kwargs):
        self.ocr_confidence_threshold = ocr_confidence_threshold
        self.panel_position = panel_position
        self.no_panel = no_panel

        # Snapshot (freeze-frame) state
        self._snapshot_active = False
        self._snapshot_lock = threading.Lock()
        self._snapshot_button_bbox = (0.0, 0.0, 0.0, 0.0)
        self._snapshot_button_lock = threading.Lock()
        self._original_terminal_settings = None
        if os.isatty(sys.stdin.fileno()):
            self._original_terminal_settings = termios.tcgetattr(sys.stdin)

        # LPR panel state
        self.lpr_manager = LPRPanelManager(dedup_window_seconds=dedup_window_seconds)
        self.panel_geometry: Optional[PanelGeometry] = None
        self.last_video_width = 0
        self.last_video_height = 0

        # Start keyboard listener for snapshot toggle
        self._keyboard_thread = threading.Thread(target=self._keyboard_listener, daemon=True)
        self._keyboard_thread.start()

        super().__init__(**kwargs)

    def _get_color(self, label: str) -> Tuple[float, float, float, float]:
        """Look up per-class bounding box color."""
        return CLASS_COLORS.get(label, DEFAULT_CLASS_COLOR)

    def draw_classifications(self, classifications, context, x1, y2, bbox_min_dim,
                             pad, draw_scale):
        """Draw classification labels using Pango (supports non-Latin OCR text)."""
        from viewer_common import (CLASSIFICATION_FONT_MIN, CLASSIFICATION_FONT_MAX,
                                   CLASSIFICATION_FONT_SCALE_FACTOR)
        cls_font_sz = max(CLASSIFICATION_FONT_MIN,
                          min(CLASSIFICATION_FONT_MAX * draw_scale,
                              bbox_min_dim * CLASSIFICATION_FONT_SCALE_FACTOR))
        font_desc = Pango.FontDescription.new()
        font_desc.set_absolute_size(cls_font_sz * Pango.SCALE)
        cls_y = y2 + pad
        for cls in classifications:
            cls_label = cls.label
            if cls.type:
                cls_label = f"{cls.type}: {cls_label}"
            if cls.confidence > 0:
                cls_label = f"{cls_label} ({cls.confidence:.2f})"
            layout = PangoCairo.create_layout(context)
            layout.set_font_description(font_desc)
            layout.set_text(cls_label, -1)
            _, cls_ext = layout.get_pixel_extents()
            # Dark background
            context.set_source_rgba(0, 0, 0, 0.6)
            context.rectangle(x1, cls_y, cls_ext.width + pad * 2, cls_ext.height + pad * 2)
            context.fill()
            # Cyan text
            context.set_source_rgba(0, 1, 1, 1)
            context.move_to(x1 + pad, cls_y + pad)
            PangoCairo.show_layout(context, layout)
            cls_y += cls_ext.height + pad * 2

    def _setup_pipeline_probes(self):
        """Add snapshot drop probe and navigation event probe."""
        overlay_sink_pad = self.overlay.get_static_pad("sink")
        overlay_sink_pad.add_probe(Gst.PadProbeType.BUFFER, self._snapshot_drop_probe)

        overlay_src_pad = self.overlay.get_static_pad("src")
        overlay_src_pad.add_probe(Gst.PadProbeType.EVENT_UPSTREAM, self._navigation_event_probe)

        bus = self.pipeline.get_bus()
        bus.connect("message::element", self._on_element_message)

    def draw_frame(self, metadata, context, scale_x, scale_y, draw_scale):
        """Draw detections with per-class colors, count overlay, LPR panel, and snapshot button."""
        # Get actual display dimensions for panel geometry
        sink_pad = self.overlay.get_static_pad("sink")
        caps = sink_pad.get_current_caps()
        if caps:
            struct = caps.get_structure(0)
            actual_w = struct.get_int("width")[1]
            actual_h = struct.get_int("height")[1]

            if actual_w != self.last_video_width or actual_h != self.last_video_height:
                self.panel_geometry = compute_panel_geometry(actual_w, actual_h, self.panel_position)
                self.last_video_width = actual_w
                self.last_video_height = actual_h

        # Crop plates BEFORE drawing detection overlays (clean crop)
        if not self.no_panel:
            with DrawTimer("LPR Crop", self.enable_perf_timing):
                self._process_license_plates(metadata.detections, context, scale_x, scale_y)

        # Draw detection boxes and classifications
        for detection in metadata.detections:
            self.draw_detection(detection, context, scale_x, scale_y, draw_scale,
                                get_color=self._get_color)

        # Draw live vehicle and plate count overlay
        vehicle_count, plate_count = count_detections(metadata.detections)
        draw_count_overlay(context, vehicle_count, plate_count)

        # Draw the LPR panel on top of everything
        if not self.no_panel:
            with DrawTimer("LPR Panel Draw", self.enable_perf_timing):
                if self.panel_geometry:
                    draw_lpr_panel(context, self.panel_geometry, self.lpr_manager)

        # Draw the on-screen snapshot button last
        if caps:
            with self._snapshot_lock:
                snap_active = self._snapshot_active
            button_bbox = draw_snapshot_button(context, actual_w, actual_h, snap_active)
            with self._snapshot_button_lock:
                self._snapshot_button_bbox = button_bbox

    # ----------------------------------------------------------------
    # License Plate Processing
    # ----------------------------------------------------------------
    def _process_license_plates(self, detections, context,
                                scale_x: float, scale_y: float):
        """Scan detections for license plates and update the LPR panel slots."""
        plates = find_license_plates(detections, self.ocr_confidence_threshold)

        for det, ocr_cls in plates:
            if not det.HasField('tracking_id'):
                continue
            tracking_id = det.tracking_id

            ocr_text = ocr_cls.label
            confidence = ocr_cls.confidence

            if not self.lpr_manager.should_update(tracking_id, ocr_text, confidence):
                continue

            bbox_x = det.bbox.xmin * scale_x
            bbox_y = det.bbox.ymin * scale_y
            bbox_w = (det.bbox.xmax - det.bbox.xmin) * scale_x
            bbox_h = (det.bbox.ymax - det.bbox.ymin) * scale_y

            if self.panel_geometry is None:
                continue

            source_surface = context.get_target()
            cropped = crop_plate_surface(
                source_surface,
                bbox_x, bbox_y, bbox_w, bbox_h,
                self.panel_geometry.slot_width,
                self.panel_geometry.plate_area_height,
            )
            if cropped is not None:
                self.lpr_manager.add_or_update_plate(tracking_id, ocr_text, confidence, cropped)

    # ----------------------------------------------------------------
    # Snapshot (Freeze-Frame)
    # ----------------------------------------------------------------
    def _snapshot_drop_probe(self, pad, info):
        with self._snapshot_lock:
            if self._snapshot_active:
                return Gst.PadProbeReturn.DROP
        return Gst.PadProbeReturn.OK

    def _navigation_event_probe(self, pad, info):
        event = info.get_event()
        if event is None:
            return Gst.PadProbeReturn.OK

        if event.type != Gst.EventType.NAVIGATION:
            return Gst.PadProbeReturn.OK

        structure = event.get_structure()
        if structure is None:
            return Gst.PadProbeReturn.OK

        event_type = structure.get_string("event")
        if event_type != "mouse-button-press":
            return Gst.PadProbeReturn.OK

        ok_x, pointer_x = structure.get_double("pointer_x")
        ok_y, pointer_y = structure.get_double("pointer_y")
        if not ok_x or not ok_y:
            return Gst.PadProbeReturn.OK

        with self._snapshot_button_lock:
            bx, by, bw, bh = self._snapshot_button_bbox

        if bw > 0 and bh > 0:
            if bx <= pointer_x <= bx + bw and by <= pointer_y <= by + bh:
                GLib.idle_add(self._toggle_snapshot)

        return Gst.PadProbeReturn.OK

    def _toggle_snapshot(self):
        with self._snapshot_lock:
            self._snapshot_active = not self._snapshot_active
            state = "ON - frame frozen" if self._snapshot_active else "OFF - live"
        print(f"[Snapshot] {state}")

    def _on_element_message(self, bus, message):
        structure = message.get_structure()
        if structure is None:
            return
        struct_name = structure.get_name()

        if "navigation" not in struct_name.lower() and "Navigation" not in struct_name:
            event_field = structure.get_string("event")
            if event_field != "mouse-button-press":
                return
        else:
            event_field = structure.get_string("event")
            if event_field != "mouse-button-press":
                return

        ok_x, pointer_x = structure.get_double("pointer_x")
        ok_y, pointer_y = structure.get_double("pointer_y")
        if not ok_x or not ok_y:
            return

        with self._snapshot_button_lock:
            bx, by, bw, bh = self._snapshot_button_bbox

        if bw > 0 and bh > 0:
            if bx <= pointer_x <= bx + bw and by <= pointer_y <= by + bh:
                self._toggle_snapshot()

    def _keyboard_listener(self):
        if not os.isatty(sys.stdin.fileno()):
            return

        old_settings = termios.tcgetattr(sys.stdin)
        try:
            tty.setcbreak(sys.stdin.fileno())
            print("[Keyboard] Press 's' or SPACE to toggle snapshot, 'q' to quit")
            while True:
                if select.select([sys.stdin], [], [], 0.2)[0]:
                    char = sys.stdin.read(1)
                    if char in ('s', 'S', ' '):
                        self._toggle_snapshot()
                    elif char in ('q', 'Q'):
                        print("[Keyboard] Quit requested")
                        GLib.idle_add(self.loop.quit)
                        break
        except Exception:
            pass
        finally:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)

    # ----------------------------------------------------------------
    # Run (with terminal restore)
    # ----------------------------------------------------------------
    def run(self):
        try:
            super().run()
        finally:
            if self._original_terminal_settings is not None:
                try:
                    termios.tcsetattr(sys.stdin, termios.TCSADRAIN,
                                      self._original_terminal_settings)
                except Exception:
                    pass


# ============================================================================
# Main
# ============================================================================
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='LPR viewer — displays video with license plate panel overlay')
    BaseVideoPlayer.add_common_argparse_args(parser)
    parser.add_argument('--ocr-confidence-threshold', type=float,
                        default=DEFAULT_OCR_CONFIDENCE_THRESHOLD,
                        help=f'Minimum OCR confidence to display in panel (default: {DEFAULT_OCR_CONFIDENCE_THRESHOLD})')
    parser.add_argument('--panel-position', choices=['left', 'right'], default='right',
                        help='Position of the LPR panel (default: right)')
    parser.add_argument('--no-panel', action='store_true',
                        help='Disable the LPR panel (behave like the generic viewer)')
    parser.add_argument('--dedup-window', type=float, default=5.0, metavar='SECONDS',
                        help='Dedup window in seconds for plate panel (0 = disabled, default: 5). '
                             'Plates with the same OCR text or tracking ID within this window are '
                             'deduplicated, keeping the higher-confidence result.')
    args = parser.parse_args()

    output_size = OUTPUT_SIZE_PRESETS[args.output_resolution] if args.output_resolution else None
    app = LPRVideoPlayer(
        udp_port=args.udp_port,
        udp_ip=args.udp_ip,
        enable_perf_timing=args.debug_perf,
        perf_print_frequency=args.perf_print_freq,
        analytic_data_ip=args.analytic_data_ip,
        analytic_data_port=args.analytic_data_port,
        output_size=output_size,
        metadata_transport=args.metadata_transport,
        save_mkv=args.save_mkv,
        record_bitrate=args.record_bitrate,
        ocr_confidence_threshold=args.ocr_confidence_threshold,
        panel_position=args.panel_position,
        no_panel=args.no_panel,
        dedup_window_seconds=args.dedup_window,
    )
    app.run()
