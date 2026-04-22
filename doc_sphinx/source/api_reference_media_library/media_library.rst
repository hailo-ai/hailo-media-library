.. _media_library-label:

=============
MediaLibrary
=============

--------------------------------------------
Hailo Camera Configuration Management System
--------------------------------------------

Introduced in version 1.7.0, Hailo has implemented a new profile-based configuration system for its camera application. This system simplifies the management of camera settings by allowing users to define distinct operating profiles, each representing a specific working point of the system.

Each profile fully encapsulates the camera's functionalities including:

- **General attributes**: Resolution, color format, number of output streams from the image processing pipeline, and number of streams going into the encoder.
- **Sensor-specific settings**: Calibration and tuning parameters for the image processing pipeline.
- **Processing configurations**: Codec settings and On-Screen Display (OSD) definitions (if applicable).

The profile file replaces the legacy `frontend_config.json` and `encoder_config.json` files, while expanding their capabilities by adding additional references to ISP tuning files, and several other small differences, which replace the existing functionality of updating 3aconfig files in `/usr/bin`.

For more details, refer to :ref:`media-library-configurations-label`.

Reference Configuration Files
=============================

To facilitate integration, Hailo provides reference configuration files for the Sony IMX678, IMX675, IMX715, IMX662 sensors and Theia SL410M lens. For a complete list of all supported camera sensor modules, please refer to the Hailo-15 Approved Vendor List. These configurations align with common use cases in our reference application, including:

- **Daylight**: System tuned for a well-lit environment, with the sensor working in single exposure and ISP denoising enabled.
- **High Dynamic Range (HDR)**: System tuned for a high dynamic range environment, with the HDR 2-DOL feature enabled.
- **Low-Light**: System tuned for a low-light environment, with AI-based denoising enabled.
- **Infrared (IR)**: Similar to the daylight profile but outputs grayscale images from the processing pipeline.

Reference Profiles
==================

As part of the release, Hailo also provides a reference profile named `lowlight_r0225c5_profile`, located in the same folder as the `lowlight_r0225c8_profile`. This profile is similar to the lowlight profile but with a different noise/detail trade-off
For users wishing to switch to either of these networks instead of the default one, simply change the JSON path in the `medialib_config.json`.

Profile File Usage
==================

Profile files are designed to be treated as pre-configured points generated offline for the system, which set it in a designated state. On top of this, additional parameter changes can be applied via an API. As such, the profile needs to be treated as a **read-only file** by the runtime application.

Example: Initialize MediaLibrary and Using Profiles
===================================================

The following example demonstrates how to initialize the MediaLibrary, switch profiles, retrieve the current profile, and override parameters:

.. code-block:: cpp

    #include <iostream>
    #include <memory>
    #include <string>
    #include "media_library.h"

    MediaLibraryPtr m_media_lib;

    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        return 1;
    }
    m_media_lib = media_lib_expected.value();

    std::string medialib_config_path = 
        "/etc/imaging/apps/h15/native/configurations/cfg/medialib_configs/webserver_medialib_config.json";

    std::string medialib_config_string = read_string_from_file(medialib_config_path.c_str());

    // Initialize the media library with the sample configuration file and start with the default profile
    if (m_media_lib->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return 1;
    }

    // Switch to lowlight profile
    if (m_media_lib->set_profile("Lowlight") != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to set profile" << std::endl;
        return 1;
    }

    // Get the current active profile
    auto profile_config_expected = m_media_lib->get_current_profile();
    if (!profile_config_expected.has_value())
    {
        std::cout << "Failed to get current profile" << std::endl;
        return 1;
    }

    // Update a parameter inside the configuration and set it on top of the profile
    auto profile_config = profile_config_expected.value();
    profile_config.ldc_config.dewarp_config.enabled = true;

    if (m_media_lib->set_override_parameters(profile_config) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to set parameters" << std::endl;
        return 1;
    }


.. Note::
   Future releases will include the ability to move common settings between multiple profiles into shared files. Currently, even if information like lens distortion calibration is identical, it will exist explicitly in the profiles, although multiple profiles may point to the same calibration or tuning file.

Profile Backup and Restore
==========================

Media Library supports backing up and restoring profile states, allowing to preserve the current configuration state and restore it after system restarts.

**Backup Functionality:**

The ``backup_profiles()`` method creates a backup of the current profile state, including the active profile name and all profile configurations. The backup is stored as JSON files in a configured backup folder. The default backup folder path is extracted from the media library configuration file during initialization, using the ``backup_folder_path`` parameter. The default backup folder path can be overriden by setting it programmatically using ``set_default_backup_folder_path()``.

**Restore Functionality:**

