import sys
import os
import gi
import cairo
import zmq
import json
import msgpack
import threading
import argparse
import time
from collections import deque
from pydantic import BaseModel, Field, ValidationError, ConfigDict
from typing import List, Optional
import pydantic

# ============================================================================
# Performance Timing for DEBUG purposes
# ============================================================================
class DrawTimer:
    """Context manager for timing drawing operations with statistics and configurable print frequency"""
    
    # Class-level storage for statistics across all timers
    _stats = {}  # {name: {'times': deque, 'frame_count': int}}
    _print_every_n_frames = 30  # Print statistics every N frames
    _frame_counter = 0
    
    @classmethod
    def set_print_frequency(cls, n_frames):
        """Set how often to print statistics (every N frames)"""
        cls._print_every_n_frames = n_frames
    
    @classmethod
    def reset_stats(cls):
        """Reset all statistics"""
        cls._stats.clear()
        cls._frame_counter = 0
    
    def __init__(self, name="Draw", enabled=True, max_samples=60):
        self.name = name
        self.enabled = enabled
        self.start_time = None
        self.max_samples = max_samples
        
        # Initialize stats for this timer if not exists
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
            
            # Store timing
            stats = DrawTimer._stats[self.name]
            stats['times'].append(elapsed_ms)
            stats['frame_count'] += 1
            
            # Print statistics at the configured frequency
            if stats['frame_count'] % DrawTimer._print_every_n_frames == 0:
                times = list(stats['times'])
                if times:
                    min_time = min(times)
                    max_time = max(times)
                    avg_time = sum(times) / len(times)
                    print(f"[PERF] {self.name}: min={min_time:.2f}ms, max={max_time:.2f}ms, avg={avg_time:.2f}ms (over {len(times)} samples)")
# ============================================================================

# Note that the import order is important here, do not reorder it
gi.require_version('Gst', '1.0')
gi.require_version('GstRtp', '1.0')
from gi.repository import Gst, GstRtp, GLib

import multiprocessing

# Calculate threads with limitation (minimum 1)
num_threads = max(1, int(multiprocessing.cpu_count() / 4))

# Face Landmark Indices for Minimal Drawing
# Face Silhouette (Outer edge) - 36 points
FACIAL_OUTLINE = [
    10,  338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 
    400, 377, 152, 148, 176, 149, 150, 136, 172, 58,  132, 93,  234, 127, 162, 21, 
    54,  103, 67, 109
]

# Lips (Outer) - 20 points
LIPS_OUTER = [61, 146, 91, 181, 84, 17, 314, 405, 321, 375, 291, 185, 40, 39, 37, 0, 267, 269, 270, 409]

# Left Eye & Eyebrow - 11 points
LEFT_EYE = [33, 160, 158, 133, 153, 144]
LEFT_EYEBROW = [70, 63, 105, 66, 107]

# Right Eye & Eyebrow - 11 points
RIGHT_EYE = [362, 385, 387, 263, 373, 380]
RIGHT_EYEBROW = [336, 296, 334, 293, 300]

# Nose - 9 points
NOSE_BRIDGE = [168, 6, 197, 195, 5]
NOSE_TIP = [1, 2, 98, 327]

# Combined list for easy filtering
FACE_LANDMARK_STANDARD = set(FACIAL_OUTLINE + LIPS_OUTER + LEFT_EYE + LEFT_EYEBROW + RIGHT_EYE + RIGHT_EYEBROW + NOSE_BRIDGE + NOSE_TIP)
FACE_LANDMARK_MINIMUM = set(LIPS_OUTER + LEFT_EYE + RIGHT_EYE + NOSE_BRIDGE + NOSE_TIP)

# Adaptive Landmark Drawing Size
# Conservative: (1.5, 6)  |  Visible: (2, 8)
LANDMARK_POINT_MIN_RADIUS = 2                   # Minimum point radius in pixels
LANDMARK_POINT_MAX_RADIUS = 8                   # Maximum point radius in pixels
LANDMARK_POINT_SCALE_FACTOR = 0.012             # Point size as % of face width (1.2%)

# ZMQ Configuration Defaults
DEFAULT_ANALYTIC_DATA_IP = "10.0.0.1"
DEFAULT_ANALYTIC_DATA_PORT = 7000

# Configurable Constants
SEI_HAILO_KEY_NODE = "Hailo"                    # Key name for Hailo data in SEI JSON
TIMESTAMP_KEY = "isp_timestamp_ns"              # Key name for timestamp synchronization
UUID_SIZE = 16                                  # Size of UUID prefix in SEI payload

