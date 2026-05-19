#include <media_library/buffer_pool.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "core_tests_common.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace hailo_analytics::pipeline;
using ::testing::_;
using ::testing::Return;

// ============================================================================
// Metadata Tests
// ============================================================================

TEST_F(MetadataTest, DefaultConstructor)
{
    Metadata metadata;
    EXPECT_EQ(metadata.get_type(), MetadataType::UNKNOWN);
}

TEST_F(MetadataTest, ConstructorWithType)
{
    Metadata metadata_tensor(MetadataType::TENSOR);
    EXPECT_EQ(metadata_tensor.get_type(), MetadataType::TENSOR);

    Metadata metadata_crops(MetadataType::EXPECTED_CROPS);
    EXPECT_EQ(metadata_crops.get_type(), MetadataType::EXPECTED_CROPS);

    Metadata metadata_size(MetadataType::SIZE);
    EXPECT_EQ(metadata_size.get_type(), MetadataType::SIZE);

    Metadata metadata_batch(MetadataType::BATCH);
    EXPECT_EQ(metadata_batch.get_type(), MetadataType::BATCH);
}

TEST_F(MetadataTest, GetType)
{
    Metadata metadata(MetadataType::TENSOR);
    MetadataType type = metadata.get_type();
    EXPECT_EQ(type, MetadataType::TENSOR);
}

TEST_F(MetadataTest, SharedPtrCreation)
{
    MetadataPtr metadata_ptr = std::make_shared<Metadata>(MetadataType::TENSOR);
    ASSERT_NE(metadata_ptr, nullptr);
    EXPECT_EQ(metadata_ptr->get_type(), MetadataType::TENSOR);
}

// ============================================================================
// Buffer Tests
// ============================================================================

TEST_F(BufferTest, Constructor)
{
    ASSERT_NO_THROW({ Buffer buffer(mock_buffer); });
}

TEST_F(BufferTest, CopyConstructor)
{
    Buffer buffer1(mock_buffer);
    ASSERT_NO_THROW({ Buffer buffer2(buffer1); });
}

TEST_F(BufferTest, GetBuffer)
{
    Buffer buffer(mock_buffer);
    HailoMediaLibraryBufferPtr retrieved_buffer = buffer.get_buffer();
    EXPECT_EQ(retrieved_buffer, mock_buffer);
}

TEST_F(BufferTest, AddMetadata)
{
    Buffer buffer(mock_buffer);
    MetadataPtr metadata = std::make_shared<Metadata>(MetadataType::TENSOR);

    ASSERT_NO_THROW({ buffer.add_metadata(metadata); });
}

TEST_F(BufferTest, AddMultipleMetadata)
{
    Buffer buffer(mock_buffer);
    MetadataPtr metadata1 = std::make_shared<Metadata>(MetadataType::TENSOR);
    MetadataPtr metadata2 = std::make_shared<Metadata>(MetadataType::SIZE);
    MetadataPtr metadata3 = std::make_shared<Metadata>(MetadataType::BATCH);

    ASSERT_NO_THROW({
        buffer.add_metadata(metadata1);
        buffer.add_metadata(metadata2);
        buffer.add_metadata(metadata3);
    });
}

TEST_F(BufferTest, RemoveMetadata)
{
    Buffer buffer(mock_buffer);
    MetadataPtr metadata = std::make_shared<Metadata>(MetadataType::TENSOR);

    buffer.add_metadata(metadata);
    ASSERT_NO_THROW({ buffer.remove_metadata(metadata); });
}

TEST_F(BufferTest, GetMetadataOfType)
{
    Buffer buffer(mock_buffer);
    MetadataPtr metadata_tensor1 = std::make_shared<Metadata>(MetadataType::TENSOR);
    MetadataPtr metadata_tensor2 = std::make_shared<Metadata>(MetadataType::TENSOR);
    MetadataPtr metadata_size = std::make_shared<Metadata>(MetadataType::SIZE);

    buffer.add_metadata(metadata_tensor1);
    buffer.add_metadata(metadata_tensor2);
    buffer.add_metadata(metadata_size);

    std::vector<MetadataPtr> tensor_metadata = buffer.get_metadata_of_type(MetadataType::TENSOR);
    EXPECT_EQ(tensor_metadata.size(), 2);

    std::vector<MetadataPtr> size_metadata = buffer.get_metadata_of_type(MetadataType::SIZE);
    EXPECT_EQ(size_metadata.size(), 1);

    std::vector<MetadataPtr> batch_metadata = buffer.get_metadata_of_type(MetadataType::BATCH);
    EXPECT_EQ(batch_metadata.size(), 0);
}

