=========
Changelog
=========

.. container:: toggle

    .. container:: header, open_by_default

        .. rubric:: Media Library v1.11.0 (March 2026)
          :class: changelog

    **New Features and Enhancements:**

        * Added dedicated wrapper APIs for frontend and encoder operations
        * Added GStreamer elements for profile-based pipeline construction, including hailovision (frontend wrapper) and hailoencodebin (encoder wrapper), enabling seamless Media Library integration in GStreamer pipelines
        * Added support for the LAION-2B vision-language model, multi-class classification and search, and full-frame indexing for improved out-of-class embedding capabilities in the CLIP model
        * Added new analytics pipeline application for license plate detection and recognition
        * Added region of interest (ROI) dynamic encoding, enabling higher quality encoding for selected regions while reducing bandwidth for background areas
        * Hailo-15H only: Added user-space control and HDR mode support for Sony IMX662 Camera Sensor
        * Integrated Dynamic Privacy Masking (DPM) into the webserver application with a dedicated configuration endpoint. Supports all output resolutions with configurable IoU thresholds and dilation size
        * Added runtime control to enable or disable SDR AI-ISP without requiring a pipeline restart
        * Deprecated the AI example application

    **Bug Fixes:**
        * Fixed "Hailo-15H only: Electronic Image Stabilization (EIS) may produce visual artifacts along frame boundaries under high-amplitude vibrations"
        * Fixed "The CLIP model may occasionally fail if the source media was removed by the automatic cleanup mechanism"

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.10.1 (February 2026)
          :class: changelog

    **Bug Fixes:**

        * Fixed “Hailo-15L only: The CLIP model is not supported when using the Sony IMX678 camera sensor”
        * Fixed “Hailo-15H only: In Hailo AI Analytics applications, where multiple faces, appear bounding boxes and facial landmarks may flicker”

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.10.0 (January 2026)
          :class: changelog

    **New Features and Enhancements:**

        * AI-ISP Gen2: Added support for AI-ISP Gen2 with Denoise capabilities. This advanced AI-powered denoising enhance the image quality in low-light conditions while preserving fine details.
        * Persistent Configuration: Introduced persistent configuration support for saving and loading runtime configurations across system reboots and application restarts. This feature simplifies profile switching and ensures consistent behavior in production environments.
        * Hailo AI Analytics: Fully integrated Hailo AI Analytics into the Media Library, supporting AI-powered video analytics including object detection, tracking, and instance segmentation. Includes a new Analytics Viewer tool for real-time visualization of AI results on the host system.
        * Profile Configuration: Enhanced the profile system with comprehensive rule checking and validation mechanisms. Provides improved error detection, consistency checks, and detailed error messages to help catch misconfigurations early.
        * Perfetto Tracing Modes: Added two tracing modes—‘applications’ for high-level performance analysis and ‘applications_detailed’ for in-depth, granular timing information. Both modes are supported in the Media Library and Hailo AI Analytics.
        * HDR/SDR Switching: Significantly improved switching time between SDR and HDR modes, minimizing delay and reducing visual artifacts during mode transitions. Only Hailo-15H.
        * Profile Backup and Restore: Added profile backup and restore functionality for complete profile configurations, including sensor settings, encoding parameters, Image Quality (IQ) tuning, masking, and OSD settings.
        * Dynamic Privacy Mask: Improved Dynamic Privacy Mask performance with new neural networks offering higher accuracy and better performance, including improved handling of challenging scenarios such as partial occlusions and varying lighting conditions.
        * Fixed “system reboot issue presented as 'encoder error -11'”
        * Fixed “in some use cases CLIP application failed to open”
        * Fixed “memory leak issues in camera viewer”. This issue was revealed in long-run tests
        * Fixed “AI Example application stability issues”. Different errors occurred in long-run tests
        * Fixed “Motion detection ROI stability issues. When enabling the feature, occasionally error occurred”

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.9.1 (December 2025)
          :class: changelog

    **Bug Fixes:**

        * Fixed “error may occur when enabling motion detection mode with invalid x and y values in the JSON file” 
        * Fixed “VPS header was not generated when using the H265 encoder”

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.9.0 (October 2025)
          :class: changelog

    **New Features and Enhancements:**

        * Pre-ISP 12-bit Denoise: Introduced advanced pre-ISP 12-bit denoising capabilities for enhanced image quality processing before ISP operations, supporting higher bit depth processing for improved noise reduction.
        * Multi-Sensor Registry: Enhanced multi-sensor support with improved sensor registry system, providing better sensor detection, configuration management, and seamless switching between different sensor configurations.
        * Enhanced Perfetto Tracing: Expanded Perfetto tracing capabilities with new denoise-specific traces, enabling improved performance analysis and debugging of denoising operations throughout the media pipeline.
        * Profile Configuration Separation: Significant improvements to the profile-based configuration system including separation into multiple specialized configuration files for better modularity, maintainability, and customization of sensor, application, stabilizer, IQ, encoding, OSD, and masking settings.

    **Bug Fixes:**

        * APPSRC Support: Fixed compatibility issues with GStreamer APPSRC elements, improving raw-frame application-based source handling and pipeline stability. This feature is primarily useful for debugging purposes.
        * EIS Artifacts on Sharp Movement: Resolved Electronic Image Stabilization (EIS) artifacts that occurred during sharp camera movements, improving stabilization quality and reducing visual distortions.

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.8.1 (August 2025)
          :class: changelog

    **New Features and Enhancements:**

        * Added support for imx664 sensor.

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.8.0 (July 2025)
          :class: changelog

    **New Features and Enhancements:**

        * SoC Profiler: Integrated SoC profiler, performance tracing and profiling system for improved debugging and analysis of media library operations. 
        * Dynamic Privacy Mask: Enhanced privacy mask functionality with improved dynamic masking capabilities based on analytics database integration for real-time object detection and segmentation.
        * Analytics Database: Implemented comprehensive analytics database system for storing and querying detection and instance segmentation data with temporal management, supporting closest, exact, and within-delta query types.
        * Snapshot: Added advanced snapshot debugging feature with CLI tool support, enabling capture of pipeline states at multiple stages for debugging and quality analysis.

    **Bug Fixes:**
        * Fixed a couple of rare bugs in various locations

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.7.1 (May 2025)
          :class: changelog

    **Bug Fixes:**

        * Fixed a rare race condition in the denoise module.
        * Fixed aspect ratio bug.
        * Fixed rotation by 90 degrees bug.
        * Various profile configuration fixes.
        * Fixed stability bugs in the EIS gyro device module.


