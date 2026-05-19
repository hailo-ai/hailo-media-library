Snapshot Feature
=================

Overview
----------

The Snapshot feature in the Media Library provides a mechanism to capture the current state of media buffers at specific stages of the media processing pipeline. This feature is particularly useful for debugging, quality analysis, and development purposes.

Snapshots allow developers to:

- Visualize the intermediate processing results at different pipeline stages
- Debug visual artifacts or issues in media processing
- Validate algorithm behavior by examining input/output at specific points
- Capture reference frames for quality comparison and regression testing

How It Works
-------------

The Snapshot feature is implemented as a singleton manager that can:

1. Listen for snapshot requests through a named-pipe command interface
2. Capture frames in their native format (NV12, raw16, raw12, etc.) at designated points in the pipeline
3. Save the snapshot frames to a timestamped directory for later analysis

When a snapshot is requested, all registered stages in the pipeline will capture their next frame, allowing you to see a synchronized view of the media processing pipeline.

Pipeline Stages
~~~~~~~~~~~~~~~~

The following stages are available for snapshot capture (depending on pipeline configuration):

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Stage Name
     - Format
     - Description
   * - ``pre_isp_raw``
     - raw16
     - Raw Bayer frame from the sensor, before any processing
   * - ``pre_isp_denoised``
     - raw16
     - Pre-ISP denoised Bayer frame, fed into ISP input
   * - ``pre_isp_feedback_bayer``
     - raw16
     - Bayer feedback (loopback) channel for pre-ISP denoise (VD mode only)
   * - ``pre_isp_feedback_fusion``
     - raw16
     - Fusion feedback channel for pre-ISP denoise (HDM mode only)
   * - ``pre_isp_feedback_gamma``
     - raw16
     - Gamma feedback channel for pre-ISP denoise (HDM mode only)
   * - ``denoise``
     - NV12
     - Post-ISP denoised NV12 output
   * - ``post_isp``
     - NV12
     - ISP NV12 output, before post-ISP denoise (GStreamer path)
   * - ``dewarp``
     - NV12
     - Dewarped output frame
   * - ``multiresize_<stream_id>``
     - NV12
     - Per-stream multi-resize output (one per configured output stream)
   * - ``encoder_<width>x<height>``
     - NV12
     - Encoder input frame (one per encoder instance)

Enabling Snapshots
--------------------

Snapshots can be enabled in two ways:

1. **Environment Variable** (recommended for development):

   Set the environment variable ``MEDIALIB_SNAPSHOT_ENABLE`` before running your application:

   .. code-block:: bash

      export MEDIALIB_SNAPSHOT_ENABLE=1
      # Then run your application

2. **Programmatic API** (for runtime control):

   .. code-block:: cpp

      // Get the singleton instance
      SnapshotManager& snapshot_manager = SnapshotManager::get_instance();

      // Enable snapshots (true to enable, false to disable)
      snapshot_manager.enable_snapshot(true);

Using the Snapshot CLI Tool
----------------------------

The Media Library provides an interactive command-line tool called ``snapshot_cli`` for controlling the snapshot functionality. This is the recommended way to interact with the snapshot feature. The CLI features readline support with command history (arrow keys), tab completion for commands and stage names, and colored output.

Running the Tool
~~~~~~~~~~~~~~~~

Execute the ``snapshot_cli`` tool while your Media Library application is running:

.. code-block:: bash

   $ snapshot_cli
   ─── Hailo Snapshot Tool ───
   Type 'help' for commands, Tab to complete.
   snapshot>

The tool also supports non-interactive mode via piped input:

.. code-block:: bash

   echo "snapshot 5" | snapshot_cli

CLI Commands
~~~~~~~~~~~~

**snapshot** — Capture pipeline snapshots:

.. code-block:: bash

   snapshot                                  # 1 frame, all stages
   snapshot 5                                # 5 consecutive frames, all stages
   snapshot 3 post_isp,dewarp                # 3 frames, specific stages only
   snapshot 10 --interval 30                 # 10 snapshots, every 30th frame
   snapshot 5 --interval 15 post_isp         # 5 snapshots of post_isp, every 15th frame

The ``--interval`` flag controls frame skipping between captures: ``--interval 30`` captures one frame then skips 29 before capturing the next. This is useful for sampling over longer time periods without flooding disk.

Progress is reported during multi-frame captures:

.. code-block:: bash

   snapshot> snapshot 5
   Snapshot requested for 5 frames
   ▓▓▓░░░░░░░ 3/5

**list_stages** — Show available pipeline stages:

.. code-block:: bash

   snapshot> list_stages
   Available stages for snapshot:
   - pre_isp_raw
   - pre_isp_denoised
   - dewarp
   - multiresize_stream0
   - encoder_1920x1080
   - ...

**list_snapshots** — Browse saved snapshots on disk:

.. code-block:: bash

   snapshot> list_snapshots          # Show last 10 sessions (default)
   snapshot> list_snapshots 5        # Show last 5 sessions
   snapshot> list_snapshots --all    # Show all sessions

   Snapshots (/tmp/medialib_snapshots/)
   2026-03-17 14:30:22  (7 files, 89.2 MB)
     ├─ pre_isp_raw              3840x2160     RAW16   16.6 MB
     ├─ pre_isp_denoised         3840x2160     RAW16   16.6 MB
     ├─ dewarp                   3840x2160     NV12    12.4 MB
     └─ encoder_1920x1080        1920x1080     NV12     3.1 MB

**clear** — Delete all saved snapshot sessions:

