.. _frontend-configurations-label:

=======================
Frontend Configurations
=======================

Video Frontend
==============

The video frontend configuration is done in a single JSON structure.

The API should receive the JSON as argument, this means the JSON is not read from file by the media library code, the example and/or gst-element should read from file and execute the api, for a C++ example see the :ref:`Frontend C++ Configuration <frontend-label>` section.

The JSON snippet below represents the video front-end JSON that includes the following:

.. literalinclude:: ../../../hailo-media-library/media_library/examples/vision_config.json
   :language: json

Here is a breakdown of the JSON structure: 

Input Video
------------

The `input_video` field contains settings related to the input video stream for the frontend example.

.. code-block:: json

    {
        "input_video": {
            "resolution": {
                "width": 3840,
                "height": 2160,
                "framerate": 30
            },
            "sensor_id": "SENSOR_0"
        }
    }


- **source_type**: The type of the input video source. Possible value: ``V4L2SRC`` for Linux camera/capture devices, or ``APPSRC`` for raw-frame app-based sources (the default is ``V4L2SRC``).
- **source**: The source file path of the input video when using ``APPSRC`` as source type.
- **sensor_id**: The sensor id, which represents the sensor index, used to identify the sensor when using ``V4L2SRC`` as source type (the default is ``SENSOR_0``).
- **resolution**: A resolution for the input video, containing:

    - **width**: The width of the video resolution.
    - **height**: The height of the video resolution.
    - **framerate**: The framerate of the video.

.. Note:: This section assumes the input video is from a v4l2 device, however the input is dependent on the type of device used.
.. Note:: Sensor id is mainly relevant for dual sensor devices, where its value for the input video can be either ``SENSOR_0`` or ``SENSOR_1``.

Application Input Streams
-------------------------

The `application_input_streams` field contains settings related to the Application Input Streams stream.

.. code-block:: json

    {
        "application_input_streams": {
            "method": "INTERPOLATION_TYPE_BILINEAR",
            "format": "IMAGE_FORMAT_NV12",
            "grayscale": false,
            "resolutions": [
                {
                    "width": 3840,
                    "height": 2160,
                    "framerate": 30,
                    "pool_max_buffers": 20,
                    "scaling_mode": "STRETCH"
                },
                {
                    "width": 1280,
                    "height": 720,
                    "framerate": 30,
                    "pool_max_buffers": 20,
                    "scaling_mode": "STRETCH"
                }
            ]
        }
    }

- **method**: Specifies the interpolation method used for resizing the video. Possible value: ``INTERPOLATION_TYPE_BILINEAR``.
- **format**: Defines the format of the output video. Possible value: ``IMAGE_FORMAT_NV12``.
- **grayscale**: Boolean indicating whether the output video should be in grayscale. Possible values: ``true`` or ``false``.
- **resolutions**: A list of resolutions for the output video, each containing:

    - **stream_id**: (Optional) The given unique id that can be retrieved from frontend output for identification purposes, if this field is omitted then output stream will be named automatically as ``sink0``, ``sink1``, etc. 
    - **width**: The width of the video resolution.
    - **height**: The height of the video resolution.
    - **framerate**: The frame rate of the video.
    - **pool_max_buffers**: The number of buffers to allocate in the pool.
    - **scaling_mode**: Indicates how the image is to be resized. Options include:
        * `STRETCH`: Scale to fill the new dimensions, without maintaining aspect ratio (default).
        * `LETTERBOX_MIDDLE`: Scale to fit the new dimensions, framing with padding added evenly around it.
        * `LETTERBOX_UP_LEFT`: Scale to fit the new dimensions, framing with padding added to the bottom or to the right.
        * `SCALE_AND_CROP`: Scale to fill the new dimensions, maintaining aspect ratio and cropping the scaled image.

.. note:: Refer to the :ref:`Buffer pool <bufferpool-label>` for detailed explanation about buffer pools.

Dewarp
------

The `dewarp` field contains settings related to the dewarping of the video.

.. code-block:: json

    {
        "dewarp": {
            "enabled": true,
            "color_interpolation": "INTERPOLATION_TYPE_BILINEAR",
            "sensor_calib_path": "/etc/imaging/cfg/hailo15h/imx678/kit/4k/shared/calibration/cam_intrinsics.txt",
            "camera_type": "CAMERA_TYPE_PINHOLE",
            "camera_fov": -1
        }
    }

