#include "v4l2_ctrl.hpp"

#define IOCTL_TRIES_COUNT 3
#define IOCTL_CLEAR(x) memset(&(x), 0, sizeof(x))

template bool isp_utils::ctrl::v4l2Control::v4l2_ctrl_set<unsigned short>(isp_utils::ctrl::v4l2_ctrl_id,
                                                                          unsigned short);
template bool isp_utils::ctrl::v4l2Control::v4l2_ctrl_set<short>(isp_utils::ctrl::v4l2_ctrl_id, short);
template bool isp_utils::ctrl::v4l2Control::v4l2_ctrl_set<int>(isp_utils::ctrl::v4l2_ctrl_id, int);
template bool isp_utils::ctrl::v4l2Control::v4l2_ext_ctrl_set<unsigned short>(isp_utils::ctrl::v4l2_ctrl_id,
                                                                              unsigned short);
template bool isp_utils::ctrl::v4l2Control::v4l2_ext_ctrl_set<unsigned int>(isp_utils::ctrl::v4l2_ctrl_id,
                                                                            unsigned int);
template bool isp_utils::ctrl::v4l2Control::v4l2_ext_ctrl_set<short>(isp_utils::ctrl::v4l2_ctrl_id, short);
template bool isp_utils::ctrl::v4l2Control::v4l2_ext_ctrl_set<int>(isp_utils::ctrl::v4l2_ctrl_id, int);
template bool isp_utils::ctrl::v4l2Control::v4l2_ext_ctrl_set<bool>(isp_utils::ctrl::v4l2_ctrl_id, bool);
template bool isp_utils::ctrl::v4l2Control::v4l2_ext_ctrl_set_array<int *>(isp_utils::ctrl::v4l2_ctrl_id, int *);
template bool isp_utils::ctrl::v4l2Control::v4l2_ext_ctrl_set2<unsigned short>(isp_utils::ctrl::v4l2_ctrl_id,
                                                                               unsigned short &);
template bool isp_utils::ctrl::v4l2Control::v4l2_ext_ctrl_set2<int>(isp_utils::ctrl::v4l2_ctrl_id, int &);
template bool isp_utils::ctrl::v4l2Control::v4l2_ext_ctrl_set2<bool>(isp_utils::ctrl::v4l2_ctrl_id, bool &);
template bool isp_utils::ctrl::v4l2Control::v4l2_ctrl_get<unsigned short>(isp_utils::ctrl::v4l2_ctrl_id,
                                                                          unsigned short &);
template bool isp_utils::ctrl::v4l2Control::v4l2_ctrl_get<unsigned int>(isp_utils::ctrl::v4l2_ctrl_id, unsigned int &);
template bool isp_utils::ctrl::v4l2Control::v4l2_ctrl_get<int>(isp_utils::ctrl::v4l2_ctrl_id, int &);
template bool isp_utils::ctrl::v4l2Control::v4l2_ctrl_get<short>(isp_utils::ctrl::v4l2_ctrl_id, short &);
template bool isp_utils::ctrl::v4l2Control::v4l2_ext_ctrl_get<unsigned int>(isp_utils::ctrl::v4l2_ctrl_id,
                                                                            unsigned int &);
template bool isp_utils::ctrl::v4l2Control::v4l2_ext_ctrl_get<unsigned short>(isp_utils::ctrl::v4l2_ctrl_id,
                                                                              unsigned short &);

