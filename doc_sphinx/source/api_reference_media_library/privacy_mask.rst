.. _privacy-mask-label:

============
Privacy Mask
============

Overview
--------
The Privacy Mask feature enables the dynamic concealment of sensitive information in a video stream using the Hailo-15 DSP.

There are two types of privacy masks:

  - **Static Privacy Mask**: A set of polygons drawn over the video stream in real-time.
  - **Dynamic Privacy Mask**: A mask generated based on detected objects in the video stream, such as people.


.. note::
  For an example of Dynamic Privacy Masking, you can refer to the `dynamic_privacy_mask` reference application.


The `PrivacyMaskBlender` object manages the vector of static privacy masks and the settings for both static and dynamic privacy masks. This blender is controlled by the `HailoEncoder` and can be accessed by both the GStreamer and C++ APIs.

To access the blender in the C++ API:

.. note::

   The ``get_privacy_mask_blender()`` method is available on ``MediaLibraryEncoder`` instances.
   The unified ``MediaLibrary`` API does not yet expose a direct accessor for the privacy mask blender.
   Use the GStreamer API (shown below) for accessing the blender in pipeline-based applications.

Alternatively, access it from the GStreamer `hailoencodebin` element using:

.. code-block:: c++

  GstElementPtr encoder = glib_cpp::ptrs::get_bin_by_name(m_pipeline, "hailoencodebin");
  PrivacyMaskBlenderPtr privacy_mask_blender;
  // Retrieve the privacy mask blender from the frontend bin
  GValue val = G_VALUE_INIT;
  g_object_get_property(frontend.as_g_object(), "privacy-mask-blender", &val);
  void *value_ptr = g_value_get_pointer(&val);
  privacy_mask_blender = reinterpret_cast<PrivacyMaskBlender *>(value_ptr)->shared_from_this();
  g_value_unset(&val);

The resulting privacy mask, which combines the static and dynamic masks, supports two operational modes:

  1. **Color Mode**: The privacy mask is drawn with a specific color, configurable via the `set_color` API.
  2. **Pixelization Mode**: The privacy mask is drawn with a pixelization effect, with intensity adjustable via the `set_pixelization_size` API.

The mode can be changed both before and during pipeline execution. For example:

.. code-block:: c++

    // Set the mode to color and configure the color to black
    privacy_mask_blender->set_color({0, 0, 0});

    // Set the mode to pixelization and configure the pixelization size to 40
    privacy_mask_blender->set_pixelization_size(40);

Static Privacy Mask
--------------------
Static privacy masks can be added, removed, or updated both before and during pipeline execution. For example:

.. code-block:: c++

    polygon example_polygon;
    example_polygon.id = "privacy_mask1";
    example_polygon.vertices.push_back(vertex(125, 40));
    example_polygon.vertices.push_back(vertex(980, 40));
    example_polygon.vertices.push_back(vertex(980, 600));
    example_polygon.vertices.push_back(vertex(125, 600));
    privacy_mask_blender->add_static_privacy_mask(example_polygon);

    auto polygon_exp = privacy_mask_blender->get_static_privacy_mask("privacy_mask1");
    polygon polygon1 = polygon_exp.value();
    polygon1.vertices[0].x = 600;
    polygon1.vertices[0].y = 120;
    privacy_mask_blender->set_static_privacy_mask(polygon1);

    privacy_mask_blender->remove_static_privacy_mask("privacy_mask1");

When static privacy masks are manipulated via the API, a quantized bitmask is generated to efficiently represent the polygons. This bitmask is applied to each incoming frame to render the polygons using the DSP.

Restrictions for static privacy masks:
  - A maximum of 8 static privacy masks can be included in the blender.
  - Each polygon can have a maximum of 8 vertices.

Dynamic Privacy Mask
--------------------
Dynamic privacy masks enable the masking of objects detected in the video stream based on segmentation data. This feature requires the Analytics Database to be properly configured.

To enable and configure the dynamic privacy mask, follow these steps:

1. **Enable Dynamic Privacy Mask**:
   Use the `set_dynamic_mask_enabled` API to enable or disable the dynamic privacy mask.

   .. code-block:: c++

       privacy_mask_blender->set_dynamic_mask_enabled(true);

2. **Configure Analytics Data**:
   The dynamic privacy mask relies on segmentation data from the Analytics Database. Configure the `analytics_data_id` and specify the labels to be masked using the `masked_labels` parameter in the JSON configuration.

3. **Masking Behavior**:
   - The mask is dynamically applied based on detected objects in the video stream using the DSP.
   - The same color and pixelization settings as the static privacy mask are applied.
   - The `dilation_size` parameter can be configured to apply a dilation effect, improving mask coverage.

4. **Integration with the Analytics Database**:
   The dynamic mask queries the Analytics Database for the closest instance segmentation data and uses it to generate masks for the specified labels.

   .. note::
      Ensure the Analytics Database is properly initialized and configured with the required analytics data. For the dynamic privacy mask to function correctly, an additional element must process the video stream and add instance segmentation data to the Analytics Database. The frontend element must include an output stream dedicated to this purpose.

Restrictions:
  - The `analytics_data_id` must match the ID of the analytics data in the Analytics Database.
  - The `masked_labels` must correspond to valid labels in the analytics configuration.
  - The `dilation_size` must be within the range of `0` to `15`.

Configuration
-------------
The privacy mask can be configured using the `privacy_mask` section in the encoder configuration file.

The configuration must include the following parameters:

  - **mask_type**: Specifies the mode of the privacy mask (`COLOR` or `PIXELIZATION`).
  - **color_value**: Defines the RGB color of the privacy mask.
  - **pixelization_size**: Specifies the pixelization effect size in pixels.

Additional parameters for static and dynamic privacy mask configurations:

  - **static_privacy_mask**:
      * **enabled**: Boolean indicating whether static privacy masks are enabled (default: `true`).
      * **masks**: A list of static privacy masks to be added to the blender. Each mask is defined as a polygon with:
          * **name**: The ID of the privacy mask.
          * **polygon_points**: A list of vertices defining the polygon, with each vertex specified by its x and y coordinates.

  - **dynamic_privacy_mask**:
      * **enabled**: Boolean indicating whether the dynamic privacy mask is enabled (default: `false`).
      * **analytics_data_id**: The ID of the analytics data used in the Analytics Database.
      * **masked_labels**: A list of labels to be masked by the dynamic privacy mask.
      * **dilation_size**: Specifies the dilation effect size in pixels (default: `0`, range: `0` to `15`).

.. note::
   When static privacy masks are configured, any existing static privacy masks are removed and replaced with the new configuration.

API
---

.. doxygenfile:: privacy_mask.hpp
   :project: media_library


.. doxygenfile:: privacy_mask_types.hpp
   :project: media_library