- **enabled**: Boolean indicating whether dewarping is enabled. Possible values: ``true`` or ``false``.
- **color_interpolation**: Specifies the interpolation method used for color correction. Possible value: ``INTERPOLATION_TYPE_BILINEAR``, ``INTERPOLATION_TYPE_BICUBIC``.
- **sensor_calib_path**: Path to the sensor calibration file.
- **camera_type**: Specifies the type of camera. Possible value: ``CAMERA_TYPE_PINHOLE``, ``CAMERA_TYPE_FISHEYE``, ``CAMERA_TYPE_INPUT_DISTORTIONS``.
- **camera_fov**: Specifies the field of view of the camera. Value `-1` indicates auto-detection.

DIS
---

The `dis` field contains settings related to Digital Image Stabilization (DIS).

.. code-block:: json

    {
        "dis": {
            "enabled": false,
            "minimun_coefficient_filter": 0.1,
            "decrement_coefficient_threshold": 0.001,
            "increment_coefficient_threshold": 0.01,
            "running_average_coefficient": 0.033,
            "std_multiplier": 3.0,
            "black_corners_correction_enabled": true,
            "black_corners_threshold": 0.5,
            "average_luminance_threshold": 0,
            "debug": {
                "generate_resize_grid": false,
                "fix_stabilization": false,
                "fix_stabilization_longitude": 0.0,
                "fix_stabilization_latitude": 0.0
            }
        }
    }

- **enabled**: Boolean indicating whether DIS is enabled. Possible values: `true` or `false`.
- **minimun_coefficient_filter**: Minimal value of the coefficient 'k' used to filter the motion vectors (MVs). This value is a float in the range [0, 1], determining how fast changes in output from a given MV are seen. Example: `k = 0` results in complete filtering and lack of consideration of the current MV, `k = 1` means immediate impact.
- **decrement_coefficient_threshold**: Value to decrement `k` whenever the difference of succeeding motion vectors is not too large. This value is a float in the range [0, 1], recommended values are between 1/100 and 1/10.
- **increment_coefficient_threshold**: Value to increment `k` when large motion occurs to prevent black corners. This value is a float in the range [0, 1], recommended values are between 1/100 and 1/10.
- **running_average_coefficient**: Coefficient used to calculate the runtime average of motion vectors (MV). A float value typically set to "1 / number-of-frames-to-average". Set to `1` to disable.
- **std_multiplier**: Acceptable deviation, usually set between `2.5` and `3.5`. Set to a very large value to disable.
- **black_corners_correction_enabled**: Boolean indicating whether black corners correction is enabled. `true` enables smooth stabilization with black corners, `false` avoids black corners but may violate stabilization.
- **black_corners_threshold**: Coefficient affecting filter strength when stabilizing rotation is greater than "BLKCRN_TO_K_THR * room-for-stabilization". A float value between `0` and `1`, recommended values are between `0.2` and `0.5`.
- **average_luminance_threshold**: Threshold for average luminance to enable or disable the stabilizer. A uint8 value in the range [0, 255]. If set to `0`, the stabilizer is always enabled; if set to `255`, the stabilizer is always disabled.
- **debug**: Contains debug settings:

    - **generate_resize_grid**: Boolean indicating whether to generate a grid that only resizes the input image into the output. Possible values: `true` or `false`.
    - **fix_stabilization**: Boolean indicating whether to fix the stabilized orientation to predefined values. `true` removes the impact of the stabilization filter and black-corners limitations.
    - **fix_stabilization_longitude**: Fixed stabilized longitude correlated to the first frame, in radians. A float value.
    - **fix_stabilization_latitude**: Fixed stabilized latitude correlated to the first frame, in radians. A float value.

EIS
---
The `eis` field contains settings related to Electronic Image Stabilization (EIS).

.. code-block:: json

    {
        "eis": {
            "enabled": false,
            "eis_config_path": "/home/root/apps/resources/eis_calibration.json",
            "window_size": 10,
            "rotational_smoothing_coefficient": 0.0,
            "iir_hpf_coefficient": 0.997,
            "camera_fov_factor": 0.85,
            "line_readout_time": 7410,
            "min_angle_deg": 0.005,
            "force_clamp_correction_angles": false,
        },
        "gyro": {
            "enabled": false,
            "sensor_name": "lsm6dsr_gyro",
            "sensor_frequency": "833.000000",
            "scale": 0.000152716
        }
    }