std::unordered_map<isp_utils::ctrl::v4l2_ctrl_id, std::string> isp_utils::ctrl::v4l2Control::m_ctrl_id_to_name = {
    {V4L2_CTRL_POWERLINE_FREQUENCY, "isp_ae_flicker_period"},
    {V4L2_CTRL_SHARPNESS_DOWN, "isp_ee_y_gain_down"},
    {V4L2_CTRL_SHARPNESS_UP, "isp_ee_y_gain_up"},
    {V4L2_CTRL_BRIGHTNESS, "isp_cproc_brightness"},
    {V4L2_CTRL_CONTRAST, "isp_cproc_contrast"},
    {V4L2_CTRL_SATURATION, "isp_cproc_saturation"},
    {V4L2_CTRL_EE_ENABLE, "isp_ee_enable"},
    {V4L2_CTRL_AE_ENABLE, "isp_ae_enable"},
    {V4L2_CTRL_AE_GAIN, "isp_ae_gain"},
    {V4L2_CTRL_AE_INTEGRATION_TIME, "isp_ae_integration_time"},
    {V4L2_CTRL_WDR_CONTRAST, "isp_wdr_contrast"},
    {V4L2_CTRL_AWB_MODE, "isp_awb_mode"},
    {V4L2_CTRL_AWB_ILLUM_INDEX, "isp_awb_illum_index"},
    {V4l2_CTRL_CSI_MODE_SEL, "mode_sel"},
    {V4l2_CTRL_IMX_WDR, "Wide Dynamic Range"},
    {V4l2_CTRL_SET_HDR_RATIOS, "isp_hdr_ratio"}};

