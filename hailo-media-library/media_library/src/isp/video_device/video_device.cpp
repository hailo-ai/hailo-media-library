#include "video_device.hpp"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/time.h>
#include <thread>
#include <chrono>
#include <compare>
#include <optional>
#include <utility>

#include "hailo_media_library_perfetto.hpp"
#include "media_library/media_library_utils.hpp"
#include "sensor_registry.hpp"
#include "media_library_types.hpp"

namespace HDR
{

/***
 * Video Device
 ***/

VideoDevice::~VideoDevice()
{
    stop_stream();
    destroy_buffers();
    close_device();
}

VideoDevice::VideoDevice(v4l2_buf_type format_type)
{
    m_initialized = false;
    m_num_exposures = 0;
    m_width = 0;
    m_height = 0;
    m_fd = -1;
    m_is_capture_dev = false;
    m_num_buffers = 0;
    m_format_type = format_type;
    m_used_buffers_count = 0;
    m_name = "";
    m_buffers_counter_name = "";
    m_queue_event_name = "";
    m_dequeue_event_name = "";
}

bool VideoDevice::open_device(const std::string &device_path)
{
    m_fd = open(device_path.c_str(), O_RDWR | O_CLOEXEC);
    return m_fd >= 0;
}

void VideoDevice::close_device()
{
    if (m_fd >= 0)
        close(m_fd);
}

bool VideoDevice::validate_cap()
{
    struct v4l2_capability v4l2_caps;
    if (m_fd < 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "Invalid file descriptor: {}", m_fd);
        return false;
    }

    memset(&v4l2_caps, 0, sizeof(v4l2_caps));

    if (ioctl(m_fd, VIDIOC_QUERYCAP, &v4l2_caps) != 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "VIDIOC_QUERYCAP failed, errno: {}", errno);
        return false;
    }

    if (v4l2_caps.device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
    {
        return get_format_type() == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    }
    else if (v4l2_caps.device_caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE)
    {
        return get_format_type() == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    }

    return false;
}

bool VideoDevice::set_format()
{
    struct v4l2_format fmt;

    if (m_fd < 0)
        return false;

    memset(&fmt, 0, sizeof(fmt));

    fmt.type = get_format_type();
    fmt.fmt.pix_mp.width = get_width();
    fmt.fmt.pix_mp.height = get_height();
    fmt.fmt.pix_mp.pixelformat = get_pix_fmt();
    fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
    fmt.fmt.pix_mp.num_planes = get_num_exposures();

    int result = ioctl(m_fd, VIDIOC_S_FMT, &fmt);
    if (result != 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "VIDIOC_S_FMT failed with error: {}", errno);
        return false;
    }
    return true;
}

bool VideoDevice::init_buffers(DMABufferAllocator &allocator, size_t plane_size, bool timestamp_copy)
{
    struct v4l2_requestbuffers req;

    if (m_num_buffers == 0)
        return false;

    memset(&req, 0, sizeof(req));

    req.count = m_num_buffers;
    req.type = get_format_type();
    req.memory = V4L2_MEMORY_DMABUF;

    if (ioctl(m_fd, VIDIOC_REQBUFS, &req) != 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: VIDIOC_REQBUFS failed", m_name);
        return false;
    }

    if (req.count != m_num_buffers)
        return false;

    HAILO_MEDIA_LIBRARY_TRACE_COUNTER(perfetto::DynamicString(m_buffers_counter_name), m_used_buffers_count.load(),
                                      VIDEO_DEV_TRACK, MEDIA_LIBRARY_CATEGORY);

    for (unsigned int index = 0; index < m_num_buffers; ++index)
    {
        VideoBuffer *buffer = new VideoBuffer();

        if (!buffer->init(allocator, get_format_type(), index, get_num_exposures(), plane_size, timestamp_copy, m_fd))
        {
            delete buffer;
            goto err_querybuf;
        }

        m_buffers.push_back(buffer);
    }

    return true;

err_querybuf:
    destroy_buffers();
    return false;
}