.. code-block:: bash

   snapshot> clear
   Cleared /tmp/medialib_snapshots/

**help** — Show help with all commands and examples.

**exit** / **quit** — Exit the tool (also Ctrl+C or Ctrl+D).

Programmatic API
-----------------

While the CLI tool is recommended, you can also use the C++ API to control snapshots programmatically:

.. code-block:: cpp

   // Get the singleton instance
   SnapshotManager& snapshot_manager = SnapshotManager::get_instance();

   // Request a snapshot from all stages (1 frame)
   snapshot_manager.request_snapshot();

   // Request multiple frames (5 frames from all stages)
   snapshot_manager.request_snapshot(5);

   // Request frames from specific stages
   std::set<std::string> stages = {"post_isp", "dewarp"};
   snapshot_manager.request_snapshot(3, stages);

   // Request with frame interval (capture every 10th frame)
   snapshot_manager.request_snapshot(5, {}, 10);

   // Get list of available stages
   std::string stages_list = snapshot_manager.list_available_stages();

Integration in Your Pipeline
-----------------------------

To add snapshot capability to a custom stage in your media pipeline, call ``take_snapshot`` at the appropriate point in your processing:

.. code-block:: cpp

   #include "snapshot.hpp"

   void MyStage::process_frame(const HailoMediaLibraryBufferPtr& buffer)
   {
       // Normal frame processing...

       // Capture snapshot if requested (async by default)
       SnapshotManager::get_instance().take_snapshot("my_custom_stage", buffer);

       // For DMA-BUF buffers that are recycled immediately after this call
       // (e.g., V4L2 ISP buffers), use synchronous mode to ensure the data
       // is saved before the buffer is reused:
       SnapshotManager::get_instance().take_snapshot("my_stage", buffer, true);

       // Continue processing...
   }

The stage name is automatically registered on first call to ``take_snapshot``. The stage will appear in ``list_stages`` output after the pipeline has processed at least one frame through that code path.

Snapshot Storage and File Format
---------------------------------

Snapshots Location
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, snapshots are stored in directories under ``/tmp/medialib_snapshots/``.

The output directory can be overridden with the ``MEDIALIB_SNAPSHOT_PATH`` environment variable:

.. code-block:: bash

   export MEDIALIB_SNAPSHOT_PATH=/mnt/sdcard/snapshots/

**Directory structure:**

- Base directory: ``/tmp/medialib_snapshots/`` (or custom path)
- Each snapshot creates a timestamped folder: ``YYYY-MM-DD_HH-MM-SS_mmm/``
- Full path example: ``/tmp/medialib_snapshots/2026-03-17_14-30-22_456/``

**File naming:**

Each captured snapshot follows this naming pattern:

``<stage_name>_<width>x<height>.<format>``

For example:

- ``pre_isp_raw_3840x2160.raw16``
- ``dewarp_3840x2160.nv12``
- ``encoder_1920x1080_1920x1080.nv12``

Supported File Formats
~~~~~~~~~~~~~~~~~~~~~~~~~~

Snapshots are saved in the buffer's native format. The file extension indicates the pixel format:

.. list-table::
   :header-rows: 1
   :widths: 15 85

   * - Extension
     - Description
   * - ``.nv12``
     - YUV 4:2:0 semi-planar (Y plane + interleaved UV plane). Most pipeline stages.
   * - ``.raw16``
     - 16-bit raw Bayer data (used by pre-ISP denoise stages).
   * - ``.raw12``
     - 12-bit raw Bayer data.
   * - ``.gray8``
     - 8-bit grayscale.
   * - ``.rgb``
     - 24-bit RGB interleaved.
   * - ``.argb``
     - 32-bit ARGB interleaved.
   * - ``.a420``
     - YUV 4:2:0 planar with alpha plane.
   * - ``.raw``
     - Unknown/other format (raw bytes).

Viewing Snapshots
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**NV12 files** can be viewed with FFmpeg or GStreamer:

.. code-block:: bash

   # Convert to PNG with FFmpeg
   ffmpeg -f rawvideo -pix_fmt nv12 -s 3840x2160 -i dewarp_3840x2160.nv12 output.png

   # Display with GStreamer
   gst-launch-1.0 filesrc location=dewarp_3840x2160.nv12 ! \
     rawvideoparse format=nv12 width=3840 height=2160 ! \
     videoconvert ! autovideosink

**Raw 16-bit Bayer files** (.raw16) can be viewed with Python:

.. code-block:: python

   import numpy as np
   from PIL import Image

   width, height = 3840, 2160
   raw = np.fromfile("pre_isp_raw_3840x2160.raw16", dtype=np.uint16).reshape(height, width)

   # Normalize to 8-bit for visualization
   img = (raw >> 4).astype(np.uint8)  # Shift 12-bit data stored in 16-bit
   Image.fromarray(img).save("output.png")

Best Practices
---------------

- Enable snapshots only when needed as they may impact performance
- Use the ``list_stages`` command to identify available pipeline stages before capturing
- Use the ``--interval`` flag for long captures to avoid overwhelming disk I/O
- Target specific stages when debugging isolated issues rather than capturing all stages
- Use the ``clear`` command or set ``MEDIALIB_SNAPSHOT_PATH`` to an external storage when capturing many frames
- For production systems, keep snapshots disabled

Limitations
------------

- Snapshots are stored in the local filesystem and not automatically cleaned up
- Large or frequent snapshots may impact system performance and fill disk space
- The ``snapshot_cli`` tool requires the pipeline to be running with ``MEDIALIB_SNAPSHOT_ENABLE=1``
