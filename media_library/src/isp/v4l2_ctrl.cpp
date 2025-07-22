#include "v4l2_ctrl.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <filesystem>
#include <fstream>
#include <type_traits>

#include "media_library_logger.hpp"

#define MODULE_NAME LoggerType::Isp
#define IMX_BASE_ADDRESS (V4L2_CID_USER_BASE + 0x2000)

namespace v4l2
{
constexpr size_t MAX_IOCTL_TRIES = 3;

const std::unordered_map<Video0Ctrl, std::pair<std::string, uint32_t>> m_video0_ctrl_to_key = {
    {Video0Ctrl::POWERLINE_FREQUENCY, {"isp_ae_flicker_period", 0}},
    {Video0Ctrl::SHARPNESS_DOWN, {"isp_ee_y_gain_down", 0}},
    {Video0Ctrl::SHARPNESS_UP, {"isp_ee_y_gain_up", 0}},
    {Video0Ctrl::BRIGHTNESS, {"isp_cproc_brightness", 0}},
    {Video0Ctrl::CONTRAST, {"isp_cproc_contrast", 0}},
    {Video0Ctrl::SATURATION, {"isp_cproc_saturation", 0}},
    {Video0Ctrl::EE_ENABLE, {"isp_ee_enable", 0}},
    {Video0Ctrl::AE_ENABLE, {"isp_ae_enable", 0}},
    {Video0Ctrl::AE_GAIN, {"isp_ae_gain", 0}},
    {Video0Ctrl::AE_INTEGRATION_TIME, {"isp_ae_integration_time", 0}},
    {Video0Ctrl::WDR_CONTRAST, {"isp_wdr_contrast", 0}},
    {Video0Ctrl::AWB_MODE, {"isp_awb_mode", 0}},
    {Video0Ctrl::AWB_ILLUM_INDEX, {"isp_awb_illum_index", 0}},
    {Video0Ctrl::WB_R_GAIN, {"isp_wb_r_gain", 0}},
    {Video0Ctrl::WB_GR_GAIN, {"isp_wb_gr_gain", 0}},
    {Video0Ctrl::WB_GB_GAIN, {"isp_wb_gb_gain", 0}},
    {Video0Ctrl::WB_B_GAIN, {"isp_wb_b_gain", 0}},
    {Video0Ctrl::HDR_RATIOS, {"isp_hdr_ratio", 0}},
    {Video0Ctrl::BLS_RED, {"isp_bls_red", 0}},
    {Video0Ctrl::BLS_GREEN_RED, {"isp_bls_green_red", 0}},
    {Video0Ctrl::BLS_GREEN_BLUE, {"isp_bls_green_blue", 0}},
    {Video0Ctrl::BLS_BLUE, {"isp_bls_blue", 0}},
    {Video0Ctrl::DG_ENABLE, {"isp_dg_enable", 0}},
    {Video0Ctrl::DG_GAIN, {"isp_dg_gain", 0}},
    {Video0Ctrl::HDR_FORWARD_TIMESTAMPS, {"timestamp_mode", _IOW('D', BASE_VIDIOC_PRIVATE + 5, bool)}},
};

const std::unordered_map<ImxCtrl, std::pair<std::string, uint32_t>> m_imx_ctrl_to_key = {
    {ImxCtrl::IMX_WDR, {"Wide Dynamic Range", 0}},
    {ImxCtrl::SHUTTER_TIMING_LONG, {"shutter_timing_long", 0}},
    {ImxCtrl::SHUTTER_TIMING_SHORT, {"shutter_timing_short", 0}},
    {ImxCtrl::SHUTTER_TIMING_VERY_SHORT, {"shutter_timing_very_short", 0}},
    {ImxCtrl::READOUT_TIMING_SHORT, {"readout_timing_short", 0}},
    {ImxCtrl::READOUT_TIMING_VERY_SHORT, {"readout_timing_very_short", 0}},
    {ImxCtrl::VERTICAL_SPAN, {"vertical_span", 0}},
    {ImxCtrl::HORIZONTAL_SPAN, {"horizontal_span", 0}},
    // {ImxCtrl::VERTICAL_SPAN, {"vertical_span", _IOWR('I', IMX_BASE_ADDRESS + 10, uint32_t)}},
    // {ImxCtrl::HORIZONTAL_SPAN, {"horizontal_span", _IOWR('I', IMX_BASE_ADDRESS + 11, uint32_t)}},
};

const std::unordered_map<CsiCtrl, std::pair<std::string, uint32_t>> m_csi_ctrl_to_key = {
    {CsiCtrl::CSI_MODE_SEL, {"mode_sel", 0}},
};

const std::unordered_map<IspCtrl, uint32_t> m_isp_ctrl_to_key = {
    {IspCtrl::MCM_MODE_SEL, _IOWR('I', BASE_VIDIOC_PRIVATE + 10, uint32_t)},
};

bool xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    int tries = MAX_IOCTL_TRIES;
    do
    {
        r = ioctl(fd, request, arg);

        if (r == -1)
        {
            LOGGER__MODULE__WARN(MODULE_NAME, "ioctl failed: {}", strerror(errno));
        }
    } while (--tries > 0 && r == -1 && EINTR == errno);

