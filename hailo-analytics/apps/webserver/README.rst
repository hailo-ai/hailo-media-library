
Hailo Camera Viewer
===================

This guide assumes that the user has received the Medialib image for the H15 setup and has successfully flashed the device with the provided image.

Starting the Camera Viewer
------------------------------

1. Establish an SSH connection to the board as the root user::

    ssh root@10.0.0.1

2. Execute the following command::

    camera-viewer-server

.. note::
    The server must be started before accessing the application via a web browser.

Accessing the Application in a Web Browser
------------------------------------------

1. Open Google Chrome (this is currently the only browser that is fully compatible with the application)
2. On the host device, enter the board's IP address in the browser's address bar::

    http://10.0.0.1

Starting the Video Stream
-------------------------

Once the application is open, navigate to the video player section within the Vision Control interface.

Click the **Play** button to begin streaming video.

.. image:: ../../../../resources/camera_viewer_open.png
  :height: 500
  :width: 1000
  :align: center

Availability of EIS and DIS Buttons
-----------------------------------

The availability of the EIS (Electronic Image Stabilization) and DIS (Digital Image Stabilization) buttons depends on whether the board is equipped with a gyroscope:

- **EIS Button:** This button will be displayed if the board is equipped with a gyroscope, allowing the user to activate electronic image stabilization.

- **DIS Button:** If no gyroscope is present, only the DIS button (for digital image stabilization) will be available.

Achieving Optimal Image Quality
-------------------------------

To achieve the best possible image quality, it is recommended to:

- Match the camera resolution to the screen resolution to prevent scaling artifacts.
- Utilize full-screen mode, as displaying the camera feed in a smaller window may introduce aliasing.

.. note::
   Windows operating systems are generally more effective at minimizing aliasing compared to Ubuntu. However, aliasing may still be present regardless of the platform if the resolutions are not properly aligned.

Supported Sensor and Lens
-------------------------

For complete list of approved CMOS sensors for Hailo-15, refer to the Hailo-15 Approved Vendor List document

Supported Operating Systems
---------------------------

The webpage can be viewed on both Windows and Ubuntu operating systems. While the application is compatible with both platforms, it is important to note that Windows generally provides better aliasing minimization compared to Ubuntu when the screen resolution and streaming resolution do not match. When using Ubuntu, it's recommended to match streaming resolution to screen resolution.

Supported Browsers
------------------

At present, only Google Chrome is supported for accessing the Camera Viewer.