.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.7.0 (March 2025)
          :class: changelog

    **New Features and Enhancements:**

        * Introduced a new profile-based configuration system for the camera application.
        * Media library logger now supports controlling verbosity levels per module.
        * Media library now includes thermal monitoring support.
        * Denoise module now supports fast model switching at runtime.
        * Multi Resize now includes an option to keep the aspect ratio.
        * Removed throw statements to improve robustness.

    **Bug Fixes:**

        * Fixed various memory leaks, file descriptor leaks, and other bugs.
        * Fixed a rare race control conditions in the denoise module.

.. container:: toggle

    .. container:: header 

        .. rubric:: Media Library v1.6.1 (February 2025)
          :class: changelog

    **Bug Fixes:**

        * Fixed various memory leaks and bugs.

.. container:: toggle

    .. container:: header 

        .. rubric:: Media Library v1.6.0 (January 2025)
          :class: changelog

    **New Features and Enhancements:**

        * Improved Performance: Enhanced encoder performance under heavy CPU load.
        * Introduced Motion Detection in media library frontend

    **Bug Fixes:**

        * Resolved various issues to improve stability and reliability.

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.5.2 (December 2024)
          :class: changelog

    **New Features and Enhancements:**

        * Added post-denoise filters to enhance video quality.
        * Introduced a video freeze feature in the frontend.
        * Media library file descriptor duplication is now configurable through an environment variable, and is disabled by default.
        * Added a pause option to the EIS module, designed for improved usability with PTZ cameras.

    **Bug Fixes:**

        * Resolved an issue causing encoder buffer overflow.
        * Fixed ambiguous return values in the frontend's "Get Current FPS" function.
        * Addressed a bug in encoder SPS handling.
        * Various bug fixes in the EIS module.

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.5.1 (November 2024)
          :class: changelog

    **New Features and Enhancements:**

        * HDR 2DOL support in HML
        * Improved OSD Text quality

    **Bug Fixes:**

        * Fixed various memory leaks and bugs

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.5.0 (October 2024)
          :class: changelog

    **New Features and Enhancements:**

        * Media Library API Update - Refactored the Media Library API to utilize smart pointers, enhancing memory management and safety.
        * Switched Media library buffer management to be based only on DMA-Buf
        * EIS Support
        * HDR Configuration in Frontend - Introduced a new configuration option for HDR in the frontend module.
        * Enable MJPEG configuration during runtime 
        * General Improvement Encoder quality 

    **OSD features:**
        * Added support for alignment properties in overlays
        * Added font weight (Bold) support for Text and DateTime overlays
        * Added outline support for Text and DateTime overlays
        * Added methods to retrieve the displayed overlay dimensions in pixels for Text and DateTime overlays
        * Updated `single_stream_osd` example to include a variety of OSD options
        * Improved color quality and background alignment, particularly in low-resolution outputs

    **Bug Fixes:**

        * Memory Leaks fixes
        * Fix encoder gop size large than 1 and gop length larger than 30
        * Fix Pipeline when stopping and starting 
        * Fix AI denoising when stopping and starting 
        * Fix Dewarp + AI denoising toggle 
        * Bugfix: Fixed an issue where `background_color` was not set for `TextOverlay` when using the C++ API
        * Bugfix: Fixed an issue where shadow properties were not set for `DateTimeOverlay` when using the C++ API