void VideoDevice::destroy_buffers()
{
    for (unsigned int i = 0; i < m_buffers.size(); ++i)
    {
        delete m_buffers[i];
    }
    m_buffers.clear();
}

bool VideoDevice::set_fps(unsigned int fps)
{
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = get_format_type();
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps;

    return (ioctl(m_fd, VIDIOC_S_PARM, &parm) == 0);
}

bool VideoDevice::start_stream()
{
    if (m_is_stream_on)
    {
        return true;
    }
    enum v4l2_buf_type type = get_format_type();

    // return (ioctl(m_fd, VIDIOC_STREAMON, &type) == 0);
    if (ioctl(m_fd, VIDIOC_STREAMON, &type) != 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: VIDIOC_STREAMON failed", m_name);
        return false;
    }
    LOGGER__MODULE__DEBUG(LOGGER_TYPE, "{}: VIDIOC_STREAMON successful", m_name);
    m_is_stream_on = true;
    return true;
}

bool VideoDevice::stop_stream()
{
    if (!m_is_stream_on)
    {
        return true;
    }
    static constexpr std::chrono::seconds TIMEOUT_WAIT_FOR_BUFFERS(2);
    std::chrono::steady_clock::time_point wait_deadline = std::chrono::steady_clock::now() + TIMEOUT_WAIT_FOR_BUFFERS;
    while (m_used_buffers_count != 0 && std::chrono::steady_clock::now() < wait_deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (m_used_buffers_count != 0)
    {
        LOGGER__MODULE__WARNING(LOGGER_TYPE,
                                "{}: stop_stream called with {} buffers still in use after waiting for {} seconds",
                                m_name, m_used_buffers_count.load(), TIMEOUT_WAIT_FOR_BUFFERS.count());
    }
    enum v4l2_buf_type type = get_format_type();
    if (!m_initialized)
        return false;

    // Flip the flag before issuing STREAMOFF so any concurrent get_buffer()
    // whose VIDIOC_DQBUF unblocks during the ioctl sees the device as
    // already-stopped and logs the expected dequeue failure at debug level.
    m_is_stream_on = false;
    if (ioctl(m_fd, VIDIOC_STREAMOFF, &type) != 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: VIDIOC_STREAMOFF failed", m_name);
        return false;
    }
    LOGGER__MODULE__DEBUG(LOGGER_TYPE, "{}: VIDIOC_STREAMOFF successful", m_name);
    return true;
}

bool VideoDevice::init(const std::string &device_path, const std::string name, DMABufferAllocator &allocator,
                       unsigned int num_exposures, Resolution res, unsigned int buffers_count, int pixel_format,
                       size_t pixel_width, unsigned int fps, bool queue_buffers_on_stream_start, bool timestamp_copy)
{
    if (m_initialized)
    {
        return true;
    }

    m_name = std::move(name);
    m_buffers_counter_name = m_name + " buffers";
    m_queue_event_name = m_name + " queue";
    m_dequeue_event_name = m_name + " dequeue";

    if (num_exposures == 0 || num_exposures > HDR_DOL_3)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: invalid DOL ({})", m_name, num_exposures);
        return false;
    }

    m_num_exposures = num_exposures;

    auto &registry = SensorRegistry::get_instance();
    auto resolution_info = registry.get_resolution_info(res);
    if (!resolution_info)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: invalid resolution", m_name);
        return false;
    }

    m_width = resolution_info->width;
    m_height = resolution_info->height;

    m_num_buffers = buffers_count;
    size_t plane_size_pixels = get_width() * get_height();
    size_t plane_size = plane_size_pixels * pixel_width / CHAR_BIT;

    if (!open_device(device_path))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: unable to open video device!", m_name);
        return false;
    }

    if (!validate_cap())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: unable to validate capabilities!", m_name);
        goto err_validateCap;
    }

    m_pixelformat = pixel_format;

    if (!set_format())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: unable to set format!", m_name);
        goto err_setFormat;
    }

    if (fps != 0)
    {
        if (!set_fps(fps))
        {
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: unable to set fps", m_name, fps);
            goto err_setFps;
        }
    }

    if (!init_buffers(allocator, plane_size, timestamp_copy))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: unable to init buffers!", m_name);
        goto err_initBuffers;
    }

    if (queue_buffers_on_stream_start)
    {
        if (!queue_buffers())
        {
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: unable to queue buffers!", m_name);
            goto err_queueBuffers;
        }
    }

    m_initialized = true;
    return true;
