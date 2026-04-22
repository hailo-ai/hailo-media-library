.. _buffer-label:

======
Buffer
======

Overview
========

The ``Buffer`` class is a fundamental data structure in the Hailo AI Analytics pipeline, representing a single image frame along with its associated metadata and region of interest (ROI). Each Buffer encapsulates:

- **Raw Frame Data**: Wrapped in a ``HailoMediaLibraryBuffer`` object
- **Metadata**: Extensible metadata system for attaching additional information (tensors, batch info, size properties, etc.)
- **Region of Interest (ROI)**: Analytics results and bounding box information via ``HailoROI``

Buffers flow through various stages of the analytics pipeline, accumulating metadata and analytics results from different processing nodes such as inference engines, post-processing modules, and cropping components.

Key Concepts
------------

.. figure:: /_images_src/hailo_analytics/buffer.png
   :alt: Buffer Contents
   :align: center
   :width: 50%

Frame Representation
~~~~~~~~~~~~~~~~~~~~

A Buffer represents one complete image frame. The underlying ``HailoMediaLibraryBuffer`` contains the actual pixel data (NV12).

Metadata System
~~~~~~~~~~~~~~~

The Buffer class provides an extensible metadata system that allows different pipeline components to attach additional information to a frame without modifying the core Buffer structure. Several metadata types are available:

1. **TensorMetadata**: Associates neural network tensor outputs with the frame
2. **BatchMetadata**: Tracks a buffer's position within a batch (index and total batch size)
3. **SizeMetadata**: Stores size-related information with descriptive labels
4. **BufferMetadata**: Attaches references to other buffers, enabling relationships between frames

This design allows pipeline stages to add context-specific information that downstream components can query and utilize.

Region of Interest (ROI)
~~~~~~~~~~~~~~~~~~~~~~~~~

Each Buffer contains a ``HailoROI`` object that stores all analytics results for regions within the frame:

- **Bounding box information** (initially covering the full frame)
- **Detection objects** with bounding boxes, labels, and confidence scores
- **Classification results** for image or region classification
- **Landmarks** for pose estimation and facial feature detection
- **Tracker IDs** for object tracking across frames
- **Segmentation masks** for pixel-level classification
- **Hierarchical structure** for nested analytics results (e.g., detections within detections)

The ROI is the central container for all analytics outputs and is essential for object detection, classification, tracking, pose estimation, segmentation, and other analytics workflows where specific regions of the frame need to be analyzed and annotated.

Typical Workflow
----------------

1. **Acquisition**: Buffers are typically acquired from a buffer pool to ensure efficient memory management
2. **Frame Capture**: Raw frame data is captured from a camera or cropped from another image into the buffer
3. **Processing**: The buffer flows through pipeline stages (inference, post-processing, tracking, etc.)
4. **Metadata Accumulation**: Each stage may add metadata (analytics results, cropping roi, etc.)
5. **Analysis**: Analytics components read the buffer content, ROI, and metadata to perform their functions
6. **Export**: Vision frames are encoded for streaming, and analytics results are exported to client devices
7. **Release**: When processing is complete, the buffer is released back to the pool for reuse

Buffer API Guide
================

This section describes the main classes and methods for working with Buffers in the Hailo AI Analytics pipeline.

Buffer Class
------------

.. doxygenclass:: hailo_analytics::pipeline::Buffer
   :project: hailo_analytics
   :members:
   :undoc-members:


Metadata Classes
----------------

MetadataType Enum
~~~~~~~~~~~~~~~~~

.. doxygenenum:: hailo_analytics::pipeline::MetadataType
   :project: hailo_analytics


Base Metadata Class
~~~~~~~~~~~~~~~~~~~~

.. doxygenclass:: hailo_analytics::pipeline::Metadata
   :project: hailo_analytics
   :members:
   :undoc-members:


SizeMetadata Class
~~~~~~~~~~~~~~~~~~

.. doxygenclass:: hailo_analytics::pipeline::SizeMetadata
   :project: hailo_analytics
   :members:
   :undoc-members:


BufferMetadata Class
~~~~~~~~~~~~~~~~~~~~

.. doxygenclass:: hailo_analytics::pipeline::BufferMetadata
   :project: hailo_analytics
   :members:
   :undoc-members:


TensorMetadata Class
~~~~~~~~~~~~~~~~~~~~

.. doxygenclass:: hailo_analytics::pipeline::TensorMetadata
   :project: hailo_analytics
   :members:
   :undoc-members:


