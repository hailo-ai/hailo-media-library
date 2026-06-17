.. _profile-configurations-label:

======================
Profile Configurations
======================

Profile Structure and Differences
=================================

The profile contains all the configurations of an application including configurations previously present in the `frontend_config.json`, `encoder_config.json`, `3aconfig.json` and sensor entry.

This document describes the profile JSON structure used to configure the media library. The profile points to all the files that configure the system as a one stop shop for all the configurations of the system.

The JSON snippet below represents the webserver daylight profile configuration:

.. code-block:: json

    {
        "version": "1.0.0",
        "sensor_config": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/profiles/daylight/webserver_daylight/sensor_config.json",
        "application_settings": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/profiles/daylight/webserver_daylight/application_settings.json",
        "stabilizer_settings": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/profiles/daylight/webserver_daylight/stabilizer_settings.json",
        "iq_settings": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/profiles/daylight/webserver_daylight/iq_settings.json",
        "encoded_output_streams": [
            {
                "stream_id": "sink0",
                "encoding": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/profiles/daylight/webserver_daylight/encoder_sink0.json",
                "osd": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/profiles/daylight/webserver_daylight/osd_sink0.json",
                "masking": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/profiles/daylight/webserver_daylight/masking_sink0.json"
            }
        ]
    }


Here is a breakdown of the JSON structure:

.. note::

    Both absolute and relative paths are supported for the path-valued fields in the profile JSON.

Metadata
---------

The profile and all referenced configuration files include a top-level metadata field.
This field is generated automatically and validated by the Media Library during runtime.

The metadata provides:

* The target architecture (hailo15h or hailo15l)
* A content hash of the file
* A generation timestamp
* An optional description field

Runtime validation ensures that:

* A configuration file cannot be executed on the wrong architecture.
* A configuration file cannot be modified without re-hashing it.
* If validation fails, the Media Library prevents the pipeline from starting.

Example
~~~~~~~~

.. code-block:: json

    {
        "metadata": {
            "architecture": "hailo15h",
            "description": "",
            "content_hash": "8698f18a59334dafef6853657fdb0016569f57246c7d18af4557c83d0482abec",
            "generation_timestamp": "2025-11-16T12:18:55.676282+00:00"
        },
        "sensor_config": "...",
        "application_settings": "...",
        "stabilizer_settings": "...",
        "iq_settings": "...",
        "encoded_output_streams": [ "..." ]
    }

Allowed Architectures
~~~~~~~~~~~~~~~~~~~~~

Only the following architectures are valid:

* hailo15h
* hailo15l

Runtime Enforcement
~~~~~~~~~~~~~~~~~~~

The Media Library validates metadata during initialization:

* If the architecture does not match the running device, initialization fails.
* If a file's content_hash does not match its actual content, initialization fails.

This protects against accidental edits, stale files, and misconfigured systems.

Rehashing Configuration Files
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To make any modification permanent, files must be rehashed.
This is done using the Profile Manager tool.


Version
-------
The “version“ field indicates the version of the profile configuration structure, as well as the profiles and all their underlying components. the vertion can only be updated using the Profile Manager tool.


Sensor Configuration
---------------------

The ``sensor_config`` file is used by the ISP media server to configure and load the sensor correctly.

.. code-block:: json

   {
        "version": "1.0.0",
        "input_video": {
            "resolution": {
                "width": 3840,
                "height": 2160,
                "framerate": 30
            }
        },
        "sensor_configuration": {
            "name": "imx678",
            "drv": "HAILO_IMX678.drv",
            "mode": 0,
            "pixel_mode": 0,
            "sensor_only": 0,
            "af_i2c_bus": -1,
            "af_i2c_addr": "0x0"
        },
        "sensor_calibration_file": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/profiles/daylight/webserver_daylight/calib.json"
    }

**input_video**
~~~~~~~~~~~~~~~

This section describes the video received from the sensor and which will then be sent to the media library.

- ``source_type``: Type of the input video source.
  Possible values:
  
  - ``V4L2SRC`` – Linux camera/capture devices (default).
  - ``APPSRC`` – Raw-frame application-based sources.

- ``source``: The source file path of the input video when using ``APPSRC``.

- ``sensor_id``: The sensor id, which represents the sensor index, used to identify the sensor when using ``V4L2SRC``.  
  Possible values:
  
  - ``SENSOR_0`` (default)  
  - ``SENSOR_1`` (used for dual-sensor devices)

- ``resolution``: A resolution for the input video.

  - ``width``: The width of the video resolution.
  - ``height``: The height of the video resolution.
  - ``framerate``: The framerate of the video.

.. note::

   - This example assumes the input video comes from a V4L2 device.  
   - When using ``APPSRC``, a valid ``source`` file path must be provided

**sensor_configuration**
~~~~~~~~~~~~~~~~~~~~~~~~

This section describes the sensor configuration parameters previously included in the sensor0_entry.cfg file, which include the current sensor mode, driver and additional parameters needed in order to configure the sensor driver itself.