err_queueBuffers:
    destroy_buffers();
err_initBuffers:
err_setFps:
err_setFormat:
err_validateCap:
    close_device();
    return false;
}

bool VideoDevice::dequeue_buffers()
{
    VideoBuffer *buf = nullptr;
    struct epoll_event ev;
    struct epoll_event events[1];
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd == -1)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: Failed to create epoll instance {}", m_name, errno);
        return false;
    }

    ev.events = EPOLLIN;
    ev.data.fd = m_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, m_fd, &ev) == -1)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: Failed to add fd to epoll {}", m_name, errno);
        close(epoll_fd);
        return false;
    }

    while (true)
    {
        // Wait for events (timeout set to 1ms)
        int nfds = epoll_wait(epoll_fd, events, 1, 1);

        if (nfds == 0)
        {
            // Timeout occurred
            close(epoll_fd);
            return true;
        }
        else if (nfds == -1)
        {
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: epoll_wait failed {}", m_name, errno);
            close(epoll_fd);
            return false;
        }

        // Handle the event on m_fd
        if (events[0].data.fd == m_fd)
        {
            if (!get_buffer(&buf, false))
            {
                close(epoll_fd);
                return false;
            }
        }
    }

    close(epoll_fd);
    return false;
}
bool VideoDevice::queue_buffers()
{
    for (unsigned int index = 0; index < m_buffers.size(); ++index)
    {
        if (!put_buffer(m_buffers[index], false))
        {
            return false;
        }
    }
    return true;
}

bool VideoDevice::put_buffer(VideoBuffer *buffer, bool sub_from_used_count)
{
    int ioctl_ret;

    if (!buffer)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: put_buffer called with null buffer", m_name);
        return false;
    }

    HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN(perfetto::DynamicString(m_queue_event_name), VIDEO_DEV_THREADED_TRACK,
                                          MEDIA_LIBRARY_CATEGORY);
    ioctl_ret = ioctl(m_fd, VIDIOC_QBUF, buffer->get_v4l2_buffer());
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(VIDEO_DEV_THREADED_TRACK, MEDIA_LIBRARY_CATEGORY);

    if (ioctl_ret != 0)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: VIDIOC_QBUF failed, err {}, ioctl_ret {}, failed fd {}", m_name, errno,
                              ioctl_ret, buffer->get_v4l2_buffer()->m.planes[0].m.fd);
        return false;
    }

    if (sub_from_used_count)
    {
        auto used_buffers_count = --m_used_buffers_count;
        (void)used_buffers_count; // to avoid unused variable warning in case of disabled perfetto
        HAILO_MEDIA_LIBRARY_TRACE_COUNTER(perfetto::DynamicString(m_buffers_counter_name), used_buffers_count,
                                          VIDEO_DEV_TRACK, MEDIA_LIBRARY_CATEGORY);
    }
    return true;
}

