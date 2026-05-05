====================================
Electronic Image Stabilization (EIS)
====================================

------------
Introduction
------------

Electrical Image Stabilization (EIS) is a digital signal processing technique that compensates for unwanted camera motion to produce smoother, more stable video footage.  
Unlike optical image stabilization (OIS) which uses physical lens or sensor movement, EIS operates entirely in the digital domain by analyzing motion between consecutive frames and applying corrective transformations to counteract detected shake.  

Hailo’s EIS solution leverages gyroscope data to detect camera movement and applies rotational transformations to stabilize the output video.  
The system also includes rolling shutter compensation.  
When using EIS, the Field of View (FOV) is slightly reduced to allow margin for rotational correction. 

Currently, the only supported Gyroscope sensor is the `lsm6dsr <https://www.st.com/resource/en/datasheet/lsm6dsr.pdf>`_ by STMICROELECTRONICS®.

--------------
Key Principles
--------------

The gyroscope measures three-dimensional angular velocity, which is integrated over time to determine the camera’s rotational position.
To compensate for camera shake and stabilize frames, the camera's 3D orientation must be accurately calculated.
Since the gyroscope and camera may not be perfectly aligned, a calibrated rotation matrix is used to transform gyroscope measurements into the camera's coordinate frame.

This transformation is established using a dedicated calibration tool that defines the rotational relationship between the two sensors.
Frame rotation compensation relies on the camera's intrinsic parameters, including the camera matrix, focal length, and lens distortion coefficients.

These parameters define the internal geometry and optical properties required for accurate geometric transformations.

To address rolling shutter artifacts, each image row (or a group of consecutive rows) is rotated individually based on its capture timestamp. 
Since rolling shutter sensors expose rows sequentially rather than simultaneously, each row experiences slightly different motion. 
By applying row-specific rotation corrections that correspond to each row's exposure timing, geometric distortions typical of rolling shutter capture are effectively minimized.

---------------------
Gyroscope Calibration
---------------------

Before utilizing gyroscope measurements, it is essential to calibrate the following parameters using the provided calibration tool:

- **Gyroscope Bias**: Gyroscopes inherently produce biased readings, which must be corrected through calibration. This correction involves three float values, each corresponding to an axis.
- **Gyroscope Relative Orientation**: The axes of the sensor and gyroscope may not align perfectly. Calibration determines the static rotation matrix to align these coordinate systems, represented as three float values (using angle-axis representation).

The calibration process produces a static JSON configuration file, which is used for real-time stabilization with gyroscope measurements. The calibration process is long, it may take up to 30 minutes.

**Note**: These are the conditions that are recommended for creating the most accurate calibration:
    - *Lighting*: Ensure the recorded scene is well-lit; avoid dark or poorly illuminated scenes.
    - *Scene Content*: The scene should include distinct key points; avoid recording videos of featureless surfaces like a blank wall. For best results the use of a checkerboard pattern is highly recommended.
    - *Movement Control*: During calibration, ensure that no unintended or unrelated movements appear in the frames.

Installing the calibration tool:

  1. Download the whl file (hailo15_eis_calibration_tool-X.X.X-py3-none-any.whl) - The .whl is located at the Hailo-SW package under imaging_tools.
  2. Create a new virtualenv - python3 -m virtualenv VENV_NAME_HERE
  3. Enter the virtualenv ( source ./VENV_NAME_HERE/bin/activate )
  4. Install the whl  (pip install hailo15_eis_calibration_tool-X.X.X-py3-none-any.whl)
  5. Copy your cam intrinsics file (e.g.: /etc/imaging/cfg/<platform>/imxXXX/<lens>/4k/shared/calibration/cam_intrinsics.txt) into: ./VENV_NAME_HERE/lib/python3.10/site-packages/hailo15_eis_calibration_tool

The calibration tool usage is as follows (from inside the created virtualenv):

.. code-block:: sh

    hailo15_eis_calibration_tool [record / run / all] -e <path>/cam_intrinsics.txt

There are two steps in running the calibration tool:
    1. Record (`hailo15_eis_calibration_tool record`) - This step records the gyroscope data, frame timestamps, and records a video, typically three times by default (all three of them). The recording is interactive, guiding the user through camera movements at various angles.
    2. Run (`hailo15_eis_calibration_tool run`) - This non-interactive step processes the recorded data and generates the calibration file.