# LATENCY & SYNC CONFIG
JITTER_BUFFER_LATENCY_MS = 300                  # "Time cushion" for ZMQ to arrive (ms)
REGISTRY_CLEANUP_THRESHOLD_NS = 2 * 10**9       # Remove entries older than 2 seconds (in ns)
PREVIOUS_METADATA_MAX_AGE_MS = 200              # Max age for using previous metadata (ms)

Gst.init(None)

# Pydantic Models
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

class Detection(BaseModel):
    label: str = "obj"
    bbox: BBox = Field(default_factory=BBox)
    confidence: Optional[float] = None
    landmarks: Optional[List[Landmark]] = Field(default_factory=list)
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

class VideoPlayer:
    def __init__(self, udp_port=5000, face_landmark_filter=2, enable_perf_timing=False, perf_print_frequency=30, analytic_data_ip=DEFAULT_ANALYTIC_DATA_IP, analytic_data_port=DEFAULT_ANALYTIC_DATA_PORT):
        self.loop = GLib.MainLoop()
        self.udp_port = udp_port
        self.face_landmark_filter = face_landmark_filter  # 0=maximum (all), 1=standard, 2=minimum
        self.enable_perf_timing = enable_perf_timing
        self.zmq_address = f"tcp://{analytic_data_ip}:{analytic_data_port}"
        
        # Configure performance timing
        if enable_perf_timing:
            DrawTimer.set_print_frequency(perf_print_frequency)
        
        # Maps ZMQ isp_timestamp -> AI Metadata
        self.zmq_registry = {}
        self.zmq_lock = threading.Lock()
        
        # Maps GStreamer PTS -> isp_timestamp (from SEI)
        self.pts_to_sei_ts = {}
        self.registry_lock = threading.Lock()
        
        # Previous metadata for fallback
        self.last_metadata = None
        self.last_metadata_ts = None
        
        # Start ZMQ listener
        self.zmq_thread = threading.Thread(target=self.zmq_listener, daemon=True)
        self.zmq_thread.start()

        # PIPELINE
        # Added jitterbuffer latency to allow ZMQ packets to "catch up" to the video frames
        pipeline_str = f"""
            udpsrc port={self.udp_port} buffer-size=2097152 caps="application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264, payload=(int)96" ! 
            rtpjitterbuffer name=jitterbuffer latency={JITTER_BUFFER_LATENCY_MS} ! 
            rtph264depay ! 
            h264parse name=parser ! 
            video/x-h264,stream-format=byte-stream !
            queue leaky=downstream max-size-buffers=10 ! 
            avdec_h264 ! 
            queue max-size-buffers=5 !
            videoconvert n-threads={num_threads} ! 
            cairooverlay name=overlay ! 
            videoconvert n-threads={num_threads} ! 
            autovideosink sync=true
        """

        try:
            self.pipeline = Gst.parse_launch(pipeline_str)
            print(f"Pipeline created. UDP Port: {self.udp_port}, Cushion: {JITTER_BUFFER_LATENCY_MS}ms")
        except Exception as e:
            print(f"Error creating pipeline: {e}")
            sys.exit(1)

        self.overlay = self.pipeline.get_by_name("overlay")
        self.overlay.connect("draw", self.on_draw)

        # SEI Probe setup
        parser = self.pipeline.get_by_name("parser")
        parser_src = parser.get_static_pad("src")
        parser_src.add_probe(Gst.PadProbeType.BUFFER, self.sei_probe_callback)

        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self.on_message)

    def zmq_listener(self):
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
                    metadata = msgpack.unpackb(message, raw=False)
                    
                    ts = metadata.get(TIMESTAMP_KEY)
                    if ts:
                        with self.zmq_lock:
                            self.zmq_registry[ts] = metadata
                            # Periodic cleanup of ZMQ registry
                            if len(self.zmq_registry) > 300:
                                current_ts = ts
                                keys_to_del = [k for k in self.zmq_registry.keys() if k < current_ts - REGISTRY_CLEANUP_THRESHOLD_NS]
                                for k in keys_to_del: del self.zmq_registry[k]
                except zmq.Again:
                    continue
                except Exception as e:
                    print(f"ZMQ Unpack Error: {e}")
        except Exception as e:
            print(f"ZMQ Error: {e}")

    def sei_probe_callback(self, pad, info):
        gst_buffer = info.get_buffer()
        if not gst_buffer: return Gst.PadProbeReturn.OK

        pts = gst_buffer.pts
        success, map_info = gst_buffer.map(Gst.MapFlags.READ)
        if not success: return Gst.PadProbeReturn.OK

        try:
            data = map_info.data
            # Fast check for SEI NAL (0x06)
            nals = data.split(b'\x00\x00\x01')
            for nal in nals:
                if len(nal) > 0 and (nal[0] & 0x1F) == 6:
                    self.parse_sei_nal(nal[1:], pts)
        finally:
            gst_buffer.unmap(map_info)

        return Gst.PadProbeReturn.OK

    def parse_sei_nal(self, payload, pts):
        rbsp = payload.replace(b'\x00\x00\x03', b'\x00\x00')
        i = 0
        size = len(rbsp)
        while i < size:
            payload_type = 0
            while i < size and rbsp[i] == 0xFF:
                payload_type += 255
                i += 1
            if i >= size: break
            payload_type += rbsp[i]
            i += 1
            
            payload_size = 0
            while i < size and rbsp[i] == 0xFF:
                payload_size += 255
                i += 1
            if i >= size: break
            payload_size += rbsp[i]
            i += 1
            
            if payload_type == 5: # User Data Unregistered
                user_data = rbsp[i : i + payload_size]
                if len(user_data) > UUID_SIZE:
                    try:
                        json_str = user_data[UUID_SIZE:].decode('utf-8', errors='ignore').rstrip('\x00')
                        sei_meta = json.loads(json_str)
                        sei_ts = sei_meta.get(SEI_HAILO_KEY_NODE, {}).get(TIMESTAMP_KEY)
                        
                        if sei_ts:
                            with self.registry_lock:
                                self.pts_to_sei_ts[pts] = sei_ts
                                # Cleanup old PTS mappings
                                if len(self.pts_to_sei_ts) > 100:
                                    keys_to_del = [k for k in self.pts_to_sei_ts.keys() if k < pts - REGISTRY_CLEANUP_THRESHOLD_NS]
                                    for k in keys_to_del: del self.pts_to_sei_ts[k]
                    except: pass
            
            i += payload_size
            if i < size and rbsp[i] == 0x80: break

    def on_draw(self, overlay, context, timestamp, duration):
        # 1. Find the SEI Timestamp for this frame's PTS
        sei_ts = None
        with self.registry_lock:
            sei_ts = self.pts_to_sei_ts.get(timestamp)
        
        if not sei_ts:
            return # Frame has no metadata info

        # 2. Find the AI Metadata for that SEI Timestamp
        metadata_dict = None
        using_fallback = False
        with self.zmq_lock:
            metadata_dict = self.zmq_registry.get(sei_ts)
        
        if not metadata_dict:
            # Metadata hasn't arrived via ZMQ yet.
            # Check if we can use previous metadata
            if self.last_metadata and self.last_metadata_ts:
                time_diff_ns = sei_ts - self.last_metadata_ts
                time_diff_ms = time_diff_ns / 1_000_000  # Convert to milliseconds
                if time_diff_ms <= PREVIOUS_METADATA_MAX_AGE_MS:
                    metadata_dict = self.last_metadata
                    using_fallback = True
                else:
                    return  # Previous metadata too old
            else:
                return  # No previous metadata available

        # Only update last metadata when we get fresh data from ZMQ (not fallback)
        # this prevents the last metadata from being constantly updated with itself
        # even if there is no object in the frame.
        if metadata_dict and not using_fallback:
            self.last_metadata = metadata_dict
            self.last_metadata_ts = sei_ts

        # 3. Draw the metadata
        sink_pad = overlay.get_static_pad("sink")
        caps = sink_pad.get_current_caps()
        actual_w, actual_h = 1920, 1080
        if caps:
            structure = caps.get_structure(0)
            structure.get_int("width")[1]
            actual_w = structure.get_int("width")[1]
            actual_h = structure.get_int("height")[1]

        try:
            metadata = FrameMetadata.model_validate(metadata_dict)            
            scale_x = actual_w / (metadata.frame_width or 1)
            scale_y = actual_h / (metadata.frame_height or 1)

            with DrawTimer("Frame Draw", self.enable_perf_timing):
                for detection in metadata.detections:
                    self.draw_detection(detection, context, scale_x, scale_y)
                
                if metadata.landmarks:
                    self.draw_landmarks_list(metadata.landmarks, context, scale_x, scale_y, bbox=None)
        except Exception as e:
            print(f"Draw Error: {e}")

    def draw_landmarks_list(self, landmarks_list, context, scale_x, scale_y, bbox=None):
        # Calculate adaptive point radius based on face size
        if bbox:
            face_width = (bbox.xmax - bbox.xmin) * scale_x
            point_radius = max(LANDMARK_POINT_MIN_RADIUS, min(LANDMARK_POINT_MAX_RADIUS, face_width * LANDMARK_POINT_SCALE_FACTOR))
            line_width = max(1, point_radius * 0.5)
        else:
            point_radius = LANDMARK_POINT_MAX_RADIUS
            line_width = point_radius * 0.5
        
        for landmark in landmarks_list:
            if not landmark.points: continue
            stride = landmark.points_stride
            pts = landmark.points
            
            # Determine which landmark set to use based on filter level
            if self.face_landmark_filter == 2:
                landmark_set = FACE_LANDMARK_MINIMUM
            elif self.face_landmark_filter == 1:
                landmark_set = FACE_LANDMARK_STANDARD
            else:  # 0 = maximum (draw all)
                landmark_set = None
            
            # Batch all points together, then fill once
            context.set_source_rgba(1, 0, 0, 0.9)
            for i in range(0, len(pts), stride):
                point_index = i // stride
                if (landmark_set is None or point_index in landmark_set) and i + 1 < len(pts):
                    context.arc(pts[i]*scale_x, pts[i+1]*scale_y, point_radius, 0, 2*3.14159)
                    context.new_sub_path()
            context.fill()
            
            # Batch all skeleton lines together, then stroke once
            if landmark.pairs:
                context.set_source_rgba(0, 1, 1, 0.7)
                context.set_line_width(line_width)
                for i in range(0, len(landmark.pairs) - 1, 2):
                    idx1 = landmark.pairs[i]
                    idx2 = landmark.pairs[i+1]
                    if landmark_set is None or (idx1 in landmark_set and idx2 in landmark_set):
                        p1, p2 = idx1*stride, idx2*stride
                        if p1+1 < len(pts) and p2+1 < len(pts):
                            context.move_to(pts[p1]*scale_x, pts[p1+1]*scale_y)
                            context.line_to(pts[p2]*scale_x, pts[p2+1]*scale_y)
                context.stroke()  # Single stroke for all lines

    def draw_detection(self, det, context, scale_x, scale_y):
        x1, y1 = det.bbox.xmin * scale_x, det.bbox.ymin * scale_y
        x2, y2 = det.bbox.xmax * scale_x, det.bbox.ymax * scale_y
        w, h = x2 - x1, y2 - y1
        
        # Box
        context.set_source_rgba(0, 1, 0, 0.8)
        context.set_line_width(4)
        context.rectangle(x1, y1, w, h)
        context.stroke()
        
        # Label background and text
        label = f"{det.label} {det.confidence:.2f}" if det.confidence else det.label
        context.set_font_size(26)
        ext = context.text_extents(label)
        
        # Draw label background
        context.set_source_rgba(0, 1, 0, 0.8)
        context.rectangle(x1, y1 - ext.height - 4, ext.width + 6, ext.height + 4)
        context.fill()
        
        # Draw label text
        context.set_source_rgba(0, 0, 0, 1)
        context.move_to(x1 + 3, y1 - 4)
        context.show_text(label)

        if det.landmarks: self.draw_landmarks_list(det.landmarks, context, scale_x, scale_y, bbox=det.bbox)
        if det.detections:
            for nested in det.detections: self.draw_detection(nested, context, scale_x, scale_y)

    def on_message(self, bus, message):
        t = message.type
        if t == Gst.MessageType.EOS: self.loop.quit()
        elif t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print(f"GStreamer Error: {err}")
            self.loop.quit()

    def run(self):
        self.pipeline.set_state(Gst.State.PLAYING)
        try:
            self.loop.run()
        except KeyboardInterrupt:
            pass
        self.pipeline.set_state(Gst.State.NULL)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Analytic viewer client for drawing AI metadata on video stream')
    parser.add_argument('-p', '--port', type=int, default=5000, help='UDP port to receive video stream (default: 5000)')
    parser.add_argument('--analytic-data-ip', type=str, default=DEFAULT_ANALYTIC_DATA_IP, 
                        help=f'IP address for ZMQ analytic data connection (default: {DEFAULT_ANALYTIC_DATA_IP})')
    parser.add_argument('--analytic-data-port', type=int, default=DEFAULT_ANALYTIC_DATA_PORT,
                        help=f'Port for ZMQ analytic data connection (default: {DEFAULT_ANALYTIC_DATA_PORT})')
    parser.add_argument('--face-landmark-filter', type=int, default=2, choices=[0, 1, 2],
                        help='Face landmark filter level: 0=maximum (all points), 1=standard, 2=minimum (default: 2)')
    parser.add_argument('--debug-perf', action='store_true', help='Enable performance timing for drawing operations')
    parser.add_argument('--perf-print-freq', type=int, default=30, metavar='N',
                        help='Print performance statistics every N frames (default: 30)')
    args = parser.parse_args()
    
    app = VideoPlayer(
        udp_port=args.port, 
        face_landmark_filter=args.face_landmark_filter, 
        enable_perf_timing=args.debug_perf,
        perf_print_frequency=args.perf_print_freq,
        analytic_data_ip=args.analytic_data_ip,
        analytic_data_port=args.analytic_data_port
    )
    app.run()