- ``name``: The name of the sensor (e.g., ``imx678``).
- ``drv``: The driver file for the sensor (e.g., ``HAILO_IMX678.drv``).
- ``mode``: Linked to a specific sensor calibration (Applies to all Hailo-15 flavours) and dewarp configuration (applies only to Hailo-15L). The sensor might support multiple modes for different scenarios.
- ``pixel_mode``: The sensor pixel mode, typically set to 0
- ``sensor_only``: disable all ISP modules and get an output that looks like the sensor's input
- ``af_i2c_bus``: for future use, not currently in used.
- ``af_i2c_addr``: for future use, not currently in used.
- ``sensor_calibration_file``: Initial image quality calibration parameters that are loaded during startup, for more information please refer to the tuning and calibration guide.

Application_settings
--------------------

The ``application_settings`` file contains settings related to the stream zoom, resolution, orientation and other advanced application capabilities the media library provides.

**Application Input Streams**
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The `application_input_streams` field contains settings related to the output video stream from media library.

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
    - **pool_max_buffers**: The number of buffers to allocate in the pool .
        - **scaling_mode**: Indicates how the image is to be resized. Options include:
        
          * `STRETCH`: Scale to fill the new dimensions, without maintaining aspect ratio (default).
          * `LETTERBOX_MIDDLE`: Scale to fit the new dimensions, framing with padding added evenly around it.
          * `LETTERBOX_UP_LEFT`: Scale to fit the new dimensions, framing with padding added to the bottom or to the right.
          * `SCALE_AND_CROP`: Scale to fill the new dimensions, maintaining aspect ratio and cropping the scaled image.

.. note:: Refer to the :ref:`Buffer pool <bufferpool-label>` for detailed explanation about buffer pools.

Optical Zoom
~~~~~~~~~~~~

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
~~~~~~~~~~~~

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

Motion Detection
~~~~~~~~~~~~~~~~

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

Rotation
~~~~~~~~

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
~~~~

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

HailoRT
~~~~~~~

The `hailort` field contains settings related to the Hailo Runtime.

.. code-block:: json

    {
        "hailort": {
            "device-id": "device0"
        }
    }

- **device-id**: ID of the Hailo device.

stabilizer_settings
-------------------
The ``stabilizer_settings`` file contains settings related to video stabilization, including DIS and EIS.

.. code-block:: json

    {
        "version": "1.0.0",
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
            "camera_fov_factor": 0.85,
            "angular_dis": {
                "enabled": false,
                "vsm": {
                    "hoffset": 1856,
                    "voffset": 1016,
                    "width": 1920,
                    "height": 1080,
                    "max_displacement": 64
                }
            },
            "debug": {
                "generate_resize_grid": false,
                "fix_stabilization": false,
                "fix_stabilization_longitude": 0.0,
                "fix_stabilization_latitude": 0.0
            }
        },
        "eis": {
            "enabled": false,
            "stabilize": true,
            "eis_config_path": "/home/root/apps/resources/final_calibration.json",
            "window_size": 10,
            "rotational_smoothing_coefficient": 0.0,
            "iir_hpf_coefficient": 0.997,
            "camera_fov_factor": 0.85,
            "line_readout_time": 7410,
            "hdr_exposure_ratio": 0.2,
            "min_angle_deg": 0.05,
            "force_clamp_correction_angles": false
        },
        "gyro": {
            "enabled": false,
            "sensor_name": "lsm6dsr_gyro",
            "sensor_frequency": "833.000000",
            "scale": 0.000152716
        }
    }

DIS
~~~

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
~~~

The `eis` field contains settings related to Electronic Image Stabilization (EIS).

