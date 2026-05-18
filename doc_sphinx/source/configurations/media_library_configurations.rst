.. _media-library-configurations-label:

============================
Media Library Configurations
============================

The media library configuration is defined in a JSON structure. This configuration allows users to define profiles and their associated settings.

The JSON snippet below represents the media library configuration:

.. code-block:: json

    {
        "version": "2.0.0",
        "backup_folder_path": "/etc/imaging/cfg/backups/app_backup",
        "default_profile": "Daylight",
        "profiles": [
            {
                "name": "Daylight",
                "config_file": "/etc/imaging/cfg/hailo15h/imx678/theia_sl1410m/4k/profiles/daylight/daylight_profile.json"
            },
            {
                "name": "High_dynamic_range",
                "config_file": "/etc/imaging/cfg/hailo15h/imx678/theia_sl1410m/4k/profiles/HDR/HDR_profile.json"
            },
            {
                "name": "Lowlight",
                "config_file": "/etc/imaging/cfg/hailo15h/imx678/theia_sl1410m/4k/profiles/Lowlight/lowlight_r0225c8_profile.json"
            },
            {
                "name": "Infrared",
                "config_file": "/etc/imaging/cfg/hailo15h/imx678/theia_sl1410m/4k/profiles/IR/IR_profile.json"
            }
        ]
    }

Here is a breakdown of the JSON structure:

Version
-------

- **version**: Specifies the version of the configuration. Example: ``"2.0.0"``. The field is an internal field used by Hailo to track changes in the structure of the configuration file and ensure suitable backward compatibility. This field is not expected to be modified by the user.

Default Profile
---------------

- **default_profile**: The name of the default profile. Example: ``"Daylight"``. The field specifies the default profile to be used by the media library. The field denotes the profile used by the system during startup. It is recommended for the default profile to be one that doesn't use any AI feature in the system.

Profiles
--------

- **profiles**: A list of profiles, where each profile contains:
    - **name**: The name of the profile. Example: ``"Daylight"``.
    - **config_file**: The path to the configuration file for the profile. Example: ``"/etc/imaging/cfg/hailo15h/imx678/theia_sl1410m/4k/profiles/daylight/daylight_profile.json"``.
- The name provided in the **"default_profile"** field must be present as one of the profiles in the list.
- During application startup, all profiles in the list will be validated.

Backup Folder Path
------------------

- **backup_folder_path**: The directory path where the profiles backup is stored. Example: ``"/etc/imaging/cfg/backups/app_backup"``.

Restrictions
------------

* The `default_profile` must match one of the profile names in the `profiles` list.
* The `config_file` paths must be valid and accessible.

API Reference
-------------

:ref:`media-library-configurations-label`.
