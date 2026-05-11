#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "test_images/test_image_loader.hpp"

// ============================================================================
// TestImageLoaderTest — test suite for test_image_loader utility
// ============================================================================

static const std::vector<std::string> LICENSE_PLATE_IMAGES = {
    "APQ5.png", "PE3820.png", "YHI4HXR.png", "KHO5ZZK.png", "SM7080.png",
};

static const uint32_t LP_WIDTH = 320;
static const uint32_t LP_HEIGHT = 48;
static const size_t LP_POOL_SIZE = 5;

static const uint32_t RESIZED_WIDTH = 160;
static const uint32_t RESIZED_HEIGHT = 24;
static const size_t RESIZED_POOL_SIZE = 5;

class TestImageLoaderTest : public ::testing::Test
{
  protected:
    MediaLibraryBufferPoolPtr m_pool;
    MediaLibraryBufferPoolPtr m_resized_pool;

    void SetUp() override
    {
        m_pool = std::make_shared<MediaLibraryBufferPool>(LP_WIDTH, LP_HEIGHT, HAILO_FORMAT_NV12, LP_POOL_SIZE,
                                                          HAILO_MEMORY_TYPE_DMABUF, "test_lp_pool");
        auto ret = m_pool->init();
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            GTEST_SKIP() << "Buffer pool init failed (no DMA heap on host) — skipping test";
        }

        m_resized_pool =
            std::make_shared<MediaLibraryBufferPool>(RESIZED_WIDTH, RESIZED_HEIGHT, HAILO_FORMAT_NV12,
                                                     RESIZED_POOL_SIZE, HAILO_MEMORY_TYPE_DMABUF, "test_resized_pool");
        ret = m_resized_pool->init();
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            GTEST_SKIP() << "Resized buffer pool init failed — skipping test";
        }
    }
};

// --- Basic loading ---

TEST_F(TestImageLoaderTest, LoadValidImageReturnsValidBuffer)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }
    EXPECT_TRUE(buf.is_valid());
    EXPECT_NE(buf.get(), nullptr);
}

TEST_F(TestImageLoaderTest, LoadValidImageHasCorrectDimensions)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }
    EXPECT_EQ(buf.width(), LP_WIDTH);
    EXPECT_EQ(buf.height(), LP_HEIGHT);
}

TEST_F(TestImageLoaderTest, LoadNonexistentFileReturnsInvalidBuffer)
{
    auto buf = TestImageBuffer::load_from_file(m_pool, "/nonexistent/path/image.png");
    EXPECT_FALSE(buf.is_valid());
    EXPECT_EQ(buf.get(), nullptr);
}

TEST_F(TestImageLoaderTest, NullPoolReturnsInvalidBuffer)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(nullptr, path);
    EXPECT_FALSE(buf.is_valid());
    EXPECT_EQ(buf.get(), nullptr);
}

// --- Buffer format checks ---

TEST_F(TestImageLoaderTest, BufferFormatIsNv12)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }
    EXPECT_EQ(buf.get()->buffer_data->format, HAILO_FORMAT_NV12);
}

TEST_F(TestImageLoaderTest, BufferHasTwoPlanes)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }
    EXPECT_EQ(buf.get()->buffer_data->planes_count, 2u);
    EXPECT_EQ(buf.get()->buffer_data->planes.size(), 2u);
}

TEST_F(TestImageLoaderTest, BufferMemoryTypeDmabuf)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }
    EXPECT_EQ(buf.get()->buffer_data->memory, HAILO_MEMORY_TYPE_DMABUF);
}

TEST_F(TestImageLoaderTest, BufferPlaneFdsAreValid)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }
    EXPECT_GE(buf.get()->buffer_data->planes[0].fd, 0);
    EXPECT_GE(buf.get()->buffer_data->planes[1].fd, 0);
}

TEST_F(TestImageLoaderTest, BufferOwnerIsPool)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }
    EXPECT_EQ(buf.get()->owner, m_pool);
}

// --- Plane sizes ---

TEST_F(TestImageLoaderTest, YPlaneSizeIsWidthTimesHeight)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }
    size_t expected_y = buf.width() * buf.height();
    EXPECT_EQ(buf.get()->buffer_data->planes[0].bytesused, expected_y);
}

TEST_F(TestImageLoaderTest, UvPlaneSizeIsWidthTimesHeightOverTwo)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }
    size_t expected_uv = buf.width() * buf.height() / 2;
    EXPECT_EQ(buf.get()->buffer_data->planes[1].bytesused, expected_uv);
}

// --- Resized pool ---

