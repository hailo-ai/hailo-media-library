==========================
Analytic Draw Client
==========================

Overview
========

The ``app-analytic-draw-client.py`` is a Python-based video player that runs on the host machine and is designed to synchronize real-time video streams with external analytic metadata. It receives H.264 video over UDP, extracts SEI (Supplemental Enhancement Information) NAL units for frame-accurate timing, and synchronizes this with object detection data received via ZeroMQ (ZMQ) or WebSocket (WS).

The application uses **Cairo** to draw bounding boxes and labels directly onto the video frames. It also supports recording the video stream with overlays to an MKV file.

This tool is designed to work with backend applications running on the Hailo-15 platform, including:

* :doc:`detection` - Displays object detection results with bounding boxes and labels
* :doc:`face_landmarks` - Displays face detection boxes and facial landmark points

The analytic viewer receives video streams and metadata from these backend applications and provides real-time visualization on the host machine.

Features
========

* **UDP Video Receiver**: Listens for H15 H.264 RTP streams.
* **SEI Metadata Parsing**: Extracts ISP timestamps embedded in the video bitstream.
* **ZMQ / WebSocket Synchronization**: Matches H15 analytics metadata with specific video frames using ISP timestamps. Supports both ZMQ (default) and WebSocket transports.
* **Cairo Overlay**: Renders dynamic bounding boxes and labels.
* **MKV Recording**: Optionally records the video stream with drawn overlays to an MKV file using H.264 encoding with configurable bitrate.

Client Side Environment Setup
=============================

Describes the required setup for both Win10/11 and Ubuntu to run the client app.

Win 10/11
---------

1. Download and Install MSYS2.

2. Open MSYS2 UCRT64 Terminal.

3. ``pacman -Syu`` to update core system.

4. Install dependencies:

.. code-block:: bash

    pacman -S mingw-w64-ucrt-x86_64-python \
              mingw-w64-ucrt-x86_64-python-gobject \
              mingw-w64-ucrt-x86_64-python-cairo \
              mingw-w64-ucrt-x86_64-python-pyzmq \
              mingw-w64-ucrt-x86_64-gstreamer \
              mingw-w64-ucrt-x86_64-gst-python \
              mingw-w64-ucrt-x86_64-gst-plugins-base \
              mingw-w64-ucrt-x86_64-gst-plugins-good \
              mingw-w64-ucrt-x86_64-gst-plugins-bad \
              mingw-w64-ucrt-x86_64-gst-plugins-ugly \
              mingw-w64-ucrt-x86_64-gst-libav \
              mingw-w64-ucrt-x86_64-python-pydantic \
              mingw-w64-ucrt-x86_64-python-msgpack

   For MKV recording support, also install x264:

   .. code-block:: bash

       pacman -S mingw-w64-ucrt-x86_64-x264

   For WebSocket metadata transport, install via pip:

   .. code-block:: bash

       pip install websocket-client

5. Now ready to run the client app.

Ubuntu
------

Only 22.04 and 24.04 is supported.

1. Recommend to create python 3.1x environment.

2. ``sudo apt update``

3. Install system dependencies:

.. code-block:: bash

    sudo apt install \
        python3-dev \
        python3-pip \
        libcairo2-dev \
        libgirepository1.0-dev \
        pkg-config \
        gstreamer1.0-tools \
        gstreamer1.0-libav \
        gstreamer1.0-plugins-base \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-ugly \
        gir1.2-gstreamer-1.0 \
        gir1.2-gst-plugins-base-1.0 \
        gir1.2-gst-plugins-bad-1.0 \
        gir1.2-gtk-4.0

4. Create python virtual environment and activate it

.. code-block:: bash

    python3 -m venv .venv
    source .venv/bin/activate

5. While in python virtual environment install Python packages via pip:

.. code-block:: bash

    cd hailo-analytics/apps/analytic_viewer
    pip install -r requirements.txt

6. Now ready to run the client app.


Usage
=====

Basic Usage
-----------

To run the application with default settings (UDP port 5000, minimum landmarks):

.. code-block:: bash

    cd hailo-analytics/apps/analytic_viewer
    python app_analytic_draw_client.py

Command-Line Arguments
----------------------

The application supports the following command-line arguments:

