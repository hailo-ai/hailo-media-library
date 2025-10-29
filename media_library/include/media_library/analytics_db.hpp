#pragma once

#include <mutex>
#include <map>
#include <string>
#include <vector>
#include <chrono>
#include <tl/expected.hpp>
#include "buffer_pool.hpp"
#include "hailo/hailort.h"
#include "media_library_types.hpp"

using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;

struct InstanceSegmentationAnalyticsData
{
    Timestamp ts;
    std::vector<hailo_detection_with_byte_mask_t> analytics_buffer;
    HailoMediaLibraryBufferPtr medialib_buffer_ptr; // Optional buffer for byte masks
};

struct DetectionAnalyticsData
{
    Timestamp ts;
    std::vector<hailo_detection_t> analytics_buffer;
};

enum class AnalyticsQueryType
{
    Closest,
    Exact,
    WithinDelta
};

struct AnalyticsQueryOptions
{
    AnalyticsQueryType m_type = AnalyticsQueryType::Closest;
    Timestamp m_ts;
    std::chrono::milliseconds m_delta{0};
    std::chrono::milliseconds m_timeout{0};
};

class AnalyticsDB
{
  public:
    static AnalyticsDB &instance();
    void clear_db();
    void add_configuration(application_analytics_config_t application_analytics_config);

    media_library_return add_detection_entry(const std::string &analytics_id, const DetectionAnalyticsData &data);
    media_library_return add_instance_segmentation_entry(const std::string &analytics_id,
                                                         const InstanceSegmentationAnalyticsData &data);

    tl::expected<DetectionAnalyticsData, media_library_return> query_detection_entry(
        const std::string &analytics_id, const AnalyticsQueryOptions &options);
    tl::expected<InstanceSegmentationAnalyticsData, media_library_return> query_instance_segmentation_entry(
        const std::string &analytics_id, const AnalyticsQueryOptions &options);

    template <typename DataT, typename MapT>
    tl::expected<DataT, media_library_return> query_entry(const MapT &db, const std::string &analytics_id,
                                                          const AnalyticsQueryOptions &options);

    application_analytics_config_t get_application_analytics_config();

  private:
    AnalyticsDB();
    application_analytics_config_t m_application_analytics_config;

    // map<analytics_id, map<Timestamp, AnalyticsData>>
    std::map<std::string, std::map<Timestamp, DetectionAnalyticsData>> m_detection_entries_db;
    std::map<std::string, std::map<Timestamp, InstanceSegmentationAnalyticsData>> m_instance_segmentation_entries_db;
    std::mutex m_mutex;
    std::condition_variable m_cv;

    template <typename DataT, typename MapT, typename ConfigMapT>
    media_library_return add_entry(MapT &db, const std::string &analytics_id, DataT data, const ConfigMapT &config_map);

    template <typename DataT, typename InnerMapT>
    static tl::expected<DataT, media_library_return> find_closest(const InnerMapT &inner_map, Timestamp ts);
    template <typename DataT, typename InnerMapT>
    static tl::expected<DataT, media_library_return> find_exact(const InnerMapT &inner_map, Timestamp ts);
    template <typename DataT, typename InnerMapT>
    static tl::expected<DataT, media_library_return> find_within_delta(const InnerMapT &inner_map, Timestamp ts,
                                                                       std::chrono::milliseconds delta);
};
