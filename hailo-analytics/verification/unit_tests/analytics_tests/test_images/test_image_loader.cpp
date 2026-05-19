#include "test_image_loader.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <hailodsp.h>
#include <hailodsp_base.h>
#include <media_library/buffer_pool.hpp>
#include <media_library/dsp_utils.hpp>
#include <media_library/media_library_types.hpp>
#include <opencv2/core.hpp>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "media_library/media_library_buffer.hpp"

namespace
{

/**
 * @brief Round up to the nearest even number (NV12 requires even dimensions).
 */
uint32_t round_to_even(uint32_t val)
{
    return (val + 1) & ~1u;
}

/**
 * @brief Convert a BGR cv::Mat to NV12 format (Y plane + interleaved UV plane).
 * @param bgr Input BGR image. Dimensions must be even.
 * @param y_out Output Y plane data (size = width * height).
 * @param uv_out Output UV plane data (size = width * height / 2).
 */
void bgr_to_nv12(const cv::Mat &bgr, std::vector<uint8_t> &y_out, std::vector<uint8_t> &uv_out)
{
    // Convert BGR → YUV I420 (planar: Y, U, V each as separate planes)
    cv::Mat yuv_i420;
    cv::cvtColor(bgr, yuv_i420, cv::COLOR_BGR2YUV_I420);

    uint32_t width = bgr.cols;
    uint32_t height = bgr.rows;
    size_t y_size = width * height;
    size_t uv_width = width / 2;
    size_t uv_height = height / 2;
    size_t uv_plane_size = uv_width * uv_height;

    // Y plane: first width*height bytes of I420
    const uint8_t *y_ptr = yuv_i420.data;
    y_out.assign(y_ptr, y_ptr + y_size);

    // U and V planes in I420 are sequential after Y
    const uint8_t *u_ptr = yuv_i420.data + y_size;
    const uint8_t *v_ptr = yuv_i420.data + y_size + uv_plane_size;

    // Interleave U and V into NV12 UV plane
    uv_out.resize(uv_plane_size * 2);
    for (size_t i = 0; i < uv_plane_size; ++i)
    {
        uv_out[2 * i] = u_ptr[i];
        uv_out[2 * i + 1] = v_ptr[i];
    }
}

} // anonymous namespace

// === TestImageBuffer ===

TestImageBuffer::TestImageBuffer() : m_buffer(nullptr), m_pool(nullptr), m_width(0), m_height(0)
{
}

TestImageBuffer::~TestImageBuffer()
{
    cleanup();
}

TestImageBuffer::TestImageBuffer(TestImageBuffer &&other) noexcept
    : m_buffer(std::move(other.m_buffer)), m_pool(std::move(other.m_pool)), m_width(other.m_width),
      m_height(other.m_height)
{
    other.m_width = 0;
    other.m_height = 0;
}

TestImageBuffer &TestImageBuffer::operator=(TestImageBuffer &&other) noexcept
{
    if (this != &other)
    {
        cleanup();
        m_buffer = std::move(other.m_buffer);
        m_pool = std::move(other.m_pool);
        m_width = other.m_width;
        m_height = other.m_height;
        other.m_width = 0;
        other.m_height = 0;
    }
    return *this;
}

TestImageBuffer TestImageBuffer::load_from_file(MediaLibraryBufferPoolPtr pool, const std::string &image_path)
{
    TestImageBuffer result;

    if (!pool)
    {
        return result;
    }

    auto buffer = std::make_shared<hailo_media_library_buffer>();
    media_library_return ret = pool->acquire_buffer(buffer);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        return result;
    }

    if (!fill_nv12_buffer_from_image(buffer, image_path))
    {
        return result;
    }

    result.m_buffer = buffer;
    result.m_pool = pool;
    result.m_width = pool->get_width();
    result.m_height = pool->get_height();

    return result;
}