    return r == 0;
}

std::optional<std::string> find_subdevice_path(const std::string &subdevice_name)
{
    for (const auto &entry : std::filesystem::directory_iterator("/sys/class/video4linux/"))
    {
        if (entry.path().filename().string().find("v4l-subdev") != std::string::npos)
        {
            std::ifstream name_file(entry.path() / "name");
            std::string name;
            name_file >> name;
            if (name.find(subdevice_name) != std::string::npos)
            {
                return "/dev/" + entry.path().filename().string();
            }
        }
    }

    LOGGER__MODULE__WARN(MODULE_NAME, "Subdevice {} not found", subdevice_name);
    return std::nullopt;
}

std::optional<std::filesystem::path> device_to_path(Device device)
{
    switch (device)
    {
    case Device::VIDEO0: {
        return "/dev/video0";
    }
    case Device::IMX: {
        return find_subdevice_path("imx");
    }
    case Device::CSI: {
        return find_subdevice_path("csi");
    }
    case Device::ISP: {
        return find_subdevice_path("hailo-isp");
    }
    default:
        return std::nullopt;
    }
}

std::optional<uint32_t> get_ctrl_id(int fd, const std::string &ctrl_name)
{
    uint id = 0;
    const unsigned next_flag = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    struct v4l2_query_ext_ctrl qctrl;
    ioctl_clear(qctrl);
    qctrl.id = next_flag;
    while (true)
    {
        int ret = ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &qctrl);
        if (ret < 0)
        {
            printf("ret: %d, bad ctrl: %s fd: %d qctrl id: %u errno: %s\n", ret, ctrl_name.c_str(), fd, qctrl.id,
                   strerror(errno));
            return std::nullopt;
        }
        if (0 == strcmp(qctrl.name, ctrl_name.c_str()))
        {
            id = qctrl.id;
            break;
        }

        qctrl.id |= next_flag;
    }

    return id;
}

std::optional<uint32_t> get_ctrl_id(int fd, Video0Ctrl ctrl)
{
    std::pair<std::string, uint32_t> ctrl_data = m_video0_ctrl_to_key.at(ctrl);
    if (ctrl_data.second != 0)
    {
        return ctrl_data.second;
    }

    std::string ctrl_name = ctrl_data.first;
    return get_ctrl_id(fd, ctrl_name);
}

std::optional<uint32_t> get_ctrl_id(int fd, ImxCtrl ctrl)
{
    std::pair<std::string, uint32_t> ctrl_data = m_imx_ctrl_to_key.at(ctrl);
    if (ctrl_data.second != 0)
    {
        return ctrl_data.second;
    }
    std::string ctrl_name = ctrl_data.first;
    std::optional<uint32_t> id = get_ctrl_id(fd, ctrl_name);
    return id;
}

std::optional<uint32_t> get_ctrl_id(int fd, CsiCtrl ctrl)
{
    std::pair<std::string, uint32_t> ctrl_data = m_csi_ctrl_to_key.at(ctrl);
    if (ctrl_data.second != 0)
    {
        return ctrl_data.second;
    }

    std::string ctrl_name = ctrl_data.first;
    return get_ctrl_id(fd, ctrl_name);
}

std::optional<uint32_t> get_ctrl_id(int, IspCtrl ctrl)
{
    return m_isp_ctrl_to_key.at(ctrl);
}

std::optional<FdWithDtor> get_device_fd(Device device)
{
    auto device_path = device_to_path(device);
    if (!device_path.has_value())
    {
        return std::nullopt;
    }
    int fd = open(device_path->c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd == -1)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to open device {}", static_cast<int>(device));
        return std::nullopt;
    }
    return std::make_optional<FdWithDtor>(std::make_shared<fd_with_dtor_t>(fd));
}
} // namespace v4l2
