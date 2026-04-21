===============
Sensor Registry
===============

Overview
--------

The Sensor Registry is a centralized system for managing sensor types and their configurations within the Media Library. It provides a thread-safe singleton interface for:

- **Sensor Detection**: Automatically detect connected sensors via I2C and subdevice enumeration
- **Resolution Management**: Define and query supported resolutions for different sensors
- **Mode Mapping**: Configure sensor and CSI modes for SDR and HDR capture
- **Capabilities Query**: Retrieve sensor-specific information such as pixel formats, supported resolutions, and device paths

Usage
-----

Basic Usage
~~~~~~~~~~~

The Sensor Registry is accessible as a singleton throughout the Media Library:

.. code-block:: cpp

    // Get the singleton instance
    auto &registry = SensorRegistry::get_instance();
    
    // Detect connected sensor
    auto sensor_type = registry.detect_sensor_type();

    // Get sensor capabilities
    auto capabilities = registry.get_sensor_capabilities(sensor_type.value());
    if (capabilities.has_value()) {
        std::cout << "Sensor name: " << capabilities->sensor_name << std::endl;
        std::cout << "Pixel format: " << capabilities->pixel_format << std::endl;
    }

Querying Sensor Information
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The registry provides various methods to query sensor information:

.. code-block:: cpp

    auto &registry = SensorRegistry::get_instance();
    
    // Get I2C bus and address for a sensor
    auto i2c_info = registry.get_i2c_bus_and_address(0);
    if (i2c_info.has_value()) {
        int bus = i2c_info->first;
        std::string address = i2c_info->second;
    }
    
    // Get video device path
    auto video_path = registry.get_video_device_path(0);
    
    // Get pixel format for detected sensor
    auto pixel_format = registry.get_pixel_format();
    
    // Get sensor name
    auto sensor_name = registry.get_sensor_name(SensorType::IMX678);

Resolution and Mode Information
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Query resolution information and sensor modes:

.. code-block:: cpp

    auto &registry = SensorRegistry::get_instance();
    
    // Detect resolution from output configuration
    output_resolution_t output_res;
    output_res.dimensions.destination_width = 1920;
    output_res.dimensions.destination_height = 1080;
    
    auto resolution = registry.detect_resolution(output_res);
    if (!resolution.has_value() || resolution.value() != Resolution::FHD) {
        return media_library_return::MEDIA_LIBRARY_ERROR;
    }

Adding New Sensors
------------------

..Note:: Before adding a new sensor to the media library, it would be useful to refer to a process in the ISP guide which is similar to adding a new sensor, such as adding a driver, etc.

Follow these steps to add support for a new sensor to the registry:

Step 1: Update the SensorType Enum
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In ``sensor_types.hpp`` add the new sensor to the ``SensorType`` enum:

.. code-block:: cpp

    enum class SensorType
    {
        IMX334,
        ...
        IMX999  // Add the new sensor here
    };

Step 2: Create a Sensor Capabilities File
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Navigate to the ``media-library/hailo-media-library/media_library/src/isp/sensor_registry/`` directory and create a new header file for the sensor, e.g., ``imx999_capabilities.hpp``.

Define the sensor's capabilities by implementing the inline capabilities structure. Tip: use existing files (e.g., ``imx678_capabilities.hpp``) as a reference.

.. important::
   Check the sensor and CSI modes and the pixel format before finalizing the configuration.

Step 3: Register the Sensor
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Edit ``sensor_capabilities.hpp`` by doing the following:

1. Include the new sensor's header file:

.. code-block:: cpp

    #include "imx999_capabilities.hpp"

2. Add the sensor to the ``all_sensor_capabilities`` map:

.. code-block:: cpp

    inline const std::unordered_map<SensorType, SensorCapabilities> all_sensor_capabilities = {
        {SensorType::IMX334, sensor_config::imx334::capabilities},
        ...
        {SensorType::IMX999, sensor_config::imx999::capabilities},  // Add this line
    };

Adding New Resolutions
----------------------

Follow these steps to add support for a new resolution:

Step 1: Define the Resolution
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Open ``sensor_types.hpp`` and add the new resolution to the ``Resolution`` enum:

.. code-block:: cpp

    enum class Resolution
    {
        FHD,      // 1920x1080
        ...
        SIX_MP    // Add the new resolution here
    };

Step 2: Add Resolution Info
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Open ``sensor_capabilities.hpp`` and add the resolution to the ``all_resolution_info`` map:

.. code-block:: cpp

    inline const std::unordered_map<Resolution, ResolutionInfo> all_resolution_info = {
        {Resolution::FHD,
         {.width = 1920, .height = 1080, .name = "fhd", 
          .vsm_offsets = {.h_offset = 0, .v_offset = 0}}},
        ...
        {Resolution::SIX_MP,
         {.width = 3072, .height = 2048, .name = "6mp", 
          .vsm_offsets = {.h_offset = 576, .v_offset = 64}}},  // Add these lines
    };

Step 3: Update Sensor Capabilities
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Update the ``supported_resolutions`` and ``mode_mappings`` in the relevant sensor's capabilities file (e.g., ``imx678_capabilities.hpp``), by adding it to the ``.supported_resolutions`` and ``.mode_mappings`` maps.


API Reference
-------------

.. doxygenfile:: sensor_registry.hpp
   :project: media_library

.. doxygenfile:: sensor_types.hpp
   :project: media_library