BatchMetadata Class
~~~~~~~~~~~~~~~~~~~

.. doxygenclass:: hailo_analytics::pipeline::BatchMetadata
   :project: hailo_analytics
   :members:
   :undoc-members:


Usage Examples
==============

Creating and Using a Buffer
----------------------------

.. code-block:: cpp

   // Acquire a buffer from a media library buffer pool
   // Given a MediaLibraryBufferPoolPtr named buffer_pool
   HailoMediaLibraryBufferPtr ml_buffer =  = std::make_shared<hailo_media_library_buffer>();
   auto acquire_status = buffer_pool->acquire_buffer(ml_buffer);

   // Create a Buffer object
   BufferPtr buffer = std::make_shared<Buffer>(ml_buffer);
   
   // Get the ROI for adding analytics results
   HailoROIPtr roi = buffer->get_roi();
   
   // Add analytics objects to the ROI
   // (After inference or other processing)

Adding Metadata
---------------

.. code-block:: cpp

   // Add batch metadata
   BatchMetadataPtr batch_metadata = std::make_shared<BatchMetadata>(batch_size, batch_index);
   buffer->add_metadata(batch_metadata);
   
   // Add tensor metadata
   BufferPtr tensor_buffer = std::make_shared<Buffer>(tensor_ml_buffer);
   TensorMetadataPtr tensor_metadata = std::make_shared<TensorMetadata>(tensor_buffer, "tensor_layer_name");
   buffer->add_metadata(tensor_metadata);
   
   // Add size metadata
   SizeMetadataPtr size_metadata = std::make_shared<SizeMetadata>("original_width", 1920);
   buffer->add_metadata(size_metadata);

Querying Metadata
-----------------

.. code-block:: cpp

   // Get all tensor metadata
   auto tensor_metadatas = buffer->get_metadata_of_type(MetadataType::TENSOR);
   
   for (const auto& metadata : tensor_metadatas) {
       auto tensor_meta = std::dynamic_pointer_cast<TensorMetadata>(metadata);
       std::string tensor_name = tensor_meta->get_tensor_name();
       BufferPtr tensor_buffer = tensor_meta->get_buffer();
       // Process tensor data...
   }
   
   // Get batch metadata
   auto batch_metadatas = buffer->get_metadata_of_type(MetadataType::BATCH);
   if (!batch_metadatas.empty()) {
       auto batch_meta = std::dynamic_pointer_cast<BatchMetadata>(batch_metadatas[0]);
       size_t index = batch_meta->get_index();
       size_t total = batch_meta->get_total_size();
   }

Accessing Buffer Content
-------------------------

.. code-block:: cpp

   // Get the underlying media library buffer
   HailoMediaLibraryBufferPtr ml_buffer = buffer->get_buffer();
   
   // Access frame properties from the media library buffer
   // (See MediaLibraryBuffer API documentation for details)
   
   // Get and work with the ROI
   HailoROIPtr roi = buffer->get_roi();
   
   // Get all detections from the ROI
   auto detections = roi->get_objects_typed(HAILO_DETECTION);
   
   for (auto& detection : detections) {
       HailoBBox bbox = detection->get_bbox();
       std::string label = detection->get_label();
       float confidence = detection->get_confidence();
       // Process detection...
   }
   
   // Get classifications
   auto classifications = roi->get_objects_typed(HAILO_CLASSIFICATION);
   
   // Get landmarks (e.g., pose estimation, facial features)
   auto landmarks = roi->get_objects_typed(HAILO_LANDMARKS);
   
   // Get tracker ID if tracking is enabled
   int tracker_id = roi->get_track_id();
   
   // Access other analytics results as needed


Best Practices
==============

Memory Management
-----------------

- Always acquire buffers from a buffer pool rather than allocating them directly
- Release buffers back to the pool as soon as processing is complete
- Avoid holding buffer references longer than necessary to prevent pool exhaustion

Metadata Usage
--------------

- Use specific metadata types (TensorMetadata, BatchMetadata, etc.) rather than the base Metadata class
- Query metadata by type using ``get_metadata_of_type()`` for efficient filtering
- Consider the lifecycle of metadata - it persists for the lifetime of the Buffer

Thread Safety
-------------

- If sharing buffers between threads, implement appropriate synchronization
- Consider using one buffer per thread or a thread-safe queue for passing buffers
- The ROI object is shared, so modifications affect all references to that Buffer