During initialization, Media Library will automatically restore from a backup if the ``should_restore_backup`` parameter is set to ``true``. The library will look for the backup configuration files in the configured backup folder. If found and valid, it will initialize using the backup configuration instead of the provided configuration string. The active profile that was saved during backup is also restored, ensuring the system returns to the same profile state as when the backup was created. If the backup restoration fails, the library falls back to using the original configuration.
Note that if a custom backup folder path should be used instead of the default one, it must be set by using ``set_default_backup_folder_path`` before initialization.

**Usage Example:**

.. code-block:: cpp

    // Set the backup folder path before initialization (optional)
    // This will override any backup_folder_path in the config JSON
    m_media_lib->set_default_backup_folder_path("/custom/backup/path");
    
    // Initialize with backup restoration enabled
    std::string medialib_config_string = read_string_from_file(medialib_config_path.c_str());
    
    // Initialize with restore from backup if available
    if (m_media_lib->initialize(medialib_config_string, true) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        return 1;
    }
    
    // ... perform profile operations ...
    
    // Backup current profile state to the default backup folder
    if (m_media_lib->backup_profiles() != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to backup profiles" << std::endl;
    }


Profile Switching Logic
=======================

Profiles themselves do not contain switching logic. However, as they include the path to a `3aconfig` file, internal interpolation related to the 3A algorithms based on gains will continue to be handled internally within the profile.
As referenced in the `face_landmarks` app, it is expected that the user application switches between profiles based on runtime information (e.g., gain, dynamic range statistics) or via a UI decision.
**Important**: Profile switches may cause a reset to the running stream if there are significant differences between the active and new profile being set.

When switching profiles:
- The `isp_config_files` section fields (`3aconfig_path` and `sensor_entry`) are copied to the `/tmp` directory.
- Symlinks are created for these files in `/usr/bin/isp_3aconfig_0` and `/usr/bin/isp_sensor_entry_0`.
- The activated files are the symlinks themselves. If you need to edit the values, you can do so by modifying the symlinked files. The symlinks always point to the currently activated files.

Persistent Settings Override
-----------------------------

Media Library supports preserving certain user-configured settings across profile switches through the **override persistent settings** mechanism.

When `set_override_persistent_settings(true)` is called, profile switching works as follows:

- Before applying a new profile, the library will override all configuration structs marked as "persistent" in the new profile with the corresponding values from the currently active profile.
- This allows user-configured settings (such as zoom levels, rotation angles, stabilization parameters, etc.) to persist even when switching to a different profile.
- The default persistence state for each configuration struct can be changed at runtime by directly setting the static `is_persistent` member of the configuration struct.

.. Note::
    The `automatic_algorithms_config_t` struct has a special persistence mechanism:

    - **is_recursively_persistent**: When set to `true`, the entire `automatic_algorithms_config_t` is overridden as a single unit, regardless of individual struct persistence flags.
    - **Individual is_persistent flags**: Each of the structs within `automatic_algorithms_config_t` (e.g., `Aev1_config_t`, `Awbv2_config_t`, `AGamma64_config_t`) has its own `is_persistent` flag, so if `is_recursively_persistent` in `automatic_algorithms_config_t` is `false`, each struct is evaluated individually based on its own `is_persistent` flag.

**Version Compatibility:**

- **v1.10 (previous release)**: The `override_persistent_settings` flag defaulted to **false** for backwards compatibility.
- **v1.11 (current release)**: The default is now **true**, enabling persistent settings override by default. Applications that rely on persistent settings NOT being overridden will need to explicitly call `set_override_persistent_settings(false)`.

**Usage Example:**

.. code-block:: cpp

   // Enable persistent settings override, this is the default behavior
   media_library->set_override_persistent_settings(true);

   // Customize which settings persist across profile switches
   dewarp_config_t::is_persistent = false;
   config_application_input_streams_t::is_persistent = true;
   
   // Control automatic algorithms persistence at bulk level
   automatic_algorithms_config_t::is_recursively_persistent = true;  // Override all at once
   
   // OR control at individual struct level
   automatic_algorithms_config_t::is_recursively_persistent = false;
   Aev1_config_t::is_persistent = true;   // Only persist AE v1 settings
   Awbv2_config_t::is_persistent = false; // Don't persist AWB v2 settings
   
   // Switch profiles, all persistent settings will be preserved from previous profile
   media_library->set_profile("Lowlight");
   
   // Disable persistent settings override
   media_library->set_override_persistent_settings(false);
   
   // Switch profiles. Now all settings from the new profile will be loaded, without any persistent settings being overridden
   media_library->set_profile("High_Dynamic_Range"); 

Configuration Hierarchy
=======================

The supported profiles and their respective configurations files are defined in a media library configuration file.
This file contains a list of profiles, each pointing to its respective configuration files and calibration artifacts.

