#include "v4l2_ctrl.hpp"

#include <iostream>

#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/unistd.h>

namespace v4l2_ctrl
{
static constexpr size_t MAX_IOCTL_TRIES = 3;

int xioctl(unsigned long request, void *arg)
{
    int r;
    int tries = MAX_IOCTL_TRIES;

    int fd = open("/dev/video0", O_RDWR | O_NONBLOCK, 0);
    if (fd == -1)
    {
        return -1;
    }

    do
    {
        r = ioctl(fd, request, arg);

        if (r == -1)
        {
            std::cout << "ioctl failed: " << strerror(errno) << std::endl;
        }
    } while (--tries > 0 && r == -1 && EINTR == errno);

    close(fd);

    return r;
}

uint get_ctrl_id(const char *v4l2_ctrl_name)
{
    int ret = 0;
    uint id = 0;
    const unsigned next_flag = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    struct v4l2_query_ext_ctrl qctrl;
    ioctl_clear(qctrl);
    qctrl.id = next_flag;
    int fd = open("/dev/video0", O_RDWR | O_NONBLOCK, 0);
    if (fd == -1)
    {
        return -1;
    }
    while (true)
    {
        ret = ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &qctrl);

        if (ret < 0)
        {
            ret = 0;
            break;
        }
        if (0 == strcmp(qctrl.name, v4l2_ctrl_name))
        {
            id = qctrl.id;
            break;
        }

        qctrl.id |= next_flag;
    }

    close(fd);

    return id;
}
} // namespace v4l2_ctrl
