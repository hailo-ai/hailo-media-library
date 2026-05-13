"""
Shared module for Hailo-15 analytic viewer applications.

Contains common constants, Pydantic data models, performance timing utilities,
and the BaseVideoPlayer class that handles GStreamer pipeline construction,
SEI timestamp extraction, metadata synchronization, and detection drawing.

Specialized viewers (general analytic viewer, LPR viewer) subclass BaseVideoPlayer
and override hook methods to add their unique drawing logic.
"""

import sys
import gi
import zmq
import json
import threading
import argparse
import time
from collections import deque
from pydantic import BaseModel, Field, ConfigDict
from typing import List, Optional, Callable

# Generated from hailo-analytics/hailo_analytics_api/src/pipeline/codecs/protos/analytics_metadata.proto.
# Regenerate with:
#   protoc --proto_path=<media-library-root>/hailo-analytics/hailo_analytics_api/src/pipeline/codecs/protos \
#          --python_out=. analytics_metadata.proto
from analytics_metadata_pb2 import Frame as _ProtoFrame
from google.protobuf.json_format import MessageToDict


def _decode_metadata_proto(payload: bytes) -> dict:
    """Parse a serialized hailo_analytics.Frame into a dict with snake_case field names
    (matches the FrameMetadata Pydantic model below).

    Note: MessageToDict renders uint64 fields (e.g. isp_timestamp_ns) as *strings* because
    JSON cannot safely represent 64-bit integers. Downstream sync math needs an int, so we
    coerce it here rather than touching every callsite.
    """
    proto = _ProtoFrame()
    proto.ParseFromString(payload)
    metadata = MessageToDict(proto, preserving_proto_field_name=True, including_default_value_fields=False)
    if TIMESTAMP_KEY in metadata:
        metadata[TIMESTAMP_KEY] = int(metadata[TIMESTAMP_KEY])
    return metadata

# ============================================================================
# GStreamer Initialization
# ============================================================================
gi.require_version('Gst', '1.0')
gi.require_version('GstRtp', '1.0')
from gi.repository import Gst, GstRtp, GLib

import multiprocessing

num_threads = max(1, int(multiprocessing.cpu_count() / 4))

Gst.init(None)

# ============================================================================
# Performance Timing
# ============================================================================
class DrawTimer:
    """Context manager for timing drawing operations with statistics and configurable print frequency."""

    _stats = {}
    _print_every_n_frames = 30
    _frame_counter = 0

    @classmethod
    def set_print_frequency(cls, n_frames):
        """Set how often to print statistics (every N frames)."""
        cls._print_every_n_frames = n_frames

    @classmethod
    def reset_stats(cls):
        """Reset all statistics."""
        cls._stats.clear()
        cls._frame_counter = 0

    def __init__(self, name="Draw", enabled=True, max_samples=60):
        self.name = name
        self.enabled = enabled
        self.start_time = None
        self.max_samples = max_samples
        if enabled and name not in DrawTimer._stats:
            DrawTimer._stats[name] = {
                'times': deque(maxlen=max_samples),
                'frame_count': 0
            }

    def __enter__(self):
        if self.enabled:
            self.start_time = time.perf_counter()
        return self

    def __exit__(self, *args):
        if self.enabled and self.start_time:
            elapsed_ms = (time.perf_counter() - self.start_time) * 1000
            stats = DrawTimer._stats[self.name]
            stats['times'].append(elapsed_ms)
            stats['frame_count'] += 1
            if stats['frame_count'] % DrawTimer._print_every_n_frames == 0:
                times = list(stats['times'])
                if times:
                    min_time = min(times)
                    max_time = max(times)
                    avg_time = sum(times) / len(times)
                    print(f"[PERF] {self.name}: min={min_time:.2f}ms, max={max_time:.2f}ms, avg={avg_time:.2f}ms (over {len(times)} samples)")


# ============================================================================
# Detection Drawing Constants
# ============================================================================
DETECTION_FONT_SCALE_FACTOR = 0.7
DETECTION_FONT_MIN = 10
DETECTION_FONT_MAX = 32

CLASSIFICATION_FONT_SCALE_FACTOR = 0.55
CLASSIFICATION_FONT_MIN = 9
CLASSIFICATION_FONT_MAX = 28

DETECTION_PAD_SCALE_FACTOR = 0.10
DETECTION_PAD_MIN = 2
DETECTION_PAD_MAX = 7

