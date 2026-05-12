#pragma once

// general includes
#include <cstddef>
#include <vector>

// medialibrary includes
#include "hailo/media_library/buffer_pool.hpp"

// postprocess tools includes
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"

namespace hailo_analytics::pipeline
{

class Buffer;
using BufferPtr = std::shared_ptr<Buffer>;

/**
 * @brief Enum class representing different types of metadata that can be attached to a Buffer.
 */
enum class MetadataType
{
    UNKNOWN,            ///< Unknown metadata type
    BUFFER,             ///< Buffer metadata type
    TENSOR,             ///< Tensor metadata type
    EXPECTED_CROPS,     ///< Expected crops metadata type
    SIZE,               ///< Size metadata type
    BATCH,              ///< Batch metadata type
    SKIPPED_DETECTIONS, ///< Skipped detections metadata type (quality gate)
};

/**
 * @brief Base class for metadata that can be attached to a Buffer.
 *
 * Metadata provides additional information about a Buffer beyond its raw data content.
 * Different metadata types can be attached to track various properties like size, tensors, batches, etc.
 */
class Metadata
{
  private:
    MetadataType m_type;

  public:
    /**
     * @brief Constructs a Metadata object with the specified type.
     * @param type The type of metadata (default: MetadataType::UNKNOWN)
     */
    Metadata(MetadataType type = MetadataType::UNKNOWN);
    virtual ~Metadata() = default;

    /**
     * @brief Gets the metadata type.
     * @return The MetadataType of this metadata object
     */
    MetadataType get_type() const;
};
using MetadataPtr = std::shared_ptr<Metadata>;

/**
 * @brief Metadata class for storing size information with a label.
 *
 * This metadata type is used to attach size-related information to a Buffer,
 * such as memory allocated or other size properties.
 */
class SizeMetadata : public Metadata
{
  private:
    std::string m_label;
    size_t m_size;

  public:
    /**
     * @brief Constructs a SizeMetadata object.
     * @param label A descriptive label for the size measurement
     * @param size The size value
     */
    SizeMetadata(std::string label, size_t size);

    /**
     * @brief Gets the label associated with this size metadata.
     * @return The label string
     */
    std::string get_label();

    /**
     * @brief Gets the size value.
     * @return The size value
     */
    size_t get_size();
};
using SizeMetadataPtr = std::shared_ptr<SizeMetadata>;

/**
 * @brief Metadata class that contains a reference to another Buffer.
 *
 * This metadata type allows attaching a Buffer reference to another Buffer,
 * enabling relationships between buffers.
 */
class BufferMetadata : public Metadata
{
  private:
    BufferPtr m_buffer;

  public:
    /**
     * @brief Constructs a BufferMetadata object.
     * @param buffer The Buffer to attach as metadata
     * @param type The metadata type (default: MetadataType::UNKNOWN)
     */
    BufferMetadata(BufferPtr buffer, MetadataType type = MetadataType::UNKNOWN);

    /**
     * @brief Gets the attached buffer.
     * @return Shared pointer to the attached Buffer
     */
    BufferPtr get_buffer();
};
using BufferMetadataPtr = std::shared_ptr<BufferMetadata>;

/**
 * @brief Metadata class for tensor information attached to a Buffer.
 *
 * This metadata type stores a tensor buffer along with its name, useful for
 * tracking neural network tensor data associated with a frame.
 */
class TensorMetadata : public BufferMetadata
{
  private:
    std::string m_tensor_name;

  public:
    /**
     * @brief Constructs a TensorMetadata object.
     * @param buffer The Buffer containing the tensor data
     * @param tensor_name The name of the tensor
     */
    TensorMetadata(BufferPtr buffer, std::string tensor_name);

    /**
     * @brief Gets the tensor name.
     * @return The name of the tensor
     */
    std::string get_tensor_name();
};
using TensorMetadataPtr = std::shared_ptr<TensorMetadata>;

/**
 * @brief Metadata class for batch processing information.
 *
 * This metadata type stores information about a buffer's position within a batch,
 * including the total batch size and the buffer's index within that batch.
 */
class BatchMetadata : public Metadata
{
  private:
    size_t m_total_size;
    size_t m_index;

  public:
    /**
     * @brief Constructs a BatchMetadata object.
     * @param total_size The total number of buffers in the batch
     * @param index The index of this buffer within the batch (0-based)
     */
    BatchMetadata(size_t total_size, size_t index);

    /**
     * @brief Gets the total batch size.
     * @return The total number of buffers in the batch
     */
    size_t get_total_size();

