==================
Analytics Database
==================

Overview
--------

The Analytics Database is a centralized storage system for AI inference results within the Media Library. It provides a thread-safe, time-indexed archive, enabling other components to query and retrieve analytic results based on type and timestamps.

The Analytics Database supports the following types of analytics data:

- **Detection Analytics**: Stores object detection results with bounding boxes
- **Instance Segmentation Analytics**: Stores instance segmentation results with masks and detection data


.. note:: 
  For an example of the Analytics Database in use, refer to Dynamic Privacy Masks in :ref:`Privacy Mask <privacy-mask-label>`. and to the `dynamic_privacy_mask` reference application, which demonstrate how to integrate instance segmentation results for dynamic privacy masking.


Usage
-----

Configuration
~~~~~~~~~~~~~

The Analytics Database can be configured using the `application_analytics` section in the profile configuration file, and is used to initalize the Database during the Media Library initialization. The configuration supports the two types of analytics data: `instance_segmentation` and `detection`. Both are optional.

Note that the analytics data represents results derived from an original image that may have been resized and scaled before processing. Ensure that the configuration specifies the final dimensions and scaling details accurately.

The configuration must include the following parameters for each type:

- **analytics_data_id**: A unique identifier for the analytics data.
- **width** and **height**: The final dimensions of the resized image.
- **original_width_ratio** and **original_height_ratio**: The aspect ratio of the original image before resizing. These values help identify the actual image area versus any padding added during letterboxing.
- **scaling_mode**: Indicates how the original image was resized. Options include:
    * `STRETCH`: The image was stretched to fit the new dimensions.
    * `LETTERBOX_MIDDLE`: The image was framed with padding added evenly around it.
    * `LETTERBOX_UP_LEFT`: The image was framed with padding added to the bottom or to the right.
- **max_entries**: The maximum number of entries to store.
- **labels**: A list of labels associated with the analytics data, where each label is defined by:
    * **label**: The name of the label.
    * **id**: The ID of the label.

Example JSON Configuration:

.. code-block:: json

   {
       "application_analytics": {
           "instance_segmentation": [
               {
                   "analytics_data_id": "yolo_segmentation",
                   "scaling_mode": "STRETCH",
                   "width": 640,
                   "height": 480,
                   "original_width_ratio": 4,
                   "original_height_ratio": 3,
                   "max_entries": 10,
                   "labels": [{ "label": "person", "id": 0 }, { "label": "car", "id": 1 }]
               }
           ],
           "detection": [
               {
                   "analytics_data_id": "yolo_detection",
                   "scaling_mode": "LETTERBOX_MIDDLE",
                   "width": 640,
                   "height": 640,
                   "original_width_ratio": 3,
                   "original_height_ratio": 2,
                   "max_entries": 7,
                   "labels": [{ "label": "person", "id": 0 }]
               }
           ]
       }
   }

.. Important::
   **Dual Camera Sensor Configuration**: When using two input streams (dual sensor), ensure that each Media Library instance uses different ``analytics_data_id`` values. This prevents conflicts between analytics data from different sensors and ensures proper data isolation in the shared Analytics Database object.


The Analytics Database can also be initialized directly through the C++ API:

.. code-block:: cpp

    // Get the singleton instance
    auto &analytics_db = AnalyticsDB::instance();

    application_analytics_config_t config;

    detection_analytics_config_t detection_config;
    detection_config.analytics_data_id = "detection_results";
    detection_config.scaling_mode = ScalingMode::LETTERBOX_MIDDLE;
    detection_config.width = 640;
    detection_config.height = 640;
    detection_config.original_width_ratio = 4;
    detection_config.original_height_ratio = 3;
    detection_config.max_entries = 7;
    detection_config.labels = {{"person", 0}, {"car", 1}};

    config.detection_analytics_config["detection_results"] = detection_config;

    instance_segmentation_analytics_config_t seg_config;
    seg_config.analytics_data_id = "segmentation_results";
    seg_config.scaling_mode = ScalingMode::STRETCH;
    seg_config.width = 640;
    seg_config.height = 480;
    detection_config.original_width_ratio = 4;
    detection_config.original_height_ratio = 3;
    seg_config.max_entries = 5;
    seg_config.labels = {{"person", 0}};

    config.instance_segmentation_analytics_config["segmentation_results"] = seg_config;

    analytics_db.add_configuration(config);