.. code-block:: json

    {
        "eis": {
            "enabled": false,
            "eis_config_path": "/home/root/apps/resources/final_calibration.json",
            "window_size": 10,
            "rotational_smoothing_coefficient": 0.0,
            "iir_hpf_coefficient": 0.997,
            "camera_fov_factor": 0.85,
            "line_readout_time": 7410,
            "min_angle_deg": 0.005,
            "force_clamp_correction_angles": false
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
    - **scale**: Float indicating the scale of the gyroscope sensor. Used to convert the raw sensor data into the degrees per seconds unit. Can be obtained from the sensors datasheet "Angular rate sensitivity".

.. note:: EIS `enabled` set to `true` will not work without gyro `enabled` set to `true`.


iq_settings
-----------
The ``iq_settings`` file contains settings related to image quality enhancements, including HDR, denoise, dewarp and all automatic algorithms previously located in the 3aconfig file.

.. code-block:: json

    {
        "version": "1.0.0",
        "grayscale": false,
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
        },
        "hdr": {
            "enabled": false,
            "dol": 2
        },
        "dewarp": {
            "enabled": false,
            "color_interpolation": "INTERPOLATION_TYPE_BILINEAR",
            "sensor_calib_path": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/shared/calibration/cam_intrinsics.txt",
            "camera_type": "CAMERA_TYPE_PINHOLE"
        },
        "automatic_algorithms": {
        }
    }

Denoise
~~~~~~~

.. note::
    For an example of AI denoising, you can refer to the `denoise_analytics.sh <https://github.com/hailo-ai/tappas/tree/master/apps/h15/gstreamer/detection>`_ script in the TAPPAS repository. This script demonstrates how to use AI denoising in combination with AI analytics

The `denoise` field contains settings related to video denoising. There are three generations of AI-ISP denoise:

- **AI-ISP Gen1**: Post-ISP lowlight denoise. Processes YUV data after the ISP pipeline.
- **AI-ISP Gen2**: Pre-ISP lowlight denoise using VDM (Visidon Denoise Model). Processes raw Bayer data before the ISP pipeline.
- **AI-ISP Gen3**: Pre-ISP lowlight denoise using HDM (Hailo Denoise Model). Processes raw Bayer data with fusion and gamma feedback channels. Currently available on Hailo-15L with IMX678 sensor.

The denoise generation is determined automatically based on the profile configuration: ``bayer: false`` selects Gen1 (post-ISP), ``bayer: true`` selects Gen2 or Gen3 depending on the network channel configuration.

**Post-ISP denoise (AI-ISP Gen1):**

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
- **method**: Denoising method used. Possible values: ``HIGH_QUALITY``, ``BALANCED``, ``HIGH_PERFORMANCE``.
- **loopback-count**: Number of loopbacks for denoising.
- **network**: Contains settings for the denoising network:

    - **network_path**: Path to the network configuration file.
    - **y_channel**: Y-channel input layer for the network.
    - **uv_channel**: UV-channel input layer for the network.
    - **feedback_y_channel**: Feedback Y-channel input layer for the network.
    - **feedback_uv_channel**: Feedback UV-channel input layer for the network.
    - **output_y_channel**: Y-channel output layer for the network.
    - **output_uv_channel**: UV-channel output layer for the network.

**Pre-ISP denoise (AI-ISP Gen2 / Gen3):**

Pre-ISP denoise profiles use ``"bayer": true`` and configure the ``bayer_network`` field instead of ``network``. Gen3 (HDM) profiles additionally include fusion and gamma feedback channels for improved image quality.

HDR
~~~

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

Dewarp
~~~~~~

The `dewarp` field contains settings related to the dewarping of the video.

.. code-block:: json

    {
        "dewarp": {
            "enabled": true,
            "color_interpolation": "INTERPOLATION_TYPE_BILINEAR",
            "sensor_calib_path": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/shared/calibration/cam_intrinsics.txt",
            "camera_type": "CAMERA_TYPE_PINHOLE",
            "camera_fov": -1
        }
    }

- **enabled**: Boolean indicating whether dewarping is enabled. Possible values: ``true`` or ``false``.
- **color_interpolation**: Specifies the interpolation method used for color correction. Possible value: ``INTERPOLATION_TYPE_BILINEAR``, ``INTERPOLATION_TYPE_BICUBIC``.
- **sensor_calib_path**: Path to the sensor calibration file.
- **camera_type**: Specifies the type of camera. Possible value: ``CAMERA_TYPE_PINHOLE``, ``CAMERA_TYPE_FISHEYE``, ``CAMERA_TYPE_INPUT_DISTORTIONS``.
- **camera_fov**: Specifies the field of view of the camera. Value `-1` indicates auto-detection.

Automatic Algorithms
~~~~~~~~~~~~~~~~~~~~
The `automatic_algorithms` field contains settings related to automatic image quality algorithms running in the ISP.
for more details about each algorithm please refer to the isp documentation.


Encoded Output Streams
----------------------

The `encoded_output_streams` field contains settings related to the final encoding stage of the pipeline, which consists of masking, OSD and video/still encoding

.. code-block:: json

    {
        "encoded_output_streams": [
            {
                "stream_id": "sink0",
                "encoding": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/profiles/daylight/webserver_daylight/encoder_sink0.json",
                "osd": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/profiles/daylight/webserver_daylight/osd_sink0.json",
                "masking": "/etc/imaging/cfg/hailo15h/imx678/theia_sl410m/4k/profiles/daylight/webserver_daylight/masking_sink0.json"
            }
        ]
    }

- **encoded_output_streams**: A list of encoded output streams, each containing:
    - **stream_id**: The identifier for the output stream (e.g., `sink0`).
    - **config_path**: The path to the codec configuration file for the output stream.
    - **osd**: The path to the OSD configuration file for the output stream. for more details about the OSD configuration file please refer to the relevant secrtion
    - **masking**: The path to the masking configuration file for the output stream. for more details about the OSD configuration file please refer to the relevant secrtion

Restrictions
------------

* Input and output resolutions are constant, and cannot be changed during pipeline running
* Frame rates must not exceed the input rate, and must be of a divisible fraction (input rate: 30/1, output rate alternatives: 1,2,3,5,6,10,15)
* Once the pipeline has been initialized, the denoise network cannot be changed