In each media library configuration file, all supported profiles should be defined, and each profile should point to its respective configuration file.
In each profile configuration file, all the required configuration files and calibration artifacts should be defined, including all encoders settings.

The recommended flow is to use the provided reference hierarchy structure located under `/etc/imaging/cfg/<Device Type>/<Sensor Type>/<Lens Type>`, which contains the configuration file, profiles, and all the tuning and calibration artifacts, as a starting point for generating the user environment.
The recommended flow for a system to support multiple sensors, lenses, and use cases is to create multiple media library configuration files, each containing a list of profiles relevant to the specific application, and have you application point to a unified location which can be modified using a soft-link.

For users using sensor or lens that differ from the reference ones:
- It is recommended to rename the folder hierarchy to avoid confusion or copy it into a new directory structure with the correct sensor and lens.

Example Folder Structure
-------------------------

Below is an example of the folder structure for the configuration hierarchy:

.. code-block:: text

    ├── <device_type>/
    │    ├── <sensor_name>/
    │    │   ├── <lens_name>/
    │    │   │   ├── <resolution>/
    │    │   │   │   ├── profiles/
    │    │   │   │   │   ├── <profile_name>/
    │    │   │   │   │   │   ├── <application>/
    │    │   │   │   │   │   │   ├── <application>_profile.json
    │    │   │   │   │   │   │   ├── application_settings.json
    │    │   │   │   │   │   │   ├── stabilizer_settings.json
    │    │   │   │   │   │   │   ├── calib.json
    │    │   │   │   │   │   │   ├── iq_settings.json
    │    │   │   │   │   │   │   └── sensor_config.json
    │    │   │   │   ├── shared/
    │    │   │   │   │   ├── codecs/
    │    │   │   │   │   │   ├── codec_config1.json
    │    │   │   │   │   │   ├── ...
    │    │   │   │   │   │   └── codec_configN.json
    │    │   │   │   │   ├── calibration/
    │    │   │   │   │   │   ├── cam_intrinsics.txt
    │    │   │   │   │   │   └── eis_calibration.xml

This structure organizes the configurations by sensor, lens, resolution, and profiles, making it easier to manage and customize for specific use cases.

It is recommended to stay in a similar hierarchy in the user environment, as the tuning tool assumes that this is the folder structure in use.

Configuration Generation Flow
-----------------------------

1. **Add or modify profiles**:
    - The baseline profiles are `daylight`, `lowlight`, `HDR`, and `IR`. If some are not relevant to the user application, or there is a need to add more profiles, the user is expected to:
        - Generate the corresponding folders in the same hierarchy.
        - Update the profile name and path in the `medialib_config.json`.

2. **For each of the profiles, update the following**:
    - **"input_video"**: The input video stream to the media library.
    - **"application_input_streams"**: The number of streams sent to the application.
    - **"encoded_output_streams"**: The number of streams sent to the encoder.
    - **additional parameters can be configured as needed**

3. **Run the calibration** for the profiles, overwriting the reference files:
    - `cam_intrinsics.json` - location can be found and updated in the `application_settings.json` file.
    - `eis_calibration.xml` - location can be found and updated in the `stabilizer_settings.json` file.
    - `sony_imx678.xml`

4. **Run the tuning tool** for the each of profiles

As mentioned earlier, since the profiles also contain application logic (e.g., resolutions and number of outputs), it is expected that in the case of different applications running on the same device, the user will create multiple `medialib_config.json` files.

Each of these `medialib_config.json` files would contain a list of different profiles generated to match the specific application. However, as applicable, these profiles would point to the same calibration and tuning files.

.. Note::
   - Updating the number of streams sent to the application is **not supported** as a run-time operation by the Hailo media library. If such functionality is required, the profiles should be originally created with the maximal required streams but with the FPS set to zero for the inactive ones. To enable them, use the `set_override_parameters` function to set the actual FPS.
   - In future releases, Hailo will provide a **UI-based tool** to dynamically generate configurations and profiles, as well as connect to the calibration and tuning tools.

Reference Application Usage
===========================

The following reference applications are going to switch to using the new profile configuration scheme:

- **Tuning tool**: Please see the Hailo Imaging Camera Calibration and Tuning Guide under the Hailo Developer Zone.
- **face_landmarks_app**: See example code in `hailo-camera-apps <https://github.com/hailo-ai/hailo-camera-apps/>`_.
- **frontend_example**: See example code in `hailo-media-library <https://github.com/hailo-ai/hailo-media-library/>`_.

These applications will move to use the new API and work based on the profile support.

All existing GStreamer-based reference applications are expected to continue working with the legacy flow.

-------------
API Reference
-------------

.. doxygenclass:: MediaLibrary
   :project: media_library
   :members:

.. doxygentypedef:: MediaLibraryPtr
   :project: media_library