TEST_F(BufferTest, GetMetadataOfTypeAfterRemoval)
{
    Buffer buffer(mock_buffer);
    MetadataPtr metadata_tensor1 = std::make_shared<Metadata>(MetadataType::TENSOR);
    MetadataPtr metadata_tensor2 = std::make_shared<Metadata>(MetadataType::TENSOR);

    buffer.add_metadata(metadata_tensor1);
    buffer.add_metadata(metadata_tensor2);

    std::vector<MetadataPtr> tensor_metadata_before = buffer.get_metadata_of_type(MetadataType::TENSOR);
    EXPECT_EQ(tensor_metadata_before.size(), 2);

    buffer.remove_metadata(metadata_tensor1);

    std::vector<MetadataPtr> tensor_metadata_after = buffer.get_metadata_of_type(MetadataType::TENSOR);
    EXPECT_EQ(tensor_metadata_after.size(), 1);
}

TEST_F(BufferTest, AddRemoveMultipleMetadataTypes)
{
    Buffer buffer(mock_buffer);
    MetadataPtr metadata_tensor = std::make_shared<Metadata>(MetadataType::TENSOR);
    MetadataPtr metadata_crops = std::make_shared<Metadata>(MetadataType::EXPECTED_CROPS);
    MetadataPtr metadata_size = std::make_shared<Metadata>(MetadataType::SIZE);
    MetadataPtr metadata_batch = std::make_shared<Metadata>(MetadataType::BATCH);

    buffer.add_metadata(metadata_tensor);
    buffer.add_metadata(metadata_crops);
    buffer.add_metadata(metadata_size);
    buffer.add_metadata(metadata_batch);

    EXPECT_EQ(buffer.get_metadata_of_type(MetadataType::TENSOR).size(), 1);
    EXPECT_EQ(buffer.get_metadata_of_type(MetadataType::EXPECTED_CROPS).size(), 1);
    EXPECT_EQ(buffer.get_metadata_of_type(MetadataType::SIZE).size(), 1);
    EXPECT_EQ(buffer.get_metadata_of_type(MetadataType::BATCH).size(), 1);

    buffer.remove_metadata(metadata_crops);
    buffer.remove_metadata(metadata_batch);

    EXPECT_EQ(buffer.get_metadata_of_type(MetadataType::TENSOR).size(), 1);
    EXPECT_EQ(buffer.get_metadata_of_type(MetadataType::EXPECTED_CROPS).size(), 0);
    EXPECT_EQ(buffer.get_metadata_of_type(MetadataType::SIZE).size(), 1);
    EXPECT_EQ(buffer.get_metadata_of_type(MetadataType::BATCH).size(), 0);
}

TEST_F(BufferTest, RemoveNonExistentMetadata)
{
    Buffer buffer(mock_buffer);
    MetadataPtr metadata1 = std::make_shared<Metadata>(MetadataType::TENSOR);
    MetadataPtr metadata2 = std::make_shared<Metadata>(MetadataType::SIZE);

    buffer.add_metadata(metadata1);

    // Attempting to remove metadata that was never added
    ASSERT_NO_THROW({ buffer.remove_metadata(metadata2); });
}

TEST_F(BufferTest, BufferPtrCreation)
{
    BufferPtr buffer_ptr = std::make_shared<Buffer>(mock_buffer);
    ASSERT_NE(buffer_ptr, nullptr);
    EXPECT_EQ(buffer_ptr->get_buffer(), mock_buffer);
}

TEST_F(BufferTest, CopyConstructorPreservesMetadata)
{
    Buffer buffer1(mock_buffer);
    MetadataPtr metadata = std::make_shared<Metadata>(MetadataType::TENSOR);
    buffer1.add_metadata(metadata);

    Buffer buffer2(buffer1);

    std::vector<MetadataPtr> metadata_list = buffer2.get_metadata_of_type(MetadataType::TENSOR);
    EXPECT_EQ(metadata_list.size(), 1);
}

TEST_F(BufferTest, MultipleBuffersWithSameUnderlyingBuffer)
{
    Buffer buffer1(mock_buffer);
    Buffer buffer2(mock_buffer);

    EXPECT_EQ(buffer1.get_buffer(), buffer2.get_buffer());

    // But they should have independent metadata
    MetadataPtr metadata = std::make_shared<Metadata>(MetadataType::TENSOR);
    buffer1.add_metadata(metadata);

    EXPECT_EQ(buffer1.get_metadata_of_type(MetadataType::TENSOR).size(), 1);
    EXPECT_EQ(buffer2.get_metadata_of_type(MetadataType::TENSOR).size(), 0);
}