DETECTION_LINE_WIDTH_SCALE_FACTOR = 0.08
DETECTION_LINE_WIDTH_MIN = 1
DETECTION_LINE_WIDTH_MAX = 5

# Output Resolution Presets (name -> (width, height))
OUTPUT_SIZE_PRESETS = {
    "5mp":   (2592, 1944),
    "4k":    (3840, 2160),
    "1080p": (1920, 1080),
    "720p":  (1280, 720),
    "480p":  (640, 480),
}

# ============================================================================
# Network Configuration Defaults
# ============================================================================
DEFAULT_ANALYTIC_DATA_IP = "10.0.0.1"
DEFAULT_ZMQ_PORT = 7000
DEFAULT_WS_PORT = 8765
DEFAULT_UDP_IP = "10.0.0.2"

# ============================================================================
# SEI / Sync Configuration
# ============================================================================
SEI_HAILO_KEY_NODE = "Hailo"
TIMESTAMP_KEY = "isp_timestamp_ns"
UUID_SIZE = 16
JITTER_BUFFER_LATENCY_MS = 800
MAX_METADATA_AGE_NS = 300 * 10**6  # 300ms threshold for stale data
METADATA_BUFFER_SIZE = 150

# Default bounding box color (green)
DEFAULT_DETECTION_COLOR = (0, 1, 0, 0.8)

# ============================================================================
# Pydantic Models
# ============================================================================
class BBox(BaseModel):
    xmin: float = 0.0
    ymin: float = 0.0
    xmax: float = 0.0
    ymax: float = 0.0

class Landmark(BaseModel):
    points_format: str = "x,y,conf"
    points_stride: int = 3
    points: List[float] = Field(default_factory=list)
    pairs: List[int] = Field(default_factory=list)
    model_config = ConfigDict(extra="ignore")

class Classification(BaseModel):
    type: str = ""
    label: str = ""
    confidence: Optional[float] = None
    model_config = ConfigDict(extra="ignore")

class Detection(BaseModel):
    label: str = "obj"
    bbox: BBox = Field(default_factory=BBox)
    confidence: Optional[float] = None
    tracking_id: Optional[int] = None
    landmarks: Optional[List[Landmark]] = Field(default_factory=list)
    classifications: Optional[List[Classification]] = Field(default_factory=list)
    detections: Optional[List['Detection']] = Field(default_factory=list)
    model_config = ConfigDict(extra="ignore")

class FrameMetadata(BaseModel):
    frame_width: int = Field(default=1920)
    frame_height: int = Field(default=1080)
    detections: List[Detection] = Field(default_factory=list)
    landmarks: Optional[List[Landmark]] = Field(default_factory=list)
    isp_timestamp_ns: Optional[int] = None
    model_config = ConfigDict(extra="ignore")

Detection.model_rebuild()