* ``-u, --udp-port PORT``: UDP port to receive video stream (default: 5000)
* ``--analytic-data-ip IP``: IP address for ZMQ analytic data connection (default: 10.0.0.1)
* ``--analytic-data-port PORT``: Port for ZMQ analytic data connection (default: 7000)
* ``--face-landmark-filter LEVEL``: Face landmark filter level (default: 2)
  
  * ``0`` = maximum (all face landmark points)
  * ``1`` = standard (facial outline, eyes, eyebrows, nose, lips)
  * ``2`` = minimum (eyes, nose, lips only)

* ``--metadata-transport {zmq,ws}``: Metadata transport protocol (default: ``zmq``). Use ``ws`` for WebSocket.
* ``--save-mkv FILE``: Save the video stream with drawn overlays to an MKV file at the given path.
* ``--record-bitrate KBPS``: Recording bitrate in kbps when using ``--save-mkv`` (default: 8000).
* ``--debug-perf``: Enable performance timing for drawing operations
* ``--perf-print-freq N``: Print performance statistics every N frames (default: 30)

Examples
--------

To specify a custom UDP port:

.. code-block:: bash

    python app_analytic_draw_client.py --udp-port 5005

To specify custom ZMQ analytic data source:

.. code-block:: bash

    python app_analytic_draw_client.py --analytic-data-ip 10.0.0.3 --analytic-data-port 7000

To draw all face landmark points (maximum detail):

.. code-block:: bash

    python app_analytic_draw_client.py --face-landmark-filter 0

To enable performance debugging with statistics printed every 60 frames:

.. code-block:: bash

    python app_analytic_draw_client.py --debug-perf --perf-print-freq 60

To use WebSocket transport instead of ZMQ:

.. code-block:: bash

    python app_analytic_draw_client.py --metadata-transport ws

To record the video stream with overlays to an MKV file:

.. code-block:: bash

    python app_analytic_draw_client.py --save-mkv recording.mkv

To record with a custom bitrate (e.g., 4000 kbps):

.. code-block:: bash

    python app_analytic_draw_client.py --save-mkv recording.mkv --record-bitrate 4000

To combine multiple options:

.. code-block:: bash

    python app_analytic_draw_client.py --udp-port 5005 --analytic-data-ip 10.0.0.3 --face-landmark-filter 1 --debug-perf

Technical Logic
===============

1. **Metadata Listener**: Runs in a background thread, collecting metadata into a queue. Supports ZMQ (default) and WebSocket transports. The WebSocket listener auto-reconnects on connection loss.
2. **SEI Probe**: A GStreamer probe monitors the incoming H.264 stream for SEI units containing timestamps.
3. **Synchronization**: When a timestamp is found in the video, the script searches the metadata queue for a matching entry. If found, it is added to a draw registry with the frame PTS as key.
4. **Cairo Overlay**: The ``on_draw`` callback retrieves the metadata from the registry using the frame's presentation timestamp and renders the graphics.
5. **MKV Recording** (optional): When ``--save-mkv`` is specified, the pipeline branches via a ``tee`` element — one branch displays the video, the other encodes and writes to an MKV container. On exit (Ctrl+C), an EOS event is sent to finalize the file.

Sample Metadata
===============

.. code-block:: bash
    
    {
    "detections": [
        {
        "bbox": {
            "xmax": 1253.876220703125,
            "xmin": 1106.83251953125,
            "ymax": 867.47216796875,
            "ymin": 721.2953491210938
        },
        "confidence": 0.8466451168060303,
        "label": "person"
        },
        {
        "bbox": {
            "xmax": 1187.091064453125,
            "xmin": 1139.53955078125,
            "ymax": 800.7326049804688,
            "ymin": 736.2702026367188
        },
        "confidence": 0.9045873880386353,
        "label": "face",
        "landmarks": [
            {
            "pairs": [],
            "points": [
                1166.1790771484375,
                777.7117919921875,
                1.0,
                1164.6505126953125,
                771.7915649414062,
                1.0,
                1165.0872802734375,
                773.567626953125,
                1.0
            ],
            "points_format": "x,y,conf",
            "points_stride": 3
            }
        ]
        }
    ],
    "frame_height": 1080,
    "frame_width": 1920,
    "isp_timestamp_ns": 9022934317000
    }