- **eis**: Contains settings related to the EIS:

    - **enabled**: Boolean indicating whether EIS is enabled. Possible values: `true` or `false`.
    - **eis_config_path**: String path to the EIS configuration file - a JSON file containing calibration data. The file is created using the calibration tool.
    - **window_size**: Integer indicating the number of frames orientations used for motion smoothing.
    - **rotational_smoothing_coefficient**: Float between 0 and 1 indicating the strength of the smoothing algorithm. 0 means correcting all motion (including intentional smooth motion and shake), and 1 means no correction at all.
    - **iir_hpf_coefficient**: Float between 0 and 1 indicating the strength of the IIR high-pass filter.
    - **camera_fov_factor**: Float indicating the field of view of the output frame. A larger FOV results in less cropping on the output frame but may cause black corners to appear.
    - **line_readout_time**: The time between two sequential lines in the sensor, expressed in nanoseconds. This integer value is used to adjust the stabilization for rolling shutter effects. The value should be obtained from the sensor datasheet.
    - **min_angle_deg**: Float indicating the minimum angle in degrees for EIS to be applied. This value is used to filter out small movements that do not require stabilization.
    - **force_clamp_correction_angles**: Boolean indicating whether to force clamping of correction angles. If set to `true`, the correction angles will be clamped to the maximum angles defined, even if the gyroscope data suggests otherwise. May cause degradation in stabilization performance due to limiting the correction angles.

- **gyro**: Contains settings related to the gyroscope used in EIS:

    - **enabled**: Boolean indicating whether the gyroscope is enabled. Possible values: `true` or `false`.
    - **sensor_name**: String indicating the name of the gyroscope sensor.
    - **sensor_frequency**: String indicating the frequency of the gyroscope sensor (*The value we recommend and mostly worked with here is "833.000000"*. Other possible values are: "208.000000", "416.000000").
    - **scale**: Float indicating the scale of the gyroscope sensor. Used to convert the raw sensor data into the degrees per seconds unit. Can be obtained from the sensors datasheet "Angular rate sesitivity".

.. note:: EIS `enabled` set to `true` will not work without gyro `enabled` set to `true`.

Optical Zoom
------------

The `optical_zoom` field is required for Hailo to adjust the dewarping according to the camera zoom.

.. code-block:: json

    {
        "optical_zoom": {
            "enabled": true,
            "magnification": 1.0,
            "max_dewarping_magnification": 100.0
        }
    }

- **enabled**: Boolean indicating whether optical zoom is enabled. Possible values: ``true`` or ``false``.
- **magnification**: Magnification factor for optical zoom.
- **max_dewarping_magnification**: Maximum dewarping magnification value. if dewarping is enabled, when the magnification factor exceeds this value, the dewarping is disabled automatically. this feature is implemented to avoid over correction with dewarping since high-magnification warping is negligible.

Digital zoom
------------

The `digital_zoom` field contains settings related to digital zoom. Digital zoom has two modes: ``DIGITAL_ZOOM_MODE_ROI`` and ``DIGITAL_ZOOM_MODE_MAGNIFICATION``, and the user can choose only one of them.

.. code-block:: json

    {
        "digital_zoom": {
            "enabled": false,
            "mode": "DIGITAL_ZOOM_MODE_ROI",
            "magnification": 1.0,
            "roi": {
                "x": 200,
                "y": 200,
                "width": 2800,
                "height": 1800
            }
        }
    }

- **enabled**: Boolean indicating whether digital zoom is enabled. Possible values: `true` or `false`.
- **mode**: Mode of digital zoom. Possible values: ``DIGITAL_ZOOM_MODE_ROI``, ``DIGITAL_ZOOM_MODE_MAGNIFICATION``.
- **magnification**: Magnification factor for digital zoom. Max digital zoom is 31× at any resolution. In rare non‑standard or telescopic setups, the zoom may cap marginally lower.
- **roi**: Region of interest for digital zoom, containing:

    - **x**: X-coordinate of the region.
    - **y**: Y-coordinate of the region.
    - **width**: Width of the region.
    - **height**: Height of the region.

.. note:: If ``DIGITAL_ZOOM_MODE_ROI`` mode is selected, magnification would be ignored, and if ``DIGITAL_ZOOM_MODE_MAGNIFICATION`` mode is selected, the ROI would be ignored.

Rotation
--------

The `rotation` field contains settings related to video rotation. The rotation will be applied to all the output resolutions.

.. code-block:: json

    {
        "rotation": {
            "enabled": false,
            "angle": "ROTATION_ANGLE_180"
        }
    }

- **enabled**: Boolean indicating whether rotation is enabled. Possible values: ``true`` or ``false``.
- **angle**: Angle of rotation. Possible value: ``ROTATION_ANGLE_0``, ``ROTATION_ANGLE_90``, ``ROTATION_ANGLE_180``, ``ROTATION_ANGLE_270``.

Flip
----

The `flip` field contains settings related to video flipping.

.. code-block:: json

    {
        "flip": {
            "enabled": false,
            "direction": "FLIP_DIRECTION_HORIZONTAL"
        }
    }

- **enabled**: Boolean indicating whether flipping is enabled. Possible values: ``true`` or ``false``.
- **direction**: Direction of the flip. Possible value: ``FLIP_DIRECTION_NONE``, ``FLIP_DIRECTION_HORIZONTAL``, ``FLIP_DIRECTION_VERTICAL``, ``FLIP_DIRECTION_BOTH``.

