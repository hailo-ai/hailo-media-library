#pragma once

#include <iostream>
#include <optional>
#include <fstream>
#include <span>
#include <map>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <type_traits>
#include <unistd.h>
#include <mutex>
#include <memory>
#include <vector>
#include <thread>

#include "files_utils.hpp"

#ifndef CTRL_REPOSITORY_TTL_MS
#define CTRL_REPOSITORY_TTL_MS 5000 // 5 sec
#endif

template <typename T> struct is_span : std::false_type
{
};

template <typename T, std::size_t Extent> struct is_span<std::span<T, Extent>> : std::true_type
{
};

template <typename T> inline constexpr bool is_span_v = is_span<T>::value;

namespace v4l2
{

enum class Device
{
    UNKNOWN,
    VIDEO0,
    CSI,
    IMX,
    ISP
};

enum class Video0Ctrl
{
    POWERLINE_FREQUENCY,
    NOISE_REDUCTION,
    SHARPNESS_DOWN,
    SHARPNESS_UP,
    BRIGHTNESS,
    CONTRAST,
    SATURATION,
    EE_ENABLE,

    AE_ENABLE,
    AE_GAIN,
    AE_INTEGRATION_TIME,
    AE_WDR_VALUES,

    WDR_CONTRAST,

    AWB_MODE,
    AWB_ILLUM_INDEX,

    WB_R_GAIN,
    WB_GR_GAIN,
    WB_GB_GAIN,
    WB_B_GAIN,

    HDR_RATIOS,
    HDR_FORWARD_TIMESTAMPS,

    BLS_RED,
    BLS_GREEN_RED,
    BLS_GREEN_BLUE,
    BLS_BLUE,

    DG_ENABLE,
    DG_GAIN,

    MAX
};

enum class CsiCtrl
{
    CSI_MODE_SEL,
    MAX
};

enum class ImxCtrl
{
    IMX_WDR,
    SHUTTER_TIMING_LONG,
    SHUTTER_TIMING_SHORT,
    SHUTTER_TIMING_VERY_SHORT,
    READOUT_TIMING_SHORT,
    READOUT_TIMING_VERY_SHORT,
    VERTICAL_SPAN,
    HORIZONTAL_SPAN,
    MAX
};

enum class IspCtrl
{
    MCM_MODE_SEL,
    MAX
};

template <typename T> inline void ioctl_clear(T &v4l2_ctrl)
{
    std::memset(&v4l2_ctrl, 0, sizeof(T));
}

std::optional<files_utils::SharedFd> get_device_fd(Device device, size_t sensor_index = 0);
template <class CtrlEnum> Device get_ctrl_device()
{
    static_assert(std::is_enum_v<CtrlEnum>, "CtrlEnum type is not enum");
    if constexpr (std::is_same_v<CtrlEnum, Video0Ctrl>)
    {
        return Device::VIDEO0;
    }
    else if constexpr (std::is_same_v<CtrlEnum, ImxCtrl>)
    {
        return Device::IMX;
    }
    else if constexpr (std::is_same_v<CtrlEnum, CsiCtrl>)
    {
        return Device::CSI;
    }
    else if constexpr (std::is_same_v<CtrlEnum, IspCtrl>)
    {
        return Device::ISP;
    }
    return Device::UNKNOWN;
}
std::optional<uint32_t> get_ctrl_id(int fd, Video0Ctrl ctrl);
std::optional<uint32_t> get_ctrl_id(int fd, ImxCtrl ctrl);
std::optional<uint32_t> get_ctrl_id(int fd, CsiCtrl ctrl);
std::optional<uint32_t> get_ctrl_id(int fd, IspCtrl ctrl);
template <class CtrlEnum> std::optional<uint32_t> get_ctrl_id(int fd, CtrlEnum ctrl)
{
    static_assert(std::is_enum_v<CtrlEnum>, "CtrlEnum type is not enum");
    if constexpr (std::is_same_v<CtrlEnum, Video0Ctrl>)
    {
        return get_ctrl_id(fd, (Video0Ctrl)ctrl);
    }
    else if constexpr (std::is_same_v<CtrlEnum, ImxCtrl>)
    {
        return get_ctrl_id(fd, (ImxCtrl)ctrl);
    }
    else if constexpr (std::is_same_v<CtrlEnum, CsiCtrl>)
    {
        return get_ctrl_id(fd, (CsiCtrl)ctrl);
    }
    else if constexpr (std::is_same_v<CtrlEnum, IspCtrl>)
    {
        return get_ctrl_id(fd, (IspCtrl)ctrl);
    }
    return std::nullopt;
}
bool xioctl(int fd, unsigned long request, void *arg);

class v4l2ControlManager
{
  private:
    size_t m_sensor_index;
    uint64_t m_ttl;
    bool m_async_refresh = false;
    std::mutex m_cache_mutex;
    bool m_during_ctrl_cache_refresh = false;
    std::map<Device, std::map<uint32_t, std::pair<uint64_t, uint64_t>>>
        m_ctrl_cache; // cache of Device to ctrl_id to timestamp and value
    std::map<Device, files_utils::SharedFd> m_device_fd_cache;

