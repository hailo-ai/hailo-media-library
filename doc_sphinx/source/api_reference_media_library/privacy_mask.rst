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


Privacy masks are configured through the profile system. Initial configuration is specified in the profile
JSON (see :ref:`Masking Configurations <masking-configurations-label>`); runtime changes are applied by
editing the profile's ``masking`` config on the relevant encoded output stream and calling
:code:`MediaLibrary::set_override_parameters()`. The underlying blender is internal — it reads the masking
config from the profile attached to each frame and applies the static and dynamic masks without direct
user interaction.

Runtime Masking Updates
-----------------------

All runtime masking changes are applied through the profile system. The pattern is the same regardless of
what is being changed (mask mode, static polygons, dynamic masking):

1. Fetch the current profile from :code:`MediaLibrary::get_current_profile()`.
2. Edit the desired stream's ``masking`` field in the returned :code:`config_profile_t`.
3. Apply the change with :code:`MediaLibrary::set_override_parameters()`. The internal blender picks
   up the new masking configuration on subsequent frames.

The examples on this page use the following helper, which applies an edit to every encoded output stream:

.. code-block:: c++

    static bool apply_masking_update(MediaLibraryPtr media_lib,
                                     const std::function<void(privacy_mask_config_t &)> &mutate)
    {
        auto profile_expected = media_lib->get_current_profile();
        if (!profile_expected.has_value())
            return false;

        config_profile_t profile = profile_expected.value();
        for (auto &[stream_id, stream_config] : profile.encoded_output_streams)
        {
            mutate(stream_config.masking);
        }
        return media_lib->set_override_parameters(profile) == MEDIA_LIBRARY_SUCCESS;
    }

The :code:`PrivacyMaskConfigurer` / :code:`PrivacyMask` classes that actually drive the DSP-side masking
are internal in this release; user code should not access them directly. Direct accessor methods that
existed in earlier versions (:code:`MediaLibraryEncoder::get_privacy_mask_blender()`, the GStreamer
``privacy-mask-blender`` element property) have been removed.

Mask Modes
----------
The resulting privacy mask, which combines the static and dynamic masks, supports two operational modes:

  1. **Color Mode**: The privacy mask is drawn with a specific color, configured via ``mask_type`` and ``color_value``.
  2. **Pixelization Mode**: The privacy mask is drawn with a pixelization effect, with intensity configured via ``pixelization_size``.

The mode can be changed both before and during pipeline execution. For example:

.. code-block:: c++

    // Set the mode to color and configure the color to black
    apply_masking_update(media_lib, [](privacy_mask_config_t &masking) {
        masking.mask_type = PrivacyMaskType::COLOR;
        masking.color_value = rgb_color_t{0, 0, 0};
    });

    // Set the mode to pixelization and configure the pixelization size to 40
    apply_masking_update(media_lib, [](privacy_mask_config_t &masking) {
        masking.mask_type = PrivacyMaskType::PIXELIZATION;
        masking.pixelization_size = 40;
    });

Static Privacy Mask
--------------------
Static privacy masks are held in ``masking.static_privacy_mask_config`` and can be added, removed, or
updated both before and during pipeline execution. For example:

.. code-block:: c++

    polygon example_polygon;
    example_polygon.id = "privacy_mask1";
    example_polygon.vertices = {vertex(125, 40), vertex(980, 40), vertex(980, 600), vertex(125, 600)};

    // Add a polygon
    apply_masking_update(media_lib, [&](privacy_mask_config_t &masking) {
        if (!masking.static_privacy_mask_config.has_value())
            masking.static_privacy_mask_config = static_privacy_mask_config_t{true, {}};
        masking.static_privacy_mask_config->masks.push_back(example_polygon);
    });

    // Move the first vertex of an existing polygon
    apply_masking_update(media_lib, [](privacy_mask_config_t &masking) {
        if (!masking.static_privacy_mask_config.has_value())
            return;
        for (auto &mask : masking.static_privacy_mask_config->masks)
        {
            if (mask.id == "privacy_mask1" && !mask.vertices.empty())
            {
                mask.vertices[0].x = 600;
                mask.vertices[0].y = 120;
            }
        }
    });

    // Remove a polygon
    apply_masking_update(media_lib, [](privacy_mask_config_t &masking) {
        if (!masking.static_privacy_mask_config.has_value())
            return;
        auto &masks = masking.static_privacy_mask_config->masks;
        masks.erase(std::remove_if(masks.begin(), masks.end(),
                                   [](const polygon &mask) { return mask.id == "privacy_mask1"; }),
                    masks.end());
    });

When the static privacy mask configuration changes, a quantized bitmask is generated to efficiently represent the polygons. This bitmask is applied to each incoming frame to render the polygons using the DSP.

Restrictions for static privacy masks:
  - A maximum of 8 static privacy masks can be configured per stream.
  - Each polygon can have a maximum of 8 vertices.

