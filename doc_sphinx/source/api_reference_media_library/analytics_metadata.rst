.. _analytics-metadata-label:

==================
Analytics Metadata
==================

Overview
========

``AnalyticsMetadata`` is a per-frame carrier of AI inference results that an analytics-pipeline producer (typically an analytics-layer encoder stage) can attach directly to a ``hailo_media_library_buffer`` via the ``m_analytics_metadata`` field. It is the buffer-attached alternative to the :ref:`analytics-database-label` flow, used by the dynamic :ref:`privacy-mask-label` to receive masks and detection bboxes without querying the DB singleton.

The carrier holds two optional vectors of wire-types:

- ``LabeledSemanticMask`` — a ``hailo_semantic_segmentation_mask_t`` plus an owned copy of the mask bytes (so the pointer outlives the original tensor buffer) plus a ``std::string label`` stamped by the producer.
- ``LabeledDetection`` — a ``hailo_detection_t`` plus a ``std::string label`` stamped by the producer.

Bbox coordinates inside the wire-types are already in the encoded frame's pixel space — the producer is expected to use the wrapped buffer's ``buffer_data->width / height`` when scaling normalized AI outputs, so consumers don't need a separate AI-frame dimensions side-channel. Consumers filter entries by the ``label`` field directly (string compare against the user-configured ``masked_labels`` list, no class_id-to-label resolution needed).

When ``m_analytics_metadata`` is null on a buffer, the consumer treats the frame as having no AI results (the dynamic privacy-mask blender, for example, falls back to the AnalyticsDB query path so existing pipelines that don't attach this field continue to work).

API
---

.. doxygenfile:: analytics_metadata.hpp
   :project: media_library
