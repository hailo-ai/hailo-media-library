"""
General Analytic Viewer Client

Receives H.264 video via UDP RTP and analytics metadata (via ZMQ or WebSocket),
synchronizes them using SEI timestamps, and draws detection bounding boxes,
classification labels, and face landmarks on the video stream.

Usage:
    python3 app_analytic_draw_client.py --udp-port 5000
    python3 app_analytic_draw_client.py --metadata-transport ws --output-resolution 1080p
"""

import argparse

from viewer_common import BaseVideoPlayer, OUTPUT_SIZE_PRESETS

# ============================================================================
# Face Landmark Constants
# ============================================================================
FACIAL_OUTLINE = [
    10,  338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378,
    400, 377, 152, 148, 176, 149, 150, 136, 172, 58,  132, 93,  234, 127, 162, 21,
    54,  103, 67, 109
]
LIPS_OUTER = [61, 146, 91, 181, 84, 17, 314, 405, 321, 375, 291, 185, 40, 39, 37, 0, 267, 269, 270, 409]
LEFT_EYE = [33, 160, 158, 133, 153, 144]
RIGHT_EYE = [362, 385, 387, 263, 373, 380]
LEFT_EYEBROW = [70, 63, 105, 66, 107]
RIGHT_EYEBROW = [336, 296, 334, 293, 300]
NOSE_BRIDGE = [168, 6, 197, 195, 5]
NOSE_TIP = [1, 2, 98, 327]

FACE_LANDMARK_STANDARD = set(FACIAL_OUTLINE + LIPS_OUTER + LEFT_EYE + LEFT_EYEBROW + RIGHT_EYE + RIGHT_EYEBROW + NOSE_BRIDGE + NOSE_TIP)
FACE_LANDMARK_MINIMUM = set(LIPS_OUTER + LEFT_EYE + RIGHT_EYE + NOSE_BRIDGE + NOSE_TIP)

# Adaptive Landmark Drawing Size
LANDMARK_POINT_MIN_RADIUS = 2
LANDMARK_POINT_MAX_RADIUS = 8
LANDMARK_POINT_SCALE_FACTOR = 0.012


# ============================================================================
# Analytic Video Player
# ============================================================================
class AnalyticVideoPlayer(BaseVideoPlayer):
    """General-purpose analytic viewer with face landmark drawing and WebSocket support."""

    def __init__(self, face_landmark_filter=2, **kwargs):
        self.face_landmark_filter = face_landmark_filter  # 0=maximum (all), 1=standard, 2=minimum
        super().__init__(**kwargs)

    def draw_frame(self, metadata, context, scale_x, scale_y, draw_scale):
        """Draw detections with green boxes and face landmarks."""
        for detection in metadata.detections:
            self.draw_detection(detection, context, scale_x, scale_y, draw_scale)
        if metadata.landmarks:
            self._draw_landmarks_list(metadata.landmarks, context, scale_x, scale_y,
                                      draw_scale=draw_scale)

    def _post_draw_detection(self, det, context, scale_x, scale_y, draw_scale):
        """Draw face landmarks on detections that have them."""
        if det.landmarks:
            self._draw_landmarks_list(det.landmarks, context, scale_x, scale_y,
                                      bbox=det.bbox, draw_scale=draw_scale)

    def _draw_landmarks_list(self, landmarks_list, context, scale_x, scale_y,
                             bbox=None, draw_scale=1.0):
        """Draw face landmark points and skeleton lines."""
        max_r = LANDMARK_POINT_MAX_RADIUS * draw_scale
        min_r = max(0.5, LANDMARK_POINT_MIN_RADIUS * draw_scale)
        point_radius = max_r
        if bbox:
            face_width = (bbox.xmax - bbox.xmin) * scale_x
            point_radius = max(min_r, min(max_r, face_width * LANDMARK_POINT_SCALE_FACTOR))

        for landmark in landmarks_list:
            if not landmark.points:
                continue
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
                context.set_line_width(max(1, 2 * draw_scale))
                for i in range(0, len(landmark.pairs) - 1, 2):
                    idx1 = landmark.pairs[i]
                    idx2 = landmark.pairs[i+1]
                    if landmark_set is None or (idx1 in landmark_set and idx2 in landmark_set):
                        p1, p2 = idx1*stride, idx2*stride
                        if p1+1 < len(pts) and p2+1 < len(pts):
                            context.move_to(pts[p1]*scale_x, pts[p1+1]*scale_y)
                            context.line_to(pts[p2]*scale_x, pts[p2+1]*scale_y)
                context.stroke()


# ============================================================================
# Main
# ============================================================================
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='Analytic viewer client for drawing AI metadata on video stream')
    BaseVideoPlayer.add_common_argparse_args(parser)
    parser.add_argument('--face-landmark-filter', type=int, default=2, choices=[0, 1, 2],
                        help='Face landmark filter level: 0=maximum (all points), 1=standard, 2=minimum (default: 2)')
    args = parser.parse_args()

    output_size = OUTPUT_SIZE_PRESETS[args.output_resolution] if args.output_resolution else None
    app = AnalyticVideoPlayer(
        udp_port=args.udp_port,
        udp_ip=args.udp_ip,
        face_landmark_filter=args.face_landmark_filter,
        enable_perf_timing=args.debug_perf,
        perf_print_frequency=args.perf_print_freq,
        analytic_data_ip=args.analytic_data_ip,
        analytic_data_port=args.analytic_data_port,
        output_size=output_size,
        metadata_transport=args.metadata_transport,
        save_mkv=args.save_mkv,
        record_bitrate=args.record_bitrate,
    )
    app.run()