ISP
----

The `isp` field contains settings related to image signal processor.

.. code-block:: json

    {
        "isp": {
            "isp_config_files_path": "/usr/bin"
        },
    }

- **isp_config_files_path**: Path to the directory containing the ISP configuration files.

HailoRT
-------

The `hailort` field contains settings related to the Hailo Runtime.

.. code-block:: json

    {
        "hailort": {
            "device-id": "device0"
        }
    }

- **device-id**: ID of the Hailo device.

HDR
---

.. note::

   1. Only 2DOL is supported
   2. HDR and Denoise are mutually exclusive and cannot be enabled at the same time.
   3. HDR is supported only with input video of 4K or FHD.


The `hdr` field contains settings related to High Dynamic Range (HDR) video.

.. code-block:: json

    {
        "hdr": {
            "enabled": false,
            "dol": 2
        }
    }

- **enabled**: Boolean indicating whether HDR is enabled. Possible values: ``true`` or ``false``.
- **dol**: Digital overlap for HDR.

Denoise
-------

.. note:: 
    For an example of AI denoising, you can refer to the `denoise_analytics.sh <https://github.com/hailo-ai/tappas/tree/master/apps/h15/gstreamer/detection>`_ script in the TAPPAS repository. This script demonstrates how to use AI denoising in combination with AI analytics

The `denoise` field contains settings related to video denoising.

.. code-block:: json

    {
        "denoise": {
            "enabled": false,
            "sensor": "imx678",
            "method": "HIGH_QUALITY",
            "loopback-count": 1,
            "network": {
                "network_path": "/usr/lib/medialib/denoise_config/vd_m_imx678.hef",
                "y_channel": "model/input_layer1",
                "uv_channel": "model/input_layer4",
                "feedback_y_channel": "model/input_layer3",
                "feedback_uv_channel": "model/input_layer2",
                "output_y_channel": "model/conv17",
                "output_uv_channel": "model/conv14"
            }
        }
    }

- **enabled**: Boolean indicating whether denoising is enabled. Possible values: ``true`` or ``false``.
- **sensor**: Type of sensor used.
- **method**: Denoising method used. Possible value: ``HIGH_QUALITY``.
- **loopback-count**: Number of loopbacks for denoising.
- **network**: Contains settings for the denoising network:

    - **network_path**: Path to the network configuration file.
    - **y_channel**: Y-channel input layer for the network.
    - **uv_channel**: UV-channel input layer for the network.
    - **feedback_y_channel**: Feedback Y-channel input layer for the network.
    - **feedback_uv_channel**: Feedback UV-channel input layer for the network.
    - **output_y_channel**: Y-channel output layer for the network.
    - **output_uv_channel**: UV-channel output layer for the network.

Motion Detection
----------------

The `motion_detection` field contains settings related to motion detection feature.

.. code-block:: json

    {
        "motion_detection": {
            "enabled": true,
            "resolution": {
                "width": 640,
                "height": 480,
                "framerate": 30
            },
            "roi": {
                "x": 200,
                "y": 200,
                "width": 640,
                "height": 480
            },
            "sensitivity_level": "MEDIUM",
            "threshold": 0.5
        }
    }

- **enabled**: Boolean indicating whether motion_detection is enabled. Possible values: ``true`` or ``false``.
- **resolution**: A resolution for the motion detection stream (Includes bitmask size), containing:

    - **width**: The width of the video resolution.
    - **height**: The height of the video resolution.
    - **framerate**: The frame rate of the video.
    - **pool_max_buffers**: (Optional) The number of buffers to allocate in the pool . Default value is the maximum pool_max_buffers of output resolutions.
- **roi**: Region of interest for motion detection event to trigger, containing:

    - **x**: X-coordinate of the region.
    - **y**: Y-coordinate of the region.
    - **width**: Width of the region.
    - **height**: Height of the region.
- **sensitivity_level**: Sensitivity level of the motion detection. Possible values: ``LOWEST``, ``LOW`` ``MEDIUM``, ``HIGH``, ``HIGHEST``.
- **threshold**: Threshold for the motion detection, How much of the ROI should be changed in order to trigger motion detected. A float value between 0 and 1.


Restrictions
------------

* Input and output resolutions are constant, and cannot be changed during pipeline running
* Frame rates must not exceed the input rate, and must be of a divisible fraction (input rate: 30/1, output rate alternatives: 1,2,3,5,6,10,15)
* Once the pipeline has been initialized, the denoise network cannot be changed


API Reference
-------------

:ref:`frontend-label`.