Dynamic Privacy Mask
--------------------
Dynamic privacy masks enable the masking of objects detected in the video stream based on segmentation or detection data.

The blender supports two modes for receiving per-frame AI results, automatically selected per-frame based on the input buffer:

1. **Buffer-attached metadata mode (recommended).**
   A producer attaches an ``AnalyticsMetadata`` carrier to ``hailo_media_library_buffer::m_analytics_metadata`` containing ``LabeledSemanticMask`` and/or ``LabeledDetection`` entries already in the encoded frame's pixel coordinate space. The blender consumes them directly without going through the Analytics Database singleton. ``masked_labels`` filters by the entry's ``label`` field (string compare).

2. **Analytics Database mode.**
   Activates as a fallback when the input buffer carries no ``m_analytics_metadata``. The blender queries the Analytics Database for the closest entry matching ``analytics_data_id`` (delta/timeout configurable) and resolves ``class_id`` to label via the per-id analytics-config ``labels`` list.

To enable and configure the dynamic privacy mask, follow these steps:

1. **Enable Dynamic Privacy Mask**:
   Toggle ``masking.dynamic_privacy_mask_config->enabled``.

   .. code-block:: c++

       apply_masking_update(media_lib, [](privacy_mask_config_t &masking) {
           if (masking.dynamic_privacy_mask_config.has_value())
               masking.dynamic_privacy_mask_config->enabled = true;
       });

2. **Configure analytics entries**:
   In the JSON configuration, list the analytics ids to consider and the ``masked_labels`` to apply per id. Both modes use this same configuration shape:

   - In **buffer-attached mode** the union of all ``masked_labels`` lists is used as the label filter; ``analytics_data_id`` is informational only (the wire-types carry their own labels).
   - In **Analytics Database mode** ``analytics_data_id`` selects the DB entry to query, and labels are resolved via the per-id analytics config.

3. **Masking behavior**:
   - The mask is dynamically applied based on detected objects in the video stream using the DSP.
   - The same color and pixelization settings as the static privacy mask are applied.
   - The ``dilation_size`` parameter can be configured to apply a dilation effect, improving mask coverage.

.. note::
   In buffer-attached mode the producer must scale bbox coordinates into the encoded frame's own pixel space before attaching the metadata; the blender uses the wrapped buffer's ``buffer_data->width / height`` as the network-frame dimensions. In Analytics Database mode the bbox coordinates are interpreted in the per-id ``width / height`` config space.

Restrictions:
  - In Analytics Database mode the ``analytics_data_id`` must match the ID of the analytics data in the Analytics Database, and ``masked_labels`` must correspond to valid labels in that analytics configuration.
  - In buffer-attached mode ``masked_labels`` must match the ``label`` strings the producer stamped on each ``LabeledSemanticMask`` / ``LabeledDetection`` entry.
  - The ``dilation_size`` must be within the range of ``0`` to ``15``.

Configuration
-------------
The privacy mask can be configured using the `masking` section of the profile configuration, per encoded output stream.

The configuration must include the following parameters:

  - **mask_type**: Specifies the mode of the privacy mask (`COLOR` or `PIXELIZATION`).
  - **color_value**: Defines the RGB color of the privacy mask.
  - **pixelization_size**: Specifies the pixelization effect size in pixels.

Additional parameters for static and dynamic privacy mask configurations:

  - **static_privacy_mask**:
      * **enabled**: Boolean indicating whether static privacy masks are enabled (default: `true`).
      * **masks**: A list of static privacy masks to apply to the stream. Each mask is defined as a polygon with:
          * **name**: The ID of the privacy mask.
          * **polygon_points**: A list of vertices defining the polygon, with each vertex specified by its x and y coordinates.

  - **dynamic_privacy_mask**:
      * **enabled**: Boolean indicating whether the dynamic privacy mask is enabled (default: `false`).
      * **dilation_size**: Specifies the dilation effect size in pixels (default: `0`, range: `0` to `15`).
      * Buffer-attached mode fields — **masked_labels** (labels to mask) and **label_to_class_id** (label to segmentor class index mapping). Both are required together.
      * Analytics Database mode fields — **analytics** (a list of entries, each with an **analytics_data_id** and its own **masked_labels**), **timeout_ms**, **delta_ms** and **query_type**. All four are required together.

.. note::
   The two dynamic modes are mutually exclusive in the configuration: a profile may carry the buffer-attached
   fields or the Analytics Database fields, but not both.

.. note::
   When static privacy masks are configured, any existing static privacy masks are removed and replaced with the new configuration.

API
---

The privacy mask configuration structures (:code:`privacy_mask_config_t`, :code:`static_privacy_mask_config_t`,
:code:`dynamic_privacy_mask_config_t`, :code:`polygon`) are documented in
:doc:`Media Library Types <media_library_types>`.

.. doxygenfile:: privacy_mask_types.hpp
   :project: media_library

