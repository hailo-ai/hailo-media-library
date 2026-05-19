#pragma once

#include <cstdint>
#include <string>

#include "media_library/buffer_pool.hpp"

/**
 * @brief RAII wrapper for loading test images as NV12 buffers.
 *
 * Loads PNG/JPEG images via OpenCV, converts BGR→NV12, and wraps the result
 * in a HailoMediaLibraryBufferPtr backed by a MediaLibraryBufferPool.
 * Reusable across test suites (OCR, detection, etc.).
 */
class TestImageBuffer
{
  public:
    /**
     * @brief Load an image file into a pool-acquired NV12 buffer.
     * The pool determines buffer dimensions; the image is resized to fit.
     * Returns an invalid buffer if pool is null, acquisition fails, or image can't be read.
     */
    static TestImageBuffer load_from_file(MediaLibraryBufferPoolPtr pool, const std::string &image_path);

    /**
     * @brief Load an arbitrary-sized image with DSP letterbox resize into a pool-acquired buffer.
     * Creates a temporary pool at the source image dimensions, converts to NV12, then uses
     * DSP perform_resize with DSP_LETTERBOX_MIDDLE to resize into the destination pool buffer.
     * DSP device must be acquired before calling this method.
     */
    static TestImageBuffer load_from_file_letterbox(MediaLibraryBufferPoolPtr pool, const std::string &image_path);

    ~TestImageBuffer();

    TestImageBuffer(TestImageBuffer &&other) noexcept;
    TestImageBuffer &operator=(TestImageBuffer &&other) noexcept;

    TestImageBuffer(const TestImageBuffer &) = delete;
    TestImageBuffer &operator=(const TestImageBuffer &) = delete;

    HailoMediaLibraryBufferPtr get() const;
    bool is_valid() const;
    uint32_t width() const;
    uint32_t height() const;

  private:
    TestImageBuffer();
    void cleanup();

    HailoMediaLibraryBufferPtr m_buffer;
    MediaLibraryBufferPoolPtr m_pool;
    uint32_t m_width;
    uint32_t m_height;
};

/**
 * @brief Fill an existing buffer (e.g. DMA-pool-backed) with NV12 data from an image file.
 * The image is resized to match the buffer's dimensions.
 * For DMA buffers, sync_start/sync_end are called around the memcpy.
 * @return true on success, false on failure (null buffer, bad path, dimension mismatch).
 */
bool fill_nv12_buffer_from_image(HailoMediaLibraryBufferPtr buffer, const std::string &image_path);

/**
 * @brief Resolve a test image filename to its full path using TEST_IMAGES_DIR.
 */
std::string get_test_image_path(const std::string &filename);

/**
 * @brief Resolve an unsized test image filename to its full path.
 */
std::string get_test_image_path_unsized(const std::string &filename);
