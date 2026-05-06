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

1. Listen for snapshot requests through a command interface
2. Capture NV12 format frames at designated points in the pipeline 
3. Save the snapshot frames to a timestamped directory for later analysis

When a snapshot is requested, all registered stages in the pipeline will capture their next frame, allowing you to see a synchronized view of the media processing pipeline.

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

The Media Library provides a dedicated command-line tool called ``snapshot_cli`` for controlling the snapshot functionality. This is the recommended way to interact with the snapshot feature.

Running the Tool
~~~~~~~~~~~~~~~~

Simply execute the ``snapshot_cli`` tool while your Media Library application is running:

.. code-block:: bash

   $ snapshot_cli

CLI Commands
~~~~~~~~~~~~

The CLI supports the following commands:

1. **Show help information**:

   .. code-block:: bash

      # help

2. **Capture a single snapshot from all stages**:

   .. code-block:: bash

      # snapshot
      Snapshot requested for 1 frame

3. **Capture multiple frames**:

   .. code-block:: bash

      # snapshot 5
      Snapshot requested for 5 frames

4. **Capture from specific stages only**:

   .. code-block:: bash

      # snapshot 3 post_isp,dewarp 
      Snapshot requested for 3 frames

5. **List available pipeline stages**:

   .. code-block:: bash

      # list_stages
      Available stages for snapshot:
      - post_isp
      - dewarp
      - ...

6. **Exit the tool**:

   .. code-block:: bash

      # exit
      Snapshot tool exiting.

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
   
   // Get list of available stages
   std::string stages_list = snapshot_manager.list_available_stages();

Integration in Your Pipeline
-----------------------------

To add snapshot capability to a custom stage in your media pipeline:

1. Get the singleton instance of the SnapshotManager
2. At appropriate points in your processing, check if a snapshot is requested
3. If requested, pass your buffer to the SnapshotManager with a unique stage name

Example:

.. code-block:: cpp

   void MyStage::process_frame(const HailoMediaLibraryBufferPtr& buffer)
   {
       // Normal frame processing...
       
       // Capture snapshot if requested
       SnapshotManager::get_instance().take_snapshot("my_custom_stage", buffer);
       
       // Continue processing...
   }

Snapshot Storage and File Format
---------------------------------

Snapshots Location
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By default, snapshots are stored in directories under ``/tmp/medialib_snapshots/``.

**Directory structure:**
  
- Base directory: ``/tmp/medialib_snapshots/``
- Each snapshot session creates a timestamped folder: ``YYYY-MM-DD_HH-MM-SS_mmm/``
- Full path example: ``/tmp/medialib_snapshots/2023-11-15_14-30-22_456/``

**File naming:**

Each captured snapshot follows this naming pattern:
  
``<stage_name>_<width>x<height>.nv12``

For example:
  
``dewarp_output_3840x2160.nv12``
``post_isp_1920x1080.nv12``

File Format
~~~~~~~~~~~~~~~~~~~~~~~~~~

Snapshots are saved in raw NV12 format, which is a YUV 4:2:0 planar format with 8 bits per sample. The Y plane comes first, followed by an interleaved U/V plane at half resolution both horizontally and vertically.

Viewing Snapshots
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To view NV12 files, you can use tools like FFmpeg or GStreamer:

With FFmpeg:

.. code-block:: bash

   ffmpeg -f rawvideo -pix_fmt nv12 -s 3840x2160 -i <stage_name>_3840x2160.nv12 output.png

With GStreamer:

.. code-block:: bash

    gst-launch-1.0 filesrc location=<stage_name>_3840x2160.nv12 ! \
      rawvideoparse format=nv12 width=3840 height=2160 ! \
      videoconvert ! autovideosink

Best Practices
---------------

- Enable snapshots only when needed as they may impact performance
- Use the ``list_stages`` command to identify available pipeline stages before capturing
- For multi-frame captures, use a reasonable frame count to avoid excessive disk usage
- Target specific stages when debugging isolated issues rather than capturing all stages
- For production systems, keep snapshots disabled
- Regularly clean up older snapshot directories to avoid filling up disk space

Limitations
------------

- Snapshots are currently limited to NV12 format buffers
- Snapshots are stored in the local filesystem and not automatically cleaned up
- Large or frequent snapshots may impact system performance