namespace isp_utils
{
namespace ctrl
{
uint32_t v4l2Control::get_id(v4l2_ctrl_id id)
{
    if (m_ctrl_id_to_id.find(id) == m_ctrl_id_to_id.end())
    {
        m_ctrl_id_to_id[id] = v4l2_get_ctrl_id(m_ctrl_id_to_name[id]);
    }
    return m_ctrl_id_to_id[id];
}

int v4l2Control::xioctl(unsigned long request, void *arg)
{
    int r;
    int tries = IOCTL_TRIES_COUNT;

    do
    {
        r = ioctl(m_fd, request, arg);

        if (r == -1)
        {
            std::cout << "ioctl failed: " << strerror(errno) << std::endl;
        }
    } while (--tries > 0 && r == -1 && EINTR == errno);

    return r;
}

uint v4l2Control::v4l2_get_ctrl_id(const std::string &v4l2_ctrl_name)
{
    int ret = 0;
    uint id = 0;
    const unsigned next_flag = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    struct v4l2_query_ext_ctrl qctrl;
    IOCTL_CLEAR(qctrl);
    qctrl.id = next_flag;
    while (true)
    {
        ret = ioctl(m_fd, VIDIOC_QUERY_EXT_CTRL, &qctrl);

        if (ret < 0)
        {
            ret = 0;
            break;
        }
        if (0 == strcmp(qctrl.name, v4l2_ctrl_name.c_str()))
        {
            id = qctrl.id;
            break;
        }

        qctrl.id |= next_flag;
    }

    return id;
}

v4l2Control::v4l2Control(std::string device) : m_device(device)
{
    m_fd = open(device.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (m_fd == -1)
    {
        std::cout << "Cannot open device " << device << std::endl;
        throw std::runtime_error("Cannot open device " + device);
    }
}

v4l2Control::~v4l2Control()
{
    close(m_fd);
}

template <typename T> bool v4l2Control::v4l2_ctrl_set(v4l2_ctrl_id id, T val)
{
    struct v4l2_control ctrl;
    IOCTL_CLEAR(ctrl);

    ctrl.id = static_cast<uint32_t>(get_id(id));
    ctrl.value = val;
    return xioctl(VIDIOC_S_CTRL, &ctrl) != -1;
}

template <typename T> bool v4l2Control::v4l2_ctrl_get(v4l2_ctrl_id id, T &val)
{
    struct v4l2_control ctrl;
    IOCTL_CLEAR(ctrl);

    ctrl.id = static_cast<uint32_t>(get_id(id));
    if (xioctl(VIDIOC_G_CTRL, &ctrl) == -1)
    {
        return false;
    }
    val = ctrl.value;

    return true;
}

template <typename T> bool v4l2Control::v4l2_ext_ctrl_set(v4l2_ctrl_id id, T val)
{
    struct v4l2_ext_control ctrl;
    struct v4l2_ext_controls ctrls;
    struct v4l2_query_ext_ctrl qctrl;
    IOCTL_CLEAR(ctrl);
    IOCTL_CLEAR(ctrls);
    IOCTL_CLEAR(qctrl);

    qctrl.id = static_cast<uint32_t>(get_id(id));
    if (xioctl(VIDIOC_QUERY_EXT_CTRL, &qctrl) == -1)
    {
        return false;
    }

    ctrl.id = qctrl.id;
    ctrl.size = qctrl.elem_size * qctrl.elems;
    ctrl.value = val;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    ctrls.which = V4L2_CTRL_ID2WHICH(ctrl.id);

    if (xioctl(VIDIOC_S_EXT_CTRLS, &ctrls) == -1)
    {
        return false;
    }

    // TODO: Bring me back
    // free(ctrl.ptr);

    return true;
}

template <typename T> bool v4l2Control::v4l2_ext_ctrl_set_array(v4l2_ctrl_id id, T val)
{
    struct v4l2_ext_control ctrl;
    struct v4l2_ext_controls ctrls;
    struct v4l2_query_ext_ctrl qctrl;
    IOCTL_CLEAR(ctrl);
    IOCTL_CLEAR(ctrls);
    IOCTL_CLEAR(qctrl);

    qctrl.id = static_cast<uint32_t>(get_id(id));
    if (xioctl(VIDIOC_QUERY_EXT_CTRL, &qctrl) == -1)
    {
        return false;
    }

    ctrl.id = qctrl.id;
    ctrl.size = qctrl.elem_size * qctrl.elems;
    ctrl.ptr = val;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    ctrls.which = V4L2_CTRL_ID2WHICH(ctrl.id);

    if (xioctl(VIDIOC_S_EXT_CTRLS, &ctrls) == -1)
    {
        return false;
    }

    // TODO: Bring me back
    // free(ctrl.ptr);

    return true;
}

template <typename T> bool v4l2Control::v4l2_ext_ctrl_set2(v4l2_ctrl_id id, T &val)
{
    struct v4l2_ext_control ctrl;
    struct v4l2_ext_controls ctrls;
    struct v4l2_query_ext_ctrl qctrl;
    IOCTL_CLEAR(ctrl);
    IOCTL_CLEAR(ctrls);
    IOCTL_CLEAR(qctrl);

    qctrl.id = static_cast<uint32_t>(get_id(id));
    if (xioctl(VIDIOC_QUERY_EXT_CTRL, &qctrl) == -1)
    {
        return false;
    }

    ctrl.id = qctrl.id;
    ctrl.size = sizeof(val);
    ctrl.ptr = &val;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    ctrls.which = V4L2_CTRL_ID2WHICH(ctrl.id);

    if (xioctl(VIDIOC_S_EXT_CTRLS, &ctrls) == -1)
    {
        return false;
    }

    // TODO: Bring me back
    // free(ctrl.ptr);

    return true;
}

template <typename T> bool v4l2Control::v4l2_ext_ctrl_get(v4l2_ctrl_id id, T &val)
{
    struct v4l2_ext_control ctrl;
    struct v4l2_ext_controls ctrls;
    struct v4l2_query_ext_ctrl qctrl;
    IOCTL_CLEAR(ctrl);
    IOCTL_CLEAR(ctrls);
    IOCTL_CLEAR(qctrl);

    qctrl.id = static_cast<uint32_t>(get_id(id));
    if (xioctl(VIDIOC_QUERY_EXT_CTRL, &qctrl) == -1)
    {
        return false;
    }
    ctrl.id = qctrl.id;
    ctrl.size = qctrl.elem_size * qctrl.elems;
    ctrl.ptr = malloc(ctrl.size);
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    ctrls.which = V4L2_CTRL_ID2WHICH(ctrl.id);

    if (xioctl(VIDIOC_G_EXT_CTRLS, &ctrls) == -1)
    {
        return false;
    }
    val = *(T *)ctrl.ptr;
    free(ctrl.ptr);
    return true;
}
} // namespace ctrl
} // namespace isp_utils
