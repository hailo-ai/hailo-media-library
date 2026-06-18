#pragma once

#include <linux/videodev2.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <atomic>

#include "sensor_types.hpp"
#include "video_buffer.hpp"
#include "dma_buffer.hpp"
#include "media_library_logger.hpp"

namespace HDR
{

class VideoDevice
{
  public:
    virtual ~VideoDevice();
    VideoDevice(v4l2_buf_type m_format_type);

  public:
    virtual bool init(const std::string &device_path, const std::string name, DMABufferAllocator &allocator,
                      unsigned int num_exposures, Resolution res, unsigned int buffers_count, int pixel_format,
                      size_t pixel_width, unsigned int fps = 0, bool queue_buffers_on_stream_start = true,
                      bool timestamp_copy = false);

    virtual bool get_buffer(VideoBuffer **o_buffer, bool add_to_used_count = true);
    virtual bool put_buffer(VideoBuffer *buffer, bool sub_from_used_count = true);

    // Move ownership of buffers currently owned by the device from it to HML
    bool dequeue_buffers();

    // Move ownership of buffers currecntly owned by HML to the device
    bool queue_buffers();
    bool stop_stream();
    bool start_stream();

  protected:
    bool open_device(const std::string &device_path);
    void close_device();
    bool set_format();
    bool init_buffers(DMABufferAllocator &dma_allocator, size_t plane_size, bool timestamp_copy);
    void destroy_buffers();
    bool validate_cap();
    bool set_fps(unsigned int fps);

  public:
    inline unsigned int get_width()
    {
        return m_width;
    }

    inline unsigned int get_height()
    {
        return m_height;
    }

    inline unsigned int get_num_exposures()
    {
        return m_num_exposures;
    }

    inline unsigned int get_pix_fmt()
    {
        return m_pixelformat;
    }

    bool is_stream_on()
    {
        return m_is_stream_on;
    }

  protected:
    v4l2_buf_type get_format_type()
    {
        return m_format_type;
    };

  protected:
    static constexpr LoggerType LOGGER_TYPE = LoggerType::Hdr;

    bool m_initialized;
    unsigned int m_num_exposures;
    unsigned int m_width;
    unsigned int m_height;
    int m_pixelformat;
    int m_fd;
    bool m_is_capture_dev;
    unsigned int m_num_buffers;
    std::vector<VideoBuffer *> m_buffers;
    v4l2_buf_type m_format_type;
    std::atomic<unsigned int> m_used_buffers_count;
    std::string m_name;
    std::string m_buffers_counter_name;
    std::string m_queue_event_name;
    std::string m_dequeue_event_name;
    std::atomic<bool> m_is_stream_on{false};
};

class VideoOutputDevice : public VideoDevice
{
  public:
    VideoOutputDevice();

    ~VideoOutputDevice()
    {
    }

  public:
    bool init(const std::string &device_path, const std::string name, DMABufferAllocator &allocator,
              unsigned int num_exposures, Resolution res, unsigned int buffers_count, int pixel_format,
              size_t pixel_width, unsigned int fps = 0, bool queue_buffers_on_stream_start = true,
              bool timestamp_copy = true) override;

    bool get_buffer(VideoBuffer **o_buffer, bool add_to_used_count = true) override;
    bool put_buffer(VideoBuffer *buffer, bool sub_from_used_count = true) override;

  private:
    int find_first_free_buffer();
    bool mark_buffer_used(unsigned int index);

  private:
    bool m_all_buffers_used;
    std::vector<bool> m_buffer_free;
};

class VideoCaptureDevice : public VideoDevice
{
  public:
    ~VideoCaptureDevice()
    {
    }
    VideoCaptureDevice();
};

} // namespace HDR
