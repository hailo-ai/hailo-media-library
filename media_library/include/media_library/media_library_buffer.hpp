#pragma once

#include <memory>
#include <vector>

enum HailoMemoryType
{
    HAILO_MEMORY_TYPE_CMA,
    HAILO_MEMORY_TYPE_DMABUF
};

enum HailoFormat
{
    /** Grayscale format. One plane, each pixel is 8bit */
    HAILO_FORMAT_GRAY8,

    /**
     * RGB (packed) format. One plane, each color component is 8bit \n
     * @code
     * +--+--+--+ +--+--+--+
     * |R0|G0|B0| |R1|G1|B1|
     * +--+--+--+ +--+--+--+
     * @endcode
     */
    HAILO_FORMAT_RGB,

    /**
     * NV12 Format - semiplanar 4:2:0 YUV with interleaved UV plane. Each component is 8bit \n
     * For NV12 format, the dimensions of the image, both width and height, need to be even numbers \n
     * First plane (Y plane): \n
     * @code
     * +--+--+--+
     * |Y0|Y1|Y2|
     * +--+--+--+
     * @endcode
     * Second plane (UV plane): \n
     * @code
     * +--+--+ +--+--+
     * |U0|V0| |U1|V1|
     * +--+--+ +--+--+
     * @endcode
     */
    HAILO_FORMAT_NV12,

    /**
     * A420 Format - planar 4:4:2:0 AYUV. Each component is 8bit \n
     * For A420 format, the dimensions of the image, both width and height, need to be even numbers \n
     * Four planes in the following order: Y plane, U plane, V plane, Alpha plane
     */
    HAILO_FORMAT_A420,

    /**
     * ARGB - RGB with alpha channel first (packed) format. One plane, each color component is 8bit \n
     * @code
     * +--+--+--+--+ +--+--+--+--+
     * |A0|R0|G0|B0| |A1|R1|G1|B1| ...
     * +--+--+--+--+ +--+--+--+--+
     * @endcode
     */
    HAILO_FORMAT_ARGB,

    /** Grayscale format. One plane, each pixel is 16bit */
    HAILO_FORMAT_GRAY16
};

struct hailo_data_plane_t
{
    void *userptr;
    int fd;
    /** Distance in bytes between the leftmost pixels in two adjacent lines */
    size_t bytesperline;
    /** Number of bytes occupied by data (payload) in the plane */
    size_t bytesused;
    template <typename T> T As() const;
};

struct hailo_buffer_data_t
{
    public:
        /** Number of pixels in each row */
        size_t width;
        /** Number of pixels for each column */
        size_t height;
        /** Number of planes in the #planes array */
        size_t planes_count;
        /** Image format */
        HailoFormat format;
        /** Image planes memory type */
        HailoMemoryType memory;
        /** Array of planes */
        std::vector<hailo_data_plane_t> planes;
        hailo_buffer_data_t(size_t width, size_t height, size_t planes_count, HailoFormat format, HailoMemoryType memory, std::vector<hailo_data_plane_t> data_planes)
            : width(width), height(height), planes_count(planes_count), format(format), memory(memory)
        {
            planes.reserve(planes_count);
            for (uint32_t i = 0; i < planes_count; i++)
            {
                planes.emplace_back(std::move(data_planes[i]));
            }
            data_planes.clear();
        }
        // Move constructor
        hailo_buffer_data_t(hailo_buffer_data_t &&other)
            : width(std::move(other.width)), height(std::move(other.height)), planes_count(std::move(other.planes_count)), format(std::move(other.format)), memory(std::move(other.memory)), planes(std::move(other.planes))
        {
            other.planes.clear();
        }
        // Move assignment
        hailo_buffer_data_t &operator=(hailo_buffer_data_t &&other) noexcept
        {
            if (this != &other)
            {
                width = std::move(other.width);
                height = std::move(other.height);
                planes_count = std::move(other.planes_count);
                format = std::move(other.format);
                memory = std::move(other.memory);
                planes = std::move(other.planes);
                other.planes.clear();
            }
            return *this;
        }
        // Copy constructor - delete
        hailo_buffer_data_t(const hailo_buffer_data_t &other) = delete;
        // Copy assignment - delete
        hailo_buffer_data_t &operator=(const hailo_buffer_data_t &other) = delete;
        template <typename T> T As() const;
};

// template <>
// DspImagePropertiesPtr HailoBufferDataPtr::As() const
// {
//     return ;
// }

using HailoBufferDataPtr = std::shared_ptr<hailo_buffer_data_t>;