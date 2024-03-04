#pragma once

#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <thread>
#include <linux/videodev2.h>
#include "common.hpp"
#include "common/isp/common.hpp"

namespace webserver
{
    namespace common
    {

        class v4l2Control
        {
        private:
            std::string m_device;

            int m_fd;
            std::unordered_map<v4l2_ctrl_id, uint> m_ctrl_id_to_id;

            void init_ctrl_id_to_id()
            {
                m_ctrl_id_to_id[V4L2_CTRL_POWERLINE_FREQUENCY] = v4l2_get_ctrl_id("isp_ae_flicker_period");
                // m_ctrl_id_to_id[V4L2_CTRL_NOISE_REDUCTION] = v4l2_get_ctrl_id("isp_fill_me_noise_reduction");
                m_ctrl_id_to_id[V4L2_CTRL_SHARPNESS_DOWN] = v4l2_get_ctrl_id("isp_ee_y_gain_down");
                m_ctrl_id_to_id[V4L2_CTRL_SHARPNESS_UP] = v4l2_get_ctrl_id("isp_ee_y_gain_up");
                m_ctrl_id_to_id[V4L2_CTRL_BRIGHTNESS] = v4l2_get_ctrl_id("isp_cproc_brightness");
                m_ctrl_id_to_id[V4L2_CTRL_CONTRAST] = v4l2_get_ctrl_id("isp_cproc_contrast");
                m_ctrl_id_to_id[V4L2_CTRL_SATURATION] = v4l2_get_ctrl_id("isp_cproc_saturation");
                m_ctrl_id_to_id[V4L2_CTRL_EE_ENABLE] = v4l2_get_ctrl_id("isp_ee_enable");
                m_ctrl_id_to_id[V4L2_CTRL_AE_ENABLE] = v4l2_get_ctrl_id("isp_ae_enable");
                m_ctrl_id_to_id[V4L2_CTRL_AE_GAIN] = v4l2_get_ctrl_id("isp_ae_gain");
                m_ctrl_id_to_id[V4L2_CTRL_AE_INTEGRATION_TIME] = v4l2_get_ctrl_id("isp_ae_integration_time");
                m_ctrl_id_to_id[V4L2_CTRL_WDR_CONTRAST] = v4l2_get_ctrl_id("isp_wdr_contrast");
                m_ctrl_id_to_id[V4L2_CTRL_AWB_MODE] = v4l2_get_ctrl_id("isp_awb_mode");
                m_ctrl_id_to_id[V4L2_CTRL_AWB_ILLUM_INDEX] = v4l2_get_ctrl_id("isp_awb_illum_index");
            }

            int xioctl(int request, void *arg)
            {
#ifndef MEDIALIB_LOCAL_SERVER
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
#else
                return 0;
#endif
            }

            uint v4l2_get_ctrl_id(const std::string &v4l2_ctrl_name)
            {
#ifndef MEDIALIB_LOCAL_SERVER
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
#else
                return 0;
#endif
            }

        public:
            v4l2Control(std::string device) : m_device(device)
            {
                m_fd = open(device.c_str(), O_RDWR | O_NONBLOCK, 0);
                if (m_fd == -1)
                {
                    std::cout << "Cannot open device " << device << std::endl;
                    throw std::runtime_error("Cannot open device " + device);
                }

                init_ctrl_id_to_id();
            }

            template <typename T>
            bool v4l2_ctrl_set(v4l2_ctrl_id id, T val)
            {
                struct v4l2_control ctrl;
                IOCTL_CLEAR(ctrl);

                ctrl.id = static_cast<uint32_t>(m_ctrl_id_to_id[id]);
                ctrl.value = val;
                return xioctl(VIDIOC_S_CTRL, &ctrl) != -1;
            }

            template <typename T>
            bool v4l2_ctrl_get(v4l2_ctrl_id id, T &val)
            {
                struct v4l2_control ctrl;
                IOCTL_CLEAR(ctrl);

                ctrl.id = static_cast<uint32_t>(m_ctrl_id_to_id[id]);
                if (xioctl(VIDIOC_G_CTRL, &ctrl) == -1)
                {
                    return false;
                }
                val = ctrl.value;

                return true;
            }

            template <typename T>
            bool v4l2_ext_ctrl_set(v4l2_ctrl_id id, T val)
            {
                struct v4l2_ext_control ctrl;
                struct v4l2_ext_controls ctrls;
                struct v4l2_query_ext_ctrl qctrl;
                IOCTL_CLEAR(ctrl);
                IOCTL_CLEAR(ctrls);
                IOCTL_CLEAR(qctrl);

                qctrl.id = static_cast<uint32_t>(m_ctrl_id_to_id[id]);
                if (xioctl(VIDIOC_QUERY_EXT_CTRL, &qctrl) == -1)
                {
                    return false;
                }

                ctrl.id = qctrl.id;
                ctrl.size = qctrl.elem_size * qctrl.elems;
                ctrl.ptr = &val;
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

            template <typename T>
            bool v4l2_ext_ctrl_set2(v4l2_ctrl_id id, T &val)
            {
                struct v4l2_ext_control ctrl;
                struct v4l2_ext_controls ctrls;
                struct v4l2_query_ext_ctrl qctrl;
                IOCTL_CLEAR(ctrl);
                IOCTL_CLEAR(ctrls);
                IOCTL_CLEAR(qctrl);

                qctrl.id = static_cast<uint32_t>(m_ctrl_id_to_id[id]);
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

            template <typename T>
            bool v4l2_ext_ctrl_get(v4l2_ctrl_id id, T &val)
            {
                struct v4l2_ext_control ctrl;
                struct v4l2_ext_controls ctrls;
                struct v4l2_query_ext_ctrl qctrl;
                IOCTL_CLEAR(ctrl);
                IOCTL_CLEAR(ctrls);
                IOCTL_CLEAR(qctrl);

                qctrl.id = static_cast<uint32_t>(m_ctrl_id_to_id[id]);
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
        };
    }
}