bool VideoDevice::get_buffer(VideoBuffer **o_buffer, bool add_to_used_count)
{
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VideoBuffer::MAX_NUM_OF_PLANES];
    int ioctl_ret;

    if (!o_buffer)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: get_buffer called with null output buffer", m_name);
        return false;
    }

    if (!m_initialized)
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: get_buffer called on uninitialized device", m_name);
        return false;
    }

    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));

    buf.type = get_format_type();
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.length = get_num_exposures();
    buf.m.planes = planes;

    HAILO_MEDIA_LIBRARY_TRACE_EVENT_BEGIN(perfetto::DynamicString(m_dequeue_event_name), VIDEO_DEV_THREADED_TRACK,
                                          MEDIA_LIBRARY_CATEGORY);
    ioctl_ret = ioctl(m_fd, VIDIOC_DQBUF, &buf);
    if (ioctl_ret != 0)
    {
        HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(VIDEO_DEV_THREADED_TRACK, MEDIA_LIBRARY_CATEGORY);
        if (m_is_stream_on)
        {
            LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: VIDIOC_DQBUF failed, err {}, ioctl_ret {}", m_name, errno,
                                  ioctl_ret);
        }
        else
        {
            LOGGER__MODULE__DEBUG(LOGGER_TYPE, "{}: VIDIOC_DQBUF returned err {} (expected after stop_stream)", m_name,
                                  errno);
        }
        return false;
    }
    HAILO_MEDIA_LIBRARY_TRACE_EVENT_END(VIDEO_DEV_THREADED_TRACK, MEDIA_LIBRARY_CATEGORY, "isp_timestamp_ms",
                                        media_library_timeval_to_ms(buf.timestamp));

    if (buf.index >= m_buffers.size())
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: dequeued buffer index ({}) out of range (max {})", m_name, buf.index,
                              m_buffers.size());
        return false;
    }

    *o_buffer = m_buffers[buf.index];
    (*o_buffer)->get_v4l2_buffer()->timestamp = buf.timestamp;
    if (add_to_used_count)
    {
        auto used_buffers_count = ++m_used_buffers_count;
        (void)used_buffers_count; // to avoid unused variable warning in case of disabled perfetto
        HAILO_MEDIA_LIBRARY_TRACE_COUNTER(perfetto::DynamicString(m_buffers_counter_name), used_buffers_count,
                                          VIDEO_DEV_TRACK, MEDIA_LIBRARY_CATEGORY);
    }
    return true;
}

VideoCaptureDevice::VideoCaptureDevice() : VideoDevice(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
{
}

VideoOutputDevice::VideoOutputDevice() : VideoDevice(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
{
    m_all_buffers_used = false;
}

bool VideoOutputDevice::init(const std::string &device_path, const std::string name, DMABufferAllocator &allocator,
                             unsigned int num_exposures, Resolution res, unsigned int buffers_count, int pixel_format,
                             size_t pixel_width, unsigned int fps, bool, bool timestamp_copy)
{
    if (!VideoDevice::init(device_path, std::move(name), allocator, num_exposures, res, buffers_count, pixel_format,
                           pixel_width, fps, false, timestamp_copy))
        return false;

    for (unsigned int index = 0; index < m_buffers.size(); ++index)
    {
        m_buffer_free.push_back(true);
    }

    return true;
}

int VideoOutputDevice::find_first_free_buffer()
{
    for (unsigned int index = 0; index < m_buffer_free.size(); ++index)
    {
        if (m_buffer_free[index])
            return index;
    }

    return -1;
}

bool VideoOutputDevice::mark_buffer_used(unsigned int index)
{
    if (index >= m_buffer_free.size())
        return false;

    m_buffer_free[index] = false;
    m_used_buffers_count.fetch_add(1);
    return true;
}

bool VideoOutputDevice::get_buffer(VideoBuffer **o_buffer, bool add_to_used_count)
{

    if (!m_all_buffers_used)
    {
        int index = find_first_free_buffer();
        if (index < 0)
        {
            m_all_buffers_used = true;
        }
        else
        {
            mark_buffer_used(index);
            *o_buffer = m_buffers[index];
            return true;
        }
    }

    if (!VideoDevice::get_buffer(o_buffer, add_to_used_count))
        return false;

    return true;
}

bool VideoOutputDevice::put_buffer(VideoBuffer *buffer, bool sub_from_used_count)
{
    if (!VideoDevice::put_buffer(buffer, sub_from_used_count))
    {
        LOGGER__MODULE__ERROR(LOGGER_TYPE, "{}: VideoOutputDevice put_buffer failed", m_name);
        return false;
    }

    return true;
}

} // namespace HDR