  public:
    v4l2ControlManager(size_t sensor_index = 0, uint64_t ttl = CTRL_REPOSITORY_TTL_MS, bool async_refresh = true)
        : m_sensor_index(sensor_index), m_ttl(ttl), m_async_refresh(async_refresh)
    {
        set_sensor_index(sensor_index);
    }

    void set_sensor_index(size_t sensor_index)
    {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        if (m_sensor_index == sensor_index)
        {
            return;
        }

        m_sensor_index = sensor_index;
        m_device_fd_cache.clear();
        m_ctrl_cache.clear();
    }

    template <class T, class CtrlEnum> bool ctrl_set(CtrlEnum id, T val)
    {
        auto device_type = get_ctrl_device<CtrlEnum>();
        if (device_type == Device::UNKNOWN)
        {
            return false;
        }
        auto shared_fd_opt = get_fd(device_type);
        if (!shared_fd_opt.has_value())
        {
            return false;
        }
        auto shared_fd = shared_fd_opt.value();
        auto ctrl_id = get_ctrl_id(*shared_fd, id);
        if (!ctrl_id.has_value())
        {
            return false;
        }

        if (!xioctl(*shared_fd, ctrl_id.value(), &val))
        {
            return false;
        }

        return true;
    }

    std::optional<files_utils::SharedFd> get_fd(Device device)
    {
        std::lock_guard<std::mutex> lock(m_cache_mutex);

        // Get the map for the current sensor_index
        auto it = m_device_fd_cache.find(device);
        if (it == m_device_fd_cache.end())
        {
            auto fd = get_device_fd(device, m_sensor_index);
            if (!fd.has_value())
            {
                return std::nullopt;
            }
            m_device_fd_cache.insert_or_assign(device, fd.value());
            return fd;
        }
        return it->second;
    }

    template <class T, class CtrlEnum> bool ext_ctrl_set(CtrlEnum id, T val)
    {
        auto device_type = get_ctrl_device<CtrlEnum>();
        if (device_type == Device::UNKNOWN)
        {
            return false;
        }
        auto shared_fd_opt = get_fd(device_type);
        if (!shared_fd_opt.has_value())
        {
            return false;
        }
        auto shared_fd = shared_fd_opt.value();
        auto ctrl_id = get_ctrl_id(*shared_fd, (CtrlEnum)id);
        if (!ctrl_id.has_value())
        {
            return false;
        }

        if constexpr (std::is_same_v<CtrlEnum, IspCtrl>) // In isp no need in ext controls
        {
            return ctrl_set(id, val);
        }

        struct v4l2_ext_control ctrl;
        struct v4l2_ext_controls ctrls;
        struct v4l2_query_ext_ctrl qctrl;
        ioctl_clear(ctrl);
        ioctl_clear(ctrls);
        ioctl_clear(qctrl);

        qctrl.id = ctrl_id.value();
        if (!xioctl(*shared_fd, VIDIOC_QUERY_EXT_CTRL, &qctrl))
        {
            return false;
        }

        if constexpr (std::is_pointer_v<T>)
        {
            ctrl.size = sizeof(*val);
            ctrl.ptr = val;
        }
        else if constexpr (is_span_v<T>)
        {
            ctrl.size = val.size() * sizeof(val.data()[0]);
            ctrl.ptr = val.data();
        }
        else
        {
            ctrl.size = sizeof(val);
            ctrl.value = val;
        }

        ctrl.id = qctrl.id;
        ctrls.count = 1;
        ctrls.controls = &ctrl;
        ctrls.which = V4L2_CTRL_ID2WHICH(ctrl.id);

        if (!xioctl(*shared_fd, VIDIOC_S_EXT_CTRLS, &ctrls))
        {
            return false;
        }

        return true;
    }