.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.4.0 (July 2024)
          :class: changelog

    **New Features and Enhancements:**

        * Added configure C++ API to the ``hailofrontend`` module
        * The force keyframe API is now available through our hailoencoder.
        * OSD: Added shadow support for text and datetime overlays.
        * OSD: Added alpha (RGBA) support for foreground, background, and shadow colors in text/datetime overlays.

    **Bug Fixes:**
    
        * Fixed multiple memory leaks.
        * Fixed an issue with ARGB blending in our example.


.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.3.1 (May 2024)
          :class: changelog

    **New Features and Enhancements:**
    
        * JPEG-Encode C++ API: Introduced a new API for JPEG encoding through C++ API.
        * ARGB blending in Hailo-OSD Element: Added the ability to send ARGB buffer for blending through Hailo-OSD element.
        * Datetime format in Hailo-OSD: Added the ability to configure the datetime format in OSD.

    **Bug Fixes:**
    
        * Hailo AI-Denoise fixes: Resolved several issues in `hailofrontend`, related to stream AI-Denoise toggle.
        * Memory leaks Bug fixes: Fixed memory leaks in various elements including `hailofrontend` and `hailoencoder`.


.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.3.0 (April 2024)
          :class: changelog

    **New Features and Enhancements:**
        * DIS Performance Improvement: Added new heuristics to enhance DIS .performance across various scenarios, in particular focusing on angular and low-light.
        * Front-end C++ API Configuration: Introduced a new configuration option for the frontend C++ API.
        * DMA-Buf Support: Implemented DMA-Buf support across the media library.
        * Denoising Performance Enhancement: Improved denoising performance.
        * Reconfiguration Option for Hailo-OSD Element: Added the ability to reconfigure the Hailo-OSD element.
        * Front-end Multi-Resize Output Resolution: Removed the cap on output resolution for Front-end multi-resize, no longer restricted to divisors of 30.

    **Preview Features:**
        * Native AI Example: Included a preview of a native AI example.

    **Bug Fixes:**
        * Hailo encoder fixes: Resolved several issues in hailo encoder, related to stream restarts and closure
        * Privacy Mask Bug Fixes: Addressed various edge-case bugs related to privacy mask functionality.

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.2.2 (March 2024)
          :class: changelog

    * Updated webserver features and fixed some stabilization issue
    * Changed default parameters of encoder.
    * Aligned OSD api for text and datetime (now using the same font size scale).
    * New encoder dynamic update is now possible
    * Changing framerate explicitly is currently not working in new encoder, will be supported in next release.
    * Multi Resize supports more framerates (int 1-30)
    * Overall bug fixes

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.2.1 (February 2024)
          :class: changelog

    * Camera is automatically detected (imx334/imx678) for dewarp mesh 
    * New interface for privacy mask in frontend module
    * Added background color support in OSD
    * Fixed SPS/PPS calculations in encoder module 
    * Fixed Rotation/Flip 

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.2.0 (January 2024)
          :class: changelog

    * New front end element enabling the control of video input features (LDC/dewarp/multi resize) in a single element
    * Multi-thread JPEG SW encoding (allows more TP when encoding MJPEG by using several CPU cores)
    * Refined OSD rotation feature (1 degree granularity)
    * Updated hailo upload element to use GP-DMA
    * Optical zoom support


.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.1.2 (December 2023)
          :class: changelog

    * Detached OSD support
    * Dynamic fonts supported in OSD element
    * Greyscale support

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.1.1 (November 2023)
          :class: changelog

    * Performance improvements
    * Benchmark TAPPAS application ``Basic security camera`` runs at 30 fps
    * Dynamic overlay support - user application can draw/update its own overlays
    * Dynamic OSD - user can update the OSD elements (text, image, timestamp)
    * Dynamic digital zoom support (for center ROI) - user can change magnification
    * Pre-proc example includes the new features
    * Bugs fixes

.. container:: toggle

    .. container:: header

        .. rubric:: Media Library v1.1.0 (October 2023)
          :class: changelog

    Media Library C++ 
      * Lens distortion correction (4K)
      * Digital image stabilization (4K, based on VSM from ISP)
      * Digital zoom (bilinear, bicubic)
      * Multiple rescale kernel
      * Encoder C++ API
      * OSD C++ API
    Media Library GStreamer Plugin
      * Plugin created with elements to offer Media Library C++ features

