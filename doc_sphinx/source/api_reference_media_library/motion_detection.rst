.. _motion-detection-label:

================
Motion Detection
================

Overview
--------

Motion Detection is a feature that detects motion in a video stream.
It is based on the difference between two consecutive frames.
The motion detection algorithm is based on the OpenCV library, executed on the CPU.

Configuration
-------------

Configuration is done by the motion detection configuration section in the frontend configuration file.
There are several parameters that can be configured:

#. **roi** (Region of Interest) - Defines the rectangular area in the frame where motion detection will be applied. Specified with x, y coordinates and width, height dimensions. This allows you to focus motion detection on specific areas (e.g., a doorway, window, or specific zone) rather than the entire frame.

#. **sensitivity_level** - Controls how much pixel brightness change is required between frames to register as motion. Available levels range from LOWEST (least sensitive, ignores minor changes) to HIGHEST (most sensitive, detects subtle changes). Start with MEDIUM and adjust based on your environment and motion characteristics.

#. **threshold** - A fractional value (0.0 to 1.0) representing the percentage of pixels in the ROI that must change to trigger a motion detection event. For example, 0.01 (1%) means motion is detected when 1% or more of the ROI pixels exceed the sensitivity threshold. Lower values make detection more sensitive to small movements.

#. **resolution** - The resolution of the internal processing frame used for motion detection. Lower resolutions (e.g., 320x240) process faster but may miss fine details, while higher resolutions provide more accuracy at the cost of CPU usage.

#. **buffer_pool_size** - The number of buffers allocated for motion detection bitmasks. Set to 0 to use the default pool size. Increase this if you experience buffer allocation failures during high-throughput operation.

How Does it Work?
-----------------

When motion detection is enabled, it adds an extra internal output to the video stream with the configured resolution. This video stream is for internal processing only and does not go out from the frontend outputs.

**Algorithm Overview:**

Each frame is processed by comparing it to the previous frame to identify pixel-level changes. The algorithm uses OpenCV operations running on the CPU to generate a motion bitmask. The processing flow is:

#. **Frame Conversion**: An NV12 frame at the configured resolution is converted to a GRAYSCALE (GRAY8) format. This single-channel representation simplifies motion analysis by focusing on brightness changes rather than color.

#. **Frame Differencing**: The absolute difference between the current frame and the previous frame is calculated using `cv::absdiff()`. This produces a difference image where higher pixel values indicate greater change.

#. **Noise Reduction**: The difference image undergoes morphological filtering using `cv::morphologyEx()` with a `MORPH_OPEN` operation and a 5x5 elliptical kernel. This removes small artifacts and random noise that could cause false motion detection.

#. **Thresholding**: The filtered difference image is converted to a binary bitmask using `cv::threshold()` with the configured `sensitivity_level`. Pixels with changes below the sensitivity threshold become 0 (no motion), while pixels exceeding it become 255 (motion detected).

#. **Motion Decision**: The algorithm counts the number of white pixels (value 255) within the configured ROI. If the count exceeds the threshold percentage of total ROI pixels, motion is considered detected for that frame.

**Motion Bitmask Structure:**

The motion bitmask is a binary image (GRAY8 format) with the same dimensions as the configured motion detection resolution:

- **Pixel Value 0 (Black)**: No motion detected at this location
- **Pixel Value 255 (White)**: Motion detected at this location
- The bitmask provides spatial information showing exactly where motion occurred in the frame

Usage
-----

Subscribe to `hailofrontend` callbacks: When a new buffer is available, the user receives metadata with the motion detection buffer.

.. code-block:: c++

  fe_callbacks[s.id] = [s, media_lib](HailoMediaLibraryBufferPtr buffer, size_t size) {
      if (buffer->motion_detection_buffer != nullptr)
      {
          // Check the boolean flag for quick motion detection status
          if (buffer->motion_detected)
          {
              std::cout << "Motion detected in frame" << std::endl;
          }
          
          // Access the motion detection bitmask for detailed analysis
          auto motion_buffer = buffer->motion_detection_buffer;
          cv::Mat bitmask(motion_buffer->buffer_data->height,
                          motion_buffer->buffer_data->width,
                          CV_8UC1,  // Single-channel 8-bit grayscale
                          motion_buffer->get_plane_ptr(0));
          
          // Example: Count motion pixels in the bitmask
          int motion_pixel_count = cv::countNonZero(bitmask);
          int total_pixels = bitmask.rows * bitmask.cols;
          float motion_percentage = (float)motion_pixel_count / total_pixels * 100.0f;
          std::cout << "Motion coverage: " << motion_percentage << "%" << std::endl;
          
          // Optional: Save the bitmask for visualization or debugging
          cv::imwrite("motion_bitmask.png", bitmask);
      }
      
      media_lib->add_buffer_to_encoder(s.id, buffer);
  };

**Understanding the Output:**

Each video buffer from the frontend includes two motion detection outputs:

#. **motion_detected** (boolean): A simple flag indicating whether motion was detected in the current frame based on your configured ROI and threshold settings. Use this for quick decision-making (e.g., triggering recording, sending alerts).

#. **motion_detection_buffer**: A pointer to the full motion bitmask buffer. This provides pixel-level detail about where motion occurred:
   
   - **Format**: GRAY8 (single-channel 8-bit grayscale)
   - **Dimensions**: Match the configured motion detection resolution
   - **Content**: Binary image where white pixels (255) indicate motion and black pixels (0) indicate no motion

**Performance Considerations:**

- Motion detection runs on the CPU using OpenCV operations
- Lower resolution settings reduce processing time but may miss fine details
- The algorithm maintains frame-to-frame state (previous frame buffer)
- Processing time is typically 10-30ms depending on resolution and hardware
- First frame after enabling motion detection has no previous frame for comparison, so `motion_detected` will be false

**Tuning Tips:**

- Start with sensitivity_level=MEDIUM and threshold=0.01 for general use cases
- Use LOWEST or LOW sensitivity levels to reduce false positives from lighting changes, shadows, or environmental noise
- Use HIGH or HIGHEST sensitivity levels to detect subtle movements or small objects
- Adjust threshold based on expected motion size: smaller values (0.005-0.01) for detecting small objects, larger values (0.02-0.05) for significant scene changes
- Combine LOW sensitivity with low threshold (0.005) to detect only significant movements across small areas
- Combine HIGH sensitivity with higher threshold (0.03-0.05) to detect subtle movements but only when they affect a larger portion of the ROI
- Use a smaller ROI to focus on critical areas and improve performance
- Visualize the motion bitmask output to understand what the algorithm is detecting and adjust parameters accordingly