    /**
     * @brief Gets the buffer's index within the batch.
     * @return The 0-based index of this buffer in the batch
     */
    size_t get_index();
};
using BatchMetadataPtr = std::shared_ptr<BatchMetadata>;

/**
 * @brief Metadata class for storing cropping information.
 *
 * This metadata type stores information about the number of crops expected or
 * applied to a buffer, useful for tracking image cropping operations in the pipeline.
 */
class CroppingMetadata : public Metadata
{
  private:
    int m_num_crops;

  public:
    /**
     * @brief Constructs a CroppingMetadata object.
     * @param num_crops The number of crops associated with this buffer
     */
    CroppingMetadata(int num_crops);

    /**
     * @brief Gets the number of crops.
     * @return The number of crops associated with this buffer
     */
    int get_num_crops();
};
using CroppingMetadataPtr = std::shared_ptr<CroppingMetadata>;

/**
 * @brief Metadata class for carrying quality-gated detections through the pipeline.
 *
 * When the quality gate filters out detections that already have cached classifications,
 * those detections are stored in this metadata so they can be re-added to the ROI
 * after aggregation completes.
 */
class SkippedDetectionsMetadata : public Metadata
{
  private:
    std::vector<HailoDetectionPtr> m_skipped_detections;

  public:
    /**
     * @brief Constructs a SkippedDetectionsMetadata object.
     * @param detections Vector of detections that were skipped by the quality gate
     */
    SkippedDetectionsMetadata(std::vector<HailoDetectionPtr> detections);

    /**
     * @brief Gets the skipped detections.
     * @return Const reference to the vector of skipped detections
     */
    const std::vector<HailoDetectionPtr> &get_skipped_detections() const;
};
using SkippedDetectionsMetadataPtr = std::shared_ptr<SkippedDetectionsMetadata>;

/**
 * @brief Represents a single image frame with associated metadata and region of interest.
 *
 * The Buffer class is the core data structure for passing image frames through the analytics pipeline.
 * Each Buffer wraps a HailoMediaLibraryBuffer (the raw frame data) and provides functionality to:
 * - Attach and manage various types of metadata (tensors, batch info, size info, etc.)
 * - Store and retrieve region of interest (ROI) information with detection objects
 * - Enable frame processing and analysis throughout the pipeline
 *
 * HailoMediaLibraryBuffer objects are typically acquired from a MediaLibraryBufferPool. After wrapping as a Buffer,
 * they flow through various pipeline stages where they accumulate metadata from different processing nodes (inference,
 * post-processing, etc.).
 */
class Buffer
{
  private:
    HailoMediaLibraryBufferPtr m_buffer;
    std::vector<MetadataPtr> m_metadata;
    HailoROIPtr m_roi;

  public:
    /**
     * @brief Constructs a Buffer from a HailoMediaLibraryBuffer.
     * @param buffer The underlying media library buffer containing the frame data
     *
     * Initializes a new Buffer with a full-frame ROI (bounding box covering the entire frame).
     */
    Buffer(HailoMediaLibraryBufferPtr buffer);

    /**
     * @brief Copy constructor for Buffer.
     * @param other The Buffer to copy from
     *
     * Creates a shallow copy of the buffer and metadata vector, but creates a deep copy of the ROI.
     */
    Buffer(Buffer &other);

    /**
     * @brief Constructs a Buffer from a HailoMediaLibraryBuffer and a specified ROI.
     * @param buffer The underlying media library buffer containing the frame data
     * @param roi The region of interest associated with this buffer
     */
    Buffer(HailoMediaLibraryBufferPtr buffer, HailoROIPtr roi);

    // Buffer Content methods
    /**
     * @brief Gets the underlying media library buffer.
     * @return Shared pointer to the HailoMediaLibraryBuffer containing the raw frame data
     */
    HailoMediaLibraryBufferPtr get_buffer() const;

    /**
     * @brief Gets the region of interest (ROI) associated with this buffer.
     * @return Shared pointer to the HailoROI object containing detection objects and bounding box information
     */
    HailoROIPtr get_roi() const;

    // Metadata methods
    /**
     * @brief Adds metadata to this buffer.
     * @param metadata Shared pointer to the Metadata object to attach
     *
     * Metadata can be used to store additional information like tensor data, batch information,
     * size properties, or references to other buffers.
     */
    void add_metadata(MetadataPtr metadata);

    /**
     * @brief Removes a specific metadata object from this buffer.
     * @param metadata Shared pointer to the Metadata object to remove
     */
    void remove_metadata(MetadataPtr metadata);

    /**
     * @brief Retrieves all metadata of a specific type.
     * @param metadata_type The MetadataType to filter by
     * @return Vector of shared pointers to Metadata objects matching the specified type
     */
    std::vector<MetadataPtr> get_metadata_of_type(MetadataType metadata_type) const;
};

} // namespace hailo_analytics::pipeline
