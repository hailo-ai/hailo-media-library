#include "metadata_writer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include <jpeglib.h>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"

namespace vlm_event_monitor
{

namespace fs = std::filesystem;

namespace
{
// Returns "YYYY-MM-DD_HH-MM-SS" for the current local time.
std::string format_timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", &tm);
    return buffer;
}
} // namespace

MetadataWriter::MetadataWriter(std::string root_dir, uint32_t keep_last)
    : m_root_dir(std::move(root_dir)), m_keep_last(keep_last)
{
    if (m_keep_last == 0)
    {
        m_keep_last = 1;
    }
    std::error_code error;
    fs::create_directories(m_root_dir, error);
    if (error)
    {
        HAILO_ANALYTICS_LOG_ERROR("MetadataWriter: failed to create {}: {}", m_root_dir, error.message());
    }
}

void MetadataWriter::evict_oldest_cycles()
{
    std::error_code error;
    if (!fs::exists(m_root_dir, error))
    {
        return;
    }

    // Collect immediate sub-directories with their last-write times.
    std::vector<std::pair<fs::file_time_type, fs::path>> entries;
    for (const auto &entry : fs::directory_iterator(m_root_dir, error))
    {
        if (!entry.is_directory(error))
        {
            continue;
        }
        const auto mtime = fs::last_write_time(entry.path(), error);
        if (error)
        {
            continue;
        }
        entries.emplace_back(mtime, entry.path());
    }

    // Sort oldest-first by mtime.
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    // We're about to add ONE new cycle dir, so keep at most keep_last - 1.
    const size_t target_remaining = m_keep_last > 0 ? static_cast<size_t>(m_keep_last - 1) : 0;
    while (entries.size() > target_remaining)
    {
        const auto &oldest = entries.front().second;
        fs::remove_all(oldest, error);
        if (error)
        {
            HAILO_ANALYTICS_LOG_WARN("MetadataWriter: failed to evict {}: {}", oldest.string(), error.message());
        }
        entries.erase(entries.begin());
    }
}

std::string MetadataWriter::begin_cycle()
{
    evict_oldest_cycles();

    const uint64_t seq = m_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    std::ostringstream name;
    name << format_timestamp() << "-" << std::setw(3) << std::setfill('0') << seq;

    fs::path cycle_dir = fs::path(m_root_dir) / name.str();
    std::error_code error;
    fs::create_directories(cycle_dir, error);
    if (error)
    {
        HAILO_ANALYTICS_LOG_ERROR("MetadataWriter: failed to create {}: {}", cycle_dir.string(), error.message());
        return {};
    }
    return cycle_dir.string();
}

// Shared encode loop used by both save_frame_jpeg (file dest) and
// encode_rgb_to_jpeg (memory dest). The destination is configured by the
// caller before this is invoked. cinfo must already be jpeg_create_compress'd
// and have its destination set (jpeg_stdio_dest or jpeg_mem_dest).
static void encode_rgb_into(struct jpeg_compress_struct &cinfo, const std::vector<uint8_t> &rgb, uint32_t height,
                            uint32_t width)
{
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 90, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    const size_t row_stride = static_cast<size_t>(width) * 3;
    while (cinfo.next_scanline < cinfo.image_height)
    {
        const auto *row_ptr = rgb.data() + cinfo.next_scanline * row_stride;
        // libjpeg's API requires non-const JSAMPROW; the data is read-only
        // in practice so the const_cast is safe.
        JSAMPROW row = const_cast<JSAMPROW>(row_ptr);
        jpeg_write_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_compress(&cinfo);
}

bool MetadataWriter::save_frame_jpeg(const std::string &cycle_dir, size_t idx, const std::vector<uint8_t> &rgb,
                                     uint32_t height, uint32_t width)
{
    const size_t expected = static_cast<size_t>(height) * width * 3;
    if (rgb.size() < expected)
    {
        HAILO_ANALYTICS_LOG_WARN("MetadataWriter: RGB buffer too small ({} < {})", rgb.size(), expected);
        return false;
    }

    std::ostringstream filename;
    filename << "frame_" << idx << ".jpg";
    const fs::path path = fs::path(cycle_dir) / filename.str();

    FILE *outfile = std::fopen(path.c_str(), "wb");
    if (outfile == nullptr)
    {
        HAILO_ANALYTICS_LOG_WARN("MetadataWriter: fopen failed for {}", path.string());
        return false;
    }

    struct jpeg_compress_struct cinfo{};
    struct jpeg_error_mgr error_manager{};
    cinfo.err = jpeg_std_error(&error_manager);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, outfile);

    encode_rgb_into(cinfo, rgb, height, width);

    std::fclose(outfile);
    jpeg_destroy_compress(&cinfo);
    return true;
}

std::vector<uint8_t> MetadataWriter::encode_rgb_to_jpeg(const std::vector<uint8_t> &rgb, uint32_t height,
                                                        uint32_t width)
{
    const size_t expected = static_cast<size_t>(height) * width * 3;
    if (rgb.size() < expected)
    {
        HAILO_ANALYTICS_LOG_WARN("MetadataWriter: encode_rgb_to_jpeg buffer too small ({} < {})", rgb.size(), expected);
        return {};
    }

    struct jpeg_compress_struct cinfo{};
    struct jpeg_error_mgr error_manager{};
    cinfo.err = jpeg_std_error(&error_manager);
    jpeg_create_compress(&cinfo);

    // libjpeg-turbo's jpeg_mem_dest allocates and grows a buffer as needed.
    // We hand it a nullptr/0 pair and it'll malloc, then free via jpeg_destroy_compress.
    unsigned char *outbuf = nullptr;
    unsigned long outsize = 0;
    jpeg_mem_dest(&cinfo, &outbuf, &outsize);

    encode_rgb_into(cinfo, rgb, height, width);

    std::vector<uint8_t> result(outbuf, outbuf + outsize);
    // jpeg_destroy_compress does NOT free the buffer allocated by
    // jpeg_mem_dest — per libjpeg-turbo docs, the caller must free() it.
    jpeg_destroy_compress(&cinfo);
    if (outbuf != nullptr)
    {
        std::free(outbuf);
    }
    return result;
}

bool MetadataWriter::save_metadata_json(const std::string &cycle_dir, const std::string &json_str)
{
    const fs::path path = fs::path(cycle_dir) / "metadata.json";
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out)
    {
        HAILO_ANALYTICS_LOG_WARN("MetadataWriter: failed to open {} for write", path.string());
        return false;
    }
    out.write(json_str.data(), static_cast<std::streamsize>(json_str.size()));
    return out.good();
}

} // namespace vlm_event_monitor