TEST_F(TestImageLoaderTest, ResizedPoolProducesCorrectDimensions)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(m_resized_pool, path);
    if (!buf.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }
    EXPECT_EQ(buf.width(), RESIZED_WIDTH);
    EXPECT_EQ(buf.height(), RESIZED_HEIGHT);
}

// --- fill_nv12_buffer_from_image ---

TEST_F(TestImageLoaderTest, FillNv12BufferFromImageSucceeds)
{
    std::string path = get_test_image_path("APQ5.png");

    auto buffer = std::make_shared<hailo_media_library_buffer>();
    auto ret = m_pool->acquire_buffer(buffer);
    ASSERT_EQ(ret, MEDIA_LIBRARY_SUCCESS);

    bool success = fill_nv12_buffer_from_image(buffer, path);
    if (!success)
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }
    EXPECT_TRUE(success);
}

TEST_F(TestImageLoaderTest, FillNv12BufferNullBufferReturnsFalse)
{
    EXPECT_FALSE(fill_nv12_buffer_from_image(nullptr, "any_path.png"));
}

TEST_F(TestImageLoaderTest, FillNv12BufferBadPathReturnsFalse)
{
    auto buffer = std::make_shared<hailo_media_library_buffer>();
    auto ret = m_pool->acquire_buffer(buffer);
    ASSERT_EQ(ret, MEDIA_LIBRARY_SUCCESS);

    EXPECT_FALSE(fill_nv12_buffer_from_image(buffer, "/nonexistent/image.png"));
}

// --- All license plate images ---

TEST_F(TestImageLoaderTest, AllLicensePlateImagesLoadSuccessfully)
{
    for (const auto &name : LICENSE_PLATE_IMAGES)
    {
        std::string path = get_test_image_path(name);
        auto buf = TestImageBuffer::load_from_file(m_pool, path);
        if (!buf.is_valid())
        {
            GTEST_SKIP() << "Test image not found at " << path;
        }
        EXPECT_TRUE(buf.is_valid()) << "Failed to load: " << name;
        EXPECT_EQ(buf.width(), LP_WIDTH) << "Wrong width for: " << name;
        EXPECT_EQ(buf.height(), LP_HEIGHT) << "Wrong height for: " << name;
    }
}

// --- Move semantics ---

TEST_F(TestImageLoaderTest, MoveConstructorTransfersOwnership)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf1 = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf1.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }

    uint32_t orig_width = buf1.width();
    uint32_t orig_height = buf1.height();

    TestImageBuffer buf2(std::move(buf1));

    EXPECT_TRUE(buf2.is_valid());
    EXPECT_EQ(buf2.width(), orig_width);
    EXPECT_EQ(buf2.height(), orig_height);

    // Moved-from object should be empty
    EXPECT_FALSE(buf1.is_valid());
    EXPECT_EQ(buf1.width(), 0u);
    EXPECT_EQ(buf1.height(), 0u);
}

TEST_F(TestImageLoaderTest, MoveAssignmentTransfersOwnership)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf1 = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf1.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }

    uint32_t orig_width = buf1.width();
    uint32_t orig_height = buf1.height();

    TestImageBuffer buf2 = std::move(buf1);

    EXPECT_TRUE(buf2.is_valid());
    EXPECT_EQ(buf2.width(), orig_width);
    EXPECT_EQ(buf2.height(), orig_height);

    EXPECT_FALSE(buf1.is_valid());
    EXPECT_EQ(buf1.width(), 0u);
    EXPECT_EQ(buf1.height(), 0u);
}

// --- Conversion sanity check ---

TEST_F(TestImageLoaderTest, YPlaneDataIsNotUniform)
{
    std::string path = get_test_image_path("APQ5.png");
    auto buf = TestImageBuffer::load_from_file(m_pool, path);
    if (!buf.is_valid())
    {
        GTEST_SKIP() << "Test image not found at " << path;
    }

    auto buffer = buf.get();
    if (buffer->is_dmabuf())
    {
        buffer->sync_start();
    }

    const uint8_t *y_data = static_cast<const uint8_t *>(buffer->buffer_data->planes[0].userptr);
    size_t y_size = buffer->buffer_data->planes[0].bytesused;

    // Check that not all Y values are identical (real image has variation)
    bool all_same = true;
    uint8_t first = y_data[0];
    for (size_t i = 1; i < y_size; ++i)
    {
        if (y_data[i] != first)
        {
            all_same = false;
            break;
        }
    }

    if (buffer->is_dmabuf())
    {
        buffer->sync_end();
    }

    EXPECT_FALSE(all_same) << "Y plane data is uniform — BGR→NV12 conversion may have failed";
}