Flags for both `record` and `run`:

- `-e` : Sensor type for cam intrinsics file (the default is cam_intrinsics.txt, meaning that the script will look for that file).
- `-c` : Number of recording sessions (the default (and recommended) is 3).
- `-r` : Where to store (for `record`) or find (for `run`) the output calibration files (the default is /tmp/eis-calibration-records).

Flags for `record`:

- `-i` : IP address of the board (the default is 10.0.0.1).
- `-n` : Gyro device name (the default is `lsm6dsr_gyro`).
- `-f` : Frequency of the gyro (the default and recommended is 833).
- `-p` : Device type - hailo15h / hailo15l (the default is `hailo15h`).
- `-t` : Sensor type - default is `imx678`.
- `-l` : Print a list of the available IIO devices.
- `-v` : Print a verbose list of the available IIO devices.

Flags for `run`:

- `-s` : Gyroscope scale (the default is 0.000152716).
- `-o` : Number of optimization iterations (the default (and recommended) is 300) 

The `all` option runs both `record` and `run` sequentially.

For additional information, regarding the flags for each option, run:
    `python calibration_tool.py [record /run / all] -h`

After running the calibration tool, a JSON file will be created in ./output/final_calibration.json

Example output:

    {"rot_x": -1.3166675383447541, "rot_y": 1.4847855684859292, "rot_z": 1.0729443141500428,
     "gbias_x": 0.002363248456949236, "gbias_y": -0.009985423836812312, "gbias_z": -0.004928467067823413}

this file is the finalized gyroscope calibration file that should be used for EIS stabilization.
it is expected to be located on the device under the path specified in the stabilization configuration in the media library profile (default is /home/root/apps/resources/final_calibration.json).

--------------------
EIS Online Algorithm
--------------------

The EIS online algorithm's goal is to find the per-frame rotation matrix to stabilize the video. The algorithm is part of Hailo's media library package.
It processes gyroscope measurements along three axes, each with an associated timestamp to produce the stabilization matrix.
The general EIS flow is as follows:

1. **Frame Capture**: The camera captures a frame.
2. **Gyroscope Data Collection**: The gyroscope sensor collects rotation data related to the frame (the V-SYNC signal is used to synchronize between the gyro samples and the frame ISP timestamp).
3. **IIR filter**: The gyroscope data is filtered using a high pass filter to reduce bias.
4. **Integrate rotations**: The filtered gyroscope data is integrated to obtain the correction matrix.
5. **Mesh creation**: A dewarp mesh is created based on the correction matrix.
6. **Dewarp**: The frame is dewarped (in the DSP) using the created mesh.

---------------------
Notes and Limitations
---------------------
- *EIS and Lens Calibration*: 
    EIS performance is heavily dependent on accurate lens calibration. It is recommended to perform lens calibration for the specific camera module and sensor in use to achieve optimal stabilization results.
- *EIS and HDR Compatibility*: 
    Electronic Image Stabilization (EIS) is currently not supported when High Dynamic Range (HDR) is enabled. 
- *EIS State Consistency*: 
    Changing the EIS state (turning it on or off) on a per-frame basis can lead to system instability and may cause a crash.
- *EIS and 3DNR Performance*: 
    EIS relies on precise synchronization between the gyroscope and the camera, 
    assuming each frame represents a discrete point in time. 
    Temporal averaging violates this assumption by blending pixel data from multiple frames, creating a mismatch between:

    - Gyro data: Represents instantaneous motion at frame capture time
    - Averaged frames: Contain blended motion data from multiple time intervals
    
    EIS can operate with temporal averaging enabled, but stabilization performance may degrade.
    For applications prioritizing stabilization quality, it is recommended to disable or minimize temporal averaging strength.
- *Motion Blur Limitation*:
    EIS cannot correct motion blur, as blur occurs during exposure, before any digital stabilization can be applied.
    The relationship between shutter speed and blur is direct: longer exposure times produce more pronounced blur artifacts.
    This limitation becomes particularly significant under the following conditions:

    - High external vibration: Excessive camera shake increases pixel displacement
    - High zoom levels: Magnification amplifies the apparent movement, causing greater displacement
    
    For conditions involving significant vibration or high zoom, it is recommended to limit shutter speed to 15 milliseconds or less, to minimize motion blur effects that EIS cannot compensate for.