TestImageBuffer TestImageBuffer::load_from_file_letterbox(MediaLibraryBufferPoolPtr pool, const std::string &image_path)
{
    TestImageBuffer result;

    if (!pool)
    {
        return result;
    }

    // Read original image to get its dimensions
    cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
    if (bgr.empty())
    {
        return result;
    }

    uint32_t src_width = round_to_even(static_cast<uint32_t>(bgr.cols));
    uint32_t src_height = round_to_even(static_cast<uint32_t>(bgr.rows));

    // Resize to even dimensions if needed
    if (src_width != static_cast<uint32_t>(bgr.cols) || src_height != static_cast<uint32_t>(bgr.rows))
    {
        cv::resize(bgr, bgr, cv::Size(src_width, src_height));
    }

    // Create temporary pool at source dimensions
    auto src_pool = std::make_shared<MediaLibraryBufferPool>(src_width, src_height, HAILO_FORMAT_NV12, 1,
                                                             HAILO_MEMORY_TYPE_DMABUF, "letterbox_src_pool");
    auto pool_ret = src_pool->init();
    if (pool_ret != MEDIA_LIBRARY_SUCCESS)
    {
        return result;
    }

    // Acquire and fill source buffer with NV12 data
    auto src_buffer = std::make_shared<hailo_media_library_buffer>();
    if (src_pool->acquire_buffer(src_buffer) != MEDIA_LIBRARY_SUCCESS)
    {
        return result;
    }

    std::vector<uint8_t> y_data, uv_data;
    bgr_to_nv12(bgr, y_data, uv_data);

    auto &y_plane = src_buffer->buffer_data->planes[0];
    auto &uv_plane = src_buffer->buffer_data->planes[1];

    if (!y_plane.userptr || !uv_plane.userptr)
    {
        return result;
    }

    if (src_buffer->is_dmabuf())
    {
        src_buffer->sync_start();
    }
    std::memcpy(y_plane.userptr, y_data.data(), y_data.size());
    std::memcpy(uv_plane.userptr, uv_data.data(), uv_data.size());
    if (src_buffer->is_dmabuf())
    {
        src_buffer->sync_end();
    }

    // Acquire destination buffer from target pool
    auto dst_buffer = std::make_shared<hailo_media_library_buffer>();
    if (pool->acquire_buffer(dst_buffer) != MEDIA_LIBRARY_SUCCESS)
    {
        return result;
    }

    // Perform DSP letterbox resize
    dsp_letterbox_properties_t letterbox_props{};
    letterbox_props.alignment = DSP_LETTERBOX_MIDDLE;
    letterbox_props.color.y = 0;
    letterbox_props.color.u = 128;
    letterbox_props.color.v = 128;

    dsp_status dsp_ret = dsp_utils::perform_resize(src_buffer->buffer_data.get(), dst_buffer->buffer_data.get(),
                                                   INTERPOLATION_TYPE_BILINEAR, letterbox_props);
    if (dsp_ret != DSP_SUCCESS)
    {
        return result;
    }

    result.m_buffer = dst_buffer;
    result.m_pool = pool;
    result.m_width = pool->get_width();
    result.m_height = pool->get_height();

    return result;
}

HailoMediaLibraryBufferPtr TestImageBuffer::get() const
{
    return m_buffer;
}

bool TestImageBuffer::is_valid() const
{
    return m_buffer != nullptr && m_buffer->buffer_data != nullptr;
}

uint32_t TestImageBuffer::width() const
{
    return m_width;
}

uint32_t TestImageBuffer::height() const
{
    return m_height;
}

void TestImageBuffer::cleanup()
{
    m_buffer.reset();
    m_pool.reset();
}

// === Free functions ===

bool fill_nv12_buffer_from_image(HailoMediaLibraryBufferPtr buffer, const std::string &image_path)
{
    if (!buffer || !buffer->buffer_data || buffer->buffer_data->planes.size() < 2)
    {
        return false;
    }

    cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
    if (bgr.empty())
    {
        return false;
    }

    uint32_t width = buffer->buffer_data->width;
    uint32_t height = buffer->buffer_data->height;

    if (width == 0 || height == 0)
    {
        return false;
    }

    cv::Mat resized;
    if (static_cast<uint32_t>(bgr.cols) != width || static_cast<uint32_t>(bgr.rows) != height)
    {
        cv::resize(bgr, resized, cv::Size(width, height));
    }
    else
    {
        resized = bgr;
    }

    std::vector<uint8_t> y_data, uv_data;
    bgr_to_nv12(resized, y_data, uv_data);

    auto &y_plane = buffer->buffer_data->planes[0];
    auto &uv_plane = buffer->buffer_data->planes[1];

    if (!y_plane.userptr || !uv_plane.userptr)
    {
        return false;
    }

    if (y_plane.bytesused < y_data.size() || uv_plane.bytesused < uv_data.size())
    {
        return false;
    }

    if (buffer->is_dmabuf())
    {
        buffer->sync_start();
    }

    std::memcpy(y_plane.userptr, y_data.data(), y_data.size());
    std::memcpy(uv_plane.userptr, uv_data.data(), uv_data.size());

    if (buffer->is_dmabuf())
    {
        buffer->sync_end();
    }

    return true;
}

std::string get_test_image_path(const std::string &filename)
{
#ifdef TEST_IMAGES_DIR
    return std::string(TEST_IMAGES_DIR) + "/license_plates/" + filename;
#else
    return "test_images/license_plates/" + filename;
#endif
}

std::string get_test_image_path_unsized(const std::string &filename)
{
#ifdef TEST_IMAGES_DIR
    return std::string(TEST_IMAGES_DIR) + "/license_plates_unsized/" + filename;
#else
    return "test_images/license_plates_unsized/" + filename;
#endif
}