Adding Analytics Data
~~~~~~~~~~~~~~~~~~~~~

**Detection:**

.. code-block:: cpp

    DetectionAnalyticsData detection_data;
    detection_data.ts = frame_media_lib_buffer.isp_timestamp_ns;
    detection_data.analytics_buffer = {/* detection results */};
    
    auto result = analytics_db.add_detection_entry("detection_results", detection_data);
    if (result != MEDIA_LIBRARY_SUCCESS) {
        // Handle error
    }

**Instance Segmentation:**

.. code-block:: cpp

    InstanceSegmentationAnalyticsData seg_data;
    seg_data.ts = frame_media_lib_buffer.isp_timestamp_ns;
    seg_data.analytics_buffer = {/* segmentation results with masks */};
    seg_data.medialib_buffer_ptr = mask_media_lib_buffer;
    
    auto result = analytics_db.add_instance_segmentation_entry("segmentation_results", seg_data);
    if (result != MEDIA_LIBRARY_SUCCESS) {
        // Handle error
    }

.. note:: 
   The `analytics_buffer` in both cases should contain the serialized data of the detection or segmentation results, which is a vector of hailort structs. For detection, those structs are of type ``hailo_detection_t`` and for segmentation, they are of type ``hailo_instance_segmentation_t``. The buffer used for the timestamp is the original buffer holding the frame, as opposed to `medialib_buffer_ptr` in the instance segmentation data, which is used to reference the Media Library buffer that contains the mask memory, allocated for the AI inference results.

Querying Analytics Data
~~~~~~~~~~~~~~~~~~~~~~~

The Analytics Database supports three types of queries for retrieving analytics data:

1. **Exact Timestamp Query**: 
    Retrieves analytics data with the exact specified timestamp.

   .. code-block:: cpp

      AnalyticsQueryOptions options{
          .m_type = AnalyticsQueryType::Exact,
          .m_ts = target_timestamp,
          .m_timeout = std::chrono::milliseconds(100)
      };
      
      auto result = analytics_db.query_detection_entry("detection_results", options);
      if (result.has_value()) {
          auto detection_data = result.value();
          // Process detection data
      }

2. **Closest Timestamp Query**:
    Finds the analytics data with the closest timestamp (equal or earlier).

   .. code-block:: cpp

      AnalyticsQueryOptions options{
          .m_type = AnalyticsQueryType::Closest,
          .m_ts = target_timestamp,
          .m_timeout = std::chrono::milliseconds(500)
      };
      
      auto result = analytics_db.query_instance_segmentation_entry("segmentation_results", options);

3. **Within Delta Query**:
    Searches for analytics data within a specified time frame around the target timestamp. This query also requires a delta range parameter.

   .. code-block:: cpp

      AnalyticsQueryOptions options{
          .m_type = AnalyticsQueryType::WithinDelta,
          .m_ts = target_timestamp,
          .m_delta = std::chrono::milliseconds(40),
          .m_timeout = std::chrono::milliseconds(1000)
      };
      
      auto result = analytics_db.query_detection_entry("detection_results", options);


Accessing from Media Library
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Analytics Database is easily accessible through the Media Library API:

.. code-block:: cpp

   auto &analytics_db = media_library->get_analytics_db();
   
   // Use normally
   auto config = analytics_db.get_application_analytics_config();


API Reference
-------------

.. doxygenfile:: analytics_db.hpp
   :project: media_library