    template <class T>
    auto ext_ctrl_get(uint32_t ctrl_id, files_utils::SharedFd shared_fd)
        -> std::optional<std::conditional_t<std::is_pointer<T>::value, std::remove_pointer_t<T>, T>>
    {
        struct v4l2_ext_control ctrl;
        struct v4l2_ext_controls ctrls;
        struct v4l2_query_ext_ctrl qctrl;
        ioctl_clear(ctrl);
        ioctl_clear(ctrls);
        ioctl_clear(qctrl);

        qctrl.id = ctrl_id;
        if (!xioctl(*shared_fd, VIDIOC_QUERY_EXT_CTRL, &qctrl))
        {
            return std::nullopt;
        }

        ctrl.id = qctrl.id;
        ctrl.size = qctrl.elem_size * qctrl.elems;

        // Define val as T if T is not a pointer, and as T if type is T*
        typename std::conditional<std::is_pointer_v<T>, std::remove_pointer_t<T>, T>::type val;

        if constexpr (std::is_pointer_v<T>)
        {
            ctrl.ptr = &val;
        }
        ctrls.count = 1;
        ctrls.controls = &ctrl;
        ctrls.which = V4L2_CTRL_ID2WHICH(ctrl.id);

        if (!xioctl(*shared_fd, VIDIOC_G_EXT_CTRLS, &ctrls))
        {
            return std::nullopt;
        }
        if constexpr (std::is_pointer_v<T>)
        {
            return std::make_optional<std::remove_pointer_t<T>>(val);
        }
        else
        {
            val = ctrl.value;
            return std::make_optional<T>(val);
        }
    }

    template <class T, class CtrlEnum>
    auto ext_ctrl_get(CtrlEnum id)
        -> std::optional<std::conditional_t<std::is_pointer<T>::value, std::remove_pointer_t<T>, T>>
    {
        auto device_type = get_ctrl_device<CtrlEnum>();
        if (device_type == Device::UNKNOWN)
        {
            return std::nullopt;
        }
        auto shared_fd = get_fd(device_type);
        if (!shared_fd.has_value())
        {
            return std::nullopt;
        }
        auto ctrl_id = get_ctrl_id<CtrlEnum>(*shared_fd.value(), (CtrlEnum)id);
        if (!ctrl_id.has_value())
        {
            return std::nullopt;
        }
        return ext_ctrl_get<T>(ctrl_id.value(), shared_fd.value());
    }

    template <typename T, class CtrlEnum> bool get(CtrlEnum id, T &val, bool force_refresh = false)
    {
        static_assert(std::is_enum_v<CtrlEnum>, "CtrlEnum type is not enum");
        Device device_type = get_ctrl_device<CtrlEnum>();
        if (device_type == Device::UNKNOWN)
        {

            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            if (!m_ctrl_cache.contains(device_type))
            {

                m_ctrl_cache[device_type] = {};
            }
        }

        auto fd_opt = get_fd(device_type);
        if (!fd_opt.has_value())
        {

            return false;
        }

        auto fd = fd_opt.value();
        auto ctrl_id = get_ctrl_id<CtrlEnum>(*fd, (CtrlEnum)id);
        if (!ctrl_id.has_value())
        {

            return false;
        }

        uint32_t ctrl_id_val = ctrl_id.value();
        uint64_t current_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();

        if (m_ctrl_cache[device_type].contains(ctrl_id_val) &&
            (current_time - m_ctrl_cache[device_type][ctrl_id_val].first) < m_ttl)
        {
            val = m_ctrl_cache[device_type][ctrl_id_val].second;
            return true;
        }

        if (force_refresh || !m_ctrl_cache[device_type].contains(ctrl_id_val) || !m_async_refresh)
        {

            std::lock_guard<std::mutex> lock(m_cache_mutex);
            auto tmp_val = ext_ctrl_get<T>(ctrl_id_val, fd);
            if (!tmp_val.has_value())
            {

                return false;
            }
            m_ctrl_cache[device_type][ctrl_id_val] = {current_time, tmp_val.value()};
            m_during_ctrl_cache_refresh = false;
            return true;
        }
        else // in cache but expired -> async fetch from ioctl
        {
            val = m_ctrl_cache[device_type][ctrl_id_val].second;
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            if (!m_during_ctrl_cache_refresh)
            {
                m_during_ctrl_cache_refresh = true;

                std::thread([this, device_type, ctrl_id_val, fd, current_time, &val]() {
                    auto tmp_val = ext_ctrl_get<T>(ctrl_id_val, fd);
                    if (!tmp_val.has_value())
                    {

                        // delete existing key
                        m_ctrl_cache[device_type].erase(ctrl_id_val);
                        return;
                    }
                    m_ctrl_cache[device_type][ctrl_id_val] = {current_time, tmp_val.value()};
                    m_during_ctrl_cache_refresh = false;
                }).detach();
            }
            return true;
        }
        return false;
    }
};

} // namespace v4l2
