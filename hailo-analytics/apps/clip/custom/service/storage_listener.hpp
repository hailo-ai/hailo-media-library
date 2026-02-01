#pragma once

#include <string>
#include <vector>
#include <cstdint>

class IStorageListener
{
  public:
    // Storage information structure
    struct StorageInfo
    {
        // Mount point information (in bytes)
        uint64_t mount_total_space;
        uint64_t mount_free_space;
        uint64_t mount_used_space;

        // Directory sizes (in bytes)
        uint64_t root_directory_size;
        uint64_t database_directory_size;
        uint64_t faissdb_directory_size;
        uint64_t thumbnail_directory_size;
        uint64_t video_directory_size;

        // Calculated values
        float disk_usage_percent;
        bool is_low_disk_space;
    };

    virtual ~IStorageListener() = default;
    virtual void on_storage_update_notification(const StorageInfo &info) = 0;
};