# ============================================================================
# Base Video Player
# ============================================================================
class BaseVideoPlayer:
    """GStreamer-based video player that receives H.264 via UDP RTP and
    synchronizes analytics metadata via ZMQ using SEI timestamps.

    Subclasses override `draw_frame` to implement their specific drawing logic,
    and optionally override `_start_listeners` and `_setup_pipeline_probes`
    for custom transport or probe behavior.
    """

    def __init__(self, udp_port=5000, udp_ip=DEFAULT_UDP_IP,
                 enable_perf_timing=False, perf_print_frequency=30,
                 analytic_data_ip=DEFAULT_ANALYTIC_DATA_IP,
                 analytic_data_port=None, output_size=None,
                 metadata_transport="zmq", save_mkv=None,
                 record_bitrate=8000):
        self.loop = GLib.MainLoop()
        self.udp_port = udp_port
        self.udp_ip = udp_ip
        self.enable_perf_timing = enable_perf_timing
        self.output_size = output_size
        self.metadata_transport = metadata_transport

        # Set default port based on transport if not explicitly provided
        if analytic_data_port is None:
            analytic_data_port = DEFAULT_WS_PORT if metadata_transport == "ws" else DEFAULT_ZMQ_PORT
        self.analytic_data_port = analytic_data_port
        self.analytic_data_ip = analytic_data_ip

        if enable_perf_timing:
            DrawTimer.set_print_frequency(perf_print_frequency)

        # Metadata stream (temporal queue) — transport-agnostic naming
        self.metadata_buffer = deque(maxlen=METADATA_BUFFER_SIZE)
        self.metadata_lock = threading.Lock()

        # Frame mapping (PTS -> ISP timestamp from SEI)
        self.pts_to_sei_ts = {}
        self.registry_lock = threading.Lock()

        # State tracking for smoothing
        self.current_active_metadata = None

        # Recording
        self.save_mkv = save_mkv
        self.record_bitrate = record_bitrate

        # Metadata listener addresses
        self.zmq_address = f"tcp://{analytic_data_ip}:{analytic_data_port}"
        self.ws_url = f"ws://{analytic_data_ip}:{analytic_data_port}"

        # Start metadata listeners (subclasses can override)
        self._start_listeners()

        # Build GStreamer pipeline
        scale_element = ""
        if output_size:
            output_width, output_height = output_size
            scale_element = f"videoscale ! video/x-raw,width={output_width},height={output_height},pixel-aspect-ratio=1/1 ! "

        if self.save_mkv:
            sink_str = (
                f"tee name=display_tee ! "
                f"queue max-size-buffers=5 ! {scale_element}autovideosink sync=true "
                f"display_tee. ! queue max-size-buffers=5 ! "
                f"videoconvert n-threads={num_threads} ! "
                f"x264enc tune=zerolatency speed-preset=ultrafast bitrate={self.record_bitrate} ! "
                f"matroskamux ! filesink location=\"{self.save_mkv}\""
            )
        else:
            sink_str = f"{scale_element}autovideosink sync=true"

        pipeline_str = f"""
            udpsrc address={self.udp_ip} port={self.udp_port} buffer-size=2097152
                caps="application/x-rtp, media=(string)video, clock-rate=(int)90000,
                      encoding-name=(string)H264, payload=(int)96" !
            rtpjitterbuffer name=jitterbuffer latency={JITTER_BUFFER_LATENCY_MS} !
            rtph264depay !
            h264parse name=parser !
            video/x-h264,stream-format=byte-stream !
            queue leaky=downstream max-size-buffers=30 !
            avdec_h264 !
            videoconvert n-threads={num_threads} !
            queue max-size-buffers=5 !
            cairooverlay name=overlay !
            videoconvert n-threads={num_threads} !
            {sink_str}
        """

        try:
            self.pipeline = Gst.parse_launch(pipeline_str)
            size_str = f"{output_size[0]}x{output_size[1]}" if output_size else "native"
            record_str = f", Recording: {self.save_mkv}" if self.save_mkv else ""
            metadata_addr = self.ws_url if metadata_transport == "ws" else self.zmq_address
            print(f"Pipeline created. UDP: {self.udp_ip}:{self.udp_port}, "
                  f"Metadata: {metadata_addr}, Latency: {JITTER_BUFFER_LATENCY_MS}ms, "
                  f"Output: {size_str}{record_str}")
        except Exception as e:
            print(f"Error creating pipeline: {e}")
            sys.exit(1)

        self.overlay = self.pipeline.get_by_name("overlay")
        self.overlay.connect("draw", self._on_draw)

        parser = self.pipeline.get_by_name("parser")
        parser.get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, self._sei_probe_callback)

        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self._on_message)

        # Allow subclasses to add extra probes/handlers
        self._setup_pipeline_probes()

    # ----------------------------------------------------------------
    # Hook methods for subclasses
    # ----------------------------------------------------------------
    def _start_listeners(self):
        """Start metadata listener thread (ZMQ or WebSocket based on transport setting)."""
        if self.metadata_transport == "ws":
            self.listener_thread = threading.Thread(target=self._ws_listener, daemon=True)
        else:
            self.listener_thread = threading.Thread(target=self._zmq_listener, daemon=True)
        self.listener_thread.start()

    def _setup_pipeline_probes(self):
        """Called after pipeline construction. Override to add extra pad probes or bus handlers."""
        pass

    def draw_frame(self, metadata, context, scale_x, scale_y, draw_scale):
        """Called per frame with synchronized metadata. Override to implement drawing logic.

        Args:
            metadata: Validated FrameMetadata instance.
            context: Cairo drawing context.
            scale_x: Horizontal scale factor (display width / metadata width).
            scale_y: Vertical scale factor (display height / metadata height).
            draw_scale: Geometric mean of scale_x and scale_y, for sizing elements.
        """
        for detection in metadata.detections:
            self.draw_detection(detection, context, scale_x, scale_y, draw_scale)

    def _post_draw_detection(self, det, context, scale_x, scale_y, draw_scale):
        """Called after drawing each detection. Override to add landmarks or other overlays."""
        pass

    # ----------------------------------------------------------------
    # ZMQ Metadata Listener
    # ----------------------------------------------------------------
    def _zmq_listener(self):
        context = zmq.Context()
        socket = context.socket(zmq.SUB)
        try:
            socket.connect(self.zmq_address)
            socket.setsockopt_string(zmq.SUBSCRIBE, "")
            socket.setsockopt(zmq.RCVTIMEO, 1000)
            print(f"ZMQ connected to: {self.zmq_address}")

            while True:
                try:
                    message = socket.recv()
                    metadata = _decode_metadata_proto(message)
                    ts = metadata.get(TIMESTAMP_KEY)
                    if ts:
                        with self.metadata_lock:
                            self.metadata_buffer.append((ts, metadata))
                except zmq.Again:
                    continue
                except Exception as e:
                    print(f"ZMQ Unpack Error: {e}")
        except Exception as e:
            print(f"ZMQ Error: {e}")

    # ----------------------------------------------------------------
    # WebSocket Metadata Listener
    # ----------------------------------------------------------------
    def _ws_listener(self):
        """WebSocket metadata listener with auto-reconnect.

        The server ships hailo_analytics.Frame protobuf messages in binary WebSocket frames,
        so `ws.recv()` returns bytes that we parse directly with the generated protobuf type.
        """
        import websocket
        while True:
            try:
                ws = websocket.WebSocket()
                ws.connect(self.ws_url)
                print(f"WebSocket connected to: {self.ws_url}")
                while True:
                    message = ws.recv()
                    if isinstance(message, str):
                        # Legacy / stray text frame — ignore so downstream stays on the binary path.
                        continue
                    metadata = _decode_metadata_proto(message)
                    ts = metadata.get(TIMESTAMP_KEY)
                    if ts:
                        with self.metadata_lock:
                            self.metadata_buffer.append((ts, metadata))
            except Exception as e:
                print(f"WebSocket Error: {e}, reconnecting...")
                time.sleep(1)

    # ----------------------------------------------------------------
    # SEI Timestamp Extraction
    # ----------------------------------------------------------------
    def _sei_probe_callback(self, pad, info):
        gst_buffer = info.get_buffer()
        if not gst_buffer:
            return Gst.PadProbeReturn.OK
        pts = gst_buffer.pts
        success, map_info = gst_buffer.map(Gst.MapFlags.READ)
        if success:
            try:
                nals = map_info.data.split(b'\x00\x00\x01')
                for nal in nals:
                    if len(nal) > 0 and (nal[0] & 0x1F) == 6:
                        self._parse_sei_nal(nal[1:], pts)
            finally:
                gst_buffer.unmap(map_info)
        return Gst.PadProbeReturn.OK

    def _parse_sei_nal(self, payload, pts):
        rbsp = payload.replace(b'\x00\x00\x03', b'\x00\x00')
        i, size = 0, len(rbsp)
        while i < size:
            p_type = 0
            while i < size and rbsp[i] == 0xFF:
                p_type += 255
                i += 1
            if i >= size:
                break
            p_type += rbsp[i]
            i += 1
            p_size = 0
            while i < size and rbsp[i] == 0xFF:
                p_size += 255
                i += 1
            if i >= size:
                break
            p_size += rbsp[i]
            i += 1

            if p_type == 5:
                user_data = rbsp[i : i + p_size]
                if len(user_data) > UUID_SIZE:
                    try:
                        json_str = user_data[UUID_SIZE:].decode('utf-8', errors='ignore').rstrip('\x00')
                        sei_ts = json.loads(json_str).get(SEI_HAILO_KEY_NODE, {}).get(TIMESTAMP_KEY)
                        if sei_ts:
                            with self.registry_lock:
                                self.pts_to_sei_ts[pts] = sei_ts
                                if len(self.pts_to_sei_ts) > 100:
                                    self.pts_to_sei_ts = {
                                        k: v for k, v in self.pts_to_sei_ts.items()
                                        if k > pts - 2 * 10**9
                                    }
                    except Exception:
                        pass
            i += p_size

    # ----------------------------------------------------------------
    # Metadata Synchronization
    # ----------------------------------------------------------------
    def _find_best_metadata(self, sei_ts):
        """Find the best metadata match for the given SEI timestamp (zero-order hold)."""
        best_metadata = None
        with self.metadata_lock:
            while self.metadata_buffer and self.metadata_buffer[0][0] <= sei_ts:
                ts, meta = self.metadata_buffer.popleft()
                if abs(sei_ts - ts) < MAX_METADATA_AGE_NS:
                    best_metadata = meta

            if not best_metadata and self.current_active_metadata:
                prev_ts = self.current_active_metadata.get(TIMESTAMP_KEY, 0)
                if abs(sei_ts - prev_ts) < MAX_METADATA_AGE_NS:
                    best_metadata = self.current_active_metadata

            if best_metadata:
                self.current_active_metadata = best_metadata

        return best_metadata

    # ----------------------------------------------------------------
    # Drawing Callback
    # ----------------------------------------------------------------
    def _on_draw(self, overlay, context, timestamp, duration):
        sei_ts = None
        with self.registry_lock:
            sei_ts = self.pts_to_sei_ts.get(timestamp)

        if not sei_ts:
            return

        best_metadata = self._find_best_metadata(sei_ts)
        if not best_metadata:
            return

        sink_pad = overlay.get_static_pad("sink")
        caps = sink_pad.get_current_caps()
        if not caps:
            return

        struct = caps.get_structure(0)
        actual_w = struct.get_int("width")[1]
        actual_h = struct.get_int("height")[1]

        try:
            metadata = FrameMetadata.model_validate(best_metadata)
            scale_x = actual_w / (metadata.frame_width or 1)
            scale_y = actual_h / (metadata.frame_height or 1)
            draw_scale = (scale_x * scale_y) ** 0.5

            with DrawTimer("Frame Draw", self.enable_perf_timing):
                self.draw_frame(metadata, context, scale_x, scale_y, draw_scale)
        except Exception as e:
            print(f"Draw Error: {e}")

    # ----------------------------------------------------------------
    # Detection Drawing
    # ----------------------------------------------------------------
    def draw_detection(self, det, context, scale_x, scale_y, draw_scale=1.0,
                       get_color=None):
        """Draw a detection bounding box with label and classifications.

        Args:
            get_color: Optional callable(label) -> (r, g, b, a). Defaults to green.
        """
        x1, y1 = det.bbox.xmin * scale_x, det.bbox.ymin * scale_y
        x2, y2 = det.bbox.xmax * scale_x, det.bbox.ymax * scale_y
        w, h = x2 - x1, y2 - y1

        bbox_min_dim = min(w, h)
        font_sz = max(DETECTION_FONT_MIN, min(DETECTION_FONT_MAX * draw_scale,
                                               bbox_min_dim * DETECTION_FONT_SCALE_FACTOR))
        pad = max(DETECTION_PAD_MIN, min(DETECTION_PAD_MAX * draw_scale,
                                          bbox_min_dim * DETECTION_PAD_SCALE_FACTOR))
        box_lw = max(DETECTION_LINE_WIDTH_MIN, min(DETECTION_LINE_WIDTH_MAX * draw_scale,
                                                    bbox_min_dim * DETECTION_LINE_WIDTH_SCALE_FACTOR))

        color = get_color(det.label) if get_color else DEFAULT_DETECTION_COLOR

        # Bounding box
        context.set_source_rgba(*color)
        context.set_line_width(box_lw)
        context.rectangle(x1, y1, w, h)
        context.stroke()

        # Label
        label = f"{det.label} {det.confidence:.2f}" if det.confidence is not None else det.label
        if det.tracking_id is not None:
            label = f"[{det.tracking_id}] {label}"
        context.set_font_size(font_sz)
        ext = context.text_extents(label)

        context.set_source_rgba(*color)
        context.rectangle(x1, y1 - ext.height - pad, ext.width + pad * 2, ext.height + pad)
        context.fill()

        context.set_source_rgba(0, 0, 0, 1)
        context.move_to(x1 + pad, y1 - pad)
        context.show_text(label)

        # Classifications below the bounding box
        if det.classifications:
            self.draw_classifications(det.classifications, context, x1, y2, bbox_min_dim,
                                      pad, draw_scale)

        # Hook for subclass-specific post-detection drawing (e.g. landmarks)
        self._post_draw_detection(det, context, scale_x, scale_y, draw_scale)

        # Nested detections
        if det.detections:
            for nested in det.detections:
                self.draw_detection(nested, context, scale_x, scale_y, draw_scale,
                                    get_color=get_color)

    def draw_classifications(self, classifications, context, x1, y2, bbox_min_dim,
                             pad, draw_scale):
        """Draw classification labels below a detection bounding box."""
        cls_font_sz = max(CLASSIFICATION_FONT_MIN,
                          min(CLASSIFICATION_FONT_MAX * draw_scale,
                              bbox_min_dim * CLASSIFICATION_FONT_SCALE_FACTOR))
        context.set_font_size(cls_font_sz)
        cls_y = y2 + cls_font_sz + pad
        for cls in classifications:
            cls_label = cls.label
            if cls.type:
                cls_label = f"{cls.type}: {cls_label}"
            if cls.confidence is not None:
                cls_label = f"{cls_label} ({cls.confidence:.2f})"
            cls_ext = context.text_extents(cls_label)
            # Dark background
            context.set_source_rgba(0, 0, 0, 0.6)
            context.rectangle(x1, cls_y - cls_ext.height - pad,
                              cls_ext.width + pad * 2, cls_ext.height + pad * 2)
            context.fill()
            # Cyan text
            context.set_source_rgba(0, 1, 1, 1)
            context.move_to(x1 + pad, cls_y)
            context.show_text(cls_label)
            cls_y += cls_ext.height + pad * 2

    # ----------------------------------------------------------------
    # GStreamer Bus Messages
    # ----------------------------------------------------------------
    def _on_message(self, bus, message):
        t = message.type
        if t == Gst.MessageType.EOS:
            self.loop.quit()
        elif t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print(f"GStreamer Error: {err}")
            self.loop.quit()

    # ----------------------------------------------------------------
    # Run
    # ----------------------------------------------------------------
    def run(self):
        self.pipeline.set_state(Gst.State.PLAYING)
        try:
            self.loop.run()
        except KeyboardInterrupt:
            pass
        if self.save_mkv:
            print(f"Finalizing recording: {self.save_mkv}")
            self.pipeline.send_event(Gst.Event.new_eos())
            bus = self.pipeline.get_bus()
            bus.timed_pop_filtered(5 * Gst.SECOND,
                                   Gst.MessageType.EOS | Gst.MessageType.ERROR)
            print(f"Recording saved: {self.save_mkv}")
        self.pipeline.set_state(Gst.State.NULL)

    # ----------------------------------------------------------------
    # Argparse Helpers
    # ----------------------------------------------------------------
    @staticmethod
    def add_common_argparse_args(parser):
        """Add CLI arguments common to all viewer applications."""
        parser.add_argument('-u', '--udp-port', type=int, default=5000,
                            help='UDP port to receive video stream (default: 5000)')
        parser.add_argument('--analytic-data-ip', type=str, default=DEFAULT_ANALYTIC_DATA_IP,
                            help=f'IP address for analytic data connection (default: {DEFAULT_ANALYTIC_DATA_IP})')
        parser.add_argument('--analytic-data-port', type=int, default=None,
                            help=f'Port for analytic data connection (default: {DEFAULT_ZMQ_PORT} for zmq, {DEFAULT_WS_PORT} for ws)')
        parser.add_argument('--udp-ip', type=str, default=DEFAULT_UDP_IP,
                            help=f'IP address for UDP video source (default: {DEFAULT_UDP_IP})')
        parser.add_argument('--output-resolution', choices=OUTPUT_SIZE_PRESETS.keys(), default=None,
                            help='Output resolution (default: native, no scaling)')
        parser.add_argument('--metadata-transport', type=str, default='zmq', choices=['zmq', 'ws'],
                            help='Metadata transport protocol: zmq (default) or ws (websocket)')
        parser.add_argument('--save-mkv', type=str, default=None, metavar='FILE',
                            help='Save video stream with overlays to an MKV file')
        parser.add_argument('--record-bitrate', type=int, default=8000, metavar='KBPS',
                            help='Recording bitrate in kbps (default: 8000)')
        parser.add_argument('--debug-perf', action='store_true',
                            help='Enable performance timing for drawing operations')
        parser.add_argument('--perf-print-freq', type=int, default=30, metavar='N',
                            help='Print performance statistics every N frames (default: 30)')
