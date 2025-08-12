#pragma once

#include <string>
#include <vector>
#include "video_buffer.hpp"
#include "dma_buffer.hpp"
#include "media_library_logger.hpp"

namespace HDR
{

enum DOL
{
    HDR_DOL_2 = 2,
    HDR_DOL_3,
};

enum InputResolution
{
    RES_FHD,
    RES_4K,
    RES_4MP,
};

enum class SensorType
{
    IMX334,
    IMX664,
    IMX675,
    IMX678,
    IMX715,
};

class VideoDevice
{
  public:
    virtual ~VideoDevice();
    VideoDevice(v4l2_buf_type m_format_type);

  public:
    virtual bool init(const std::string &device_path, const std::string name, DMABufferAllocator &allocator,
                      unsigned int num_exposures, HDR::InputResolution res, unsigned int buffers_count,
                      int pixel_format, size_t pixel_width, unsigned int fps = 0,
                      bool queue_buffers_on_stream_start = true, bool timestamp_copy = false);

    virtual bool get_buffer(VideoBuffer **o_buffer);
    virtual bool put_buffer(VideoBuffer *buffer);
    bool dequeue_buffers();
    bool queue_buffers();

  protected:
    bool open_device(const std::string &device_path);
    void close_device();
    bool set_format();
    bool init_buffers(DMABufferAllocator &dma_allocator, size_t plane_size, bool timestamp_copy);
    void destroy_buffers();
    bool validate_cap();
    bool set_fps(unsigned int fps);
    bool start_stream();
    bool stop_stream();

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
    unsigned int m_used_buffers_count;
    std::string m_name;
    std::string m_buffers_counter_name;
    std::string m_queue_event_name;
    std::string m_dequeue_event_name;
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
              unsigned int num_exposures, HDR::InputResolution res, unsigned int buffers_count, int pixel_format,
              size_t pixel_width, unsigned int fps = 0, bool queue_buffers_on_stream_start = true,
              bool timestamp_copy = true) override;

    bool get_buffer(VideoBuffer **o_buffer) override;
    bool put_buffer(VideoBuffer *buffer) override;

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
