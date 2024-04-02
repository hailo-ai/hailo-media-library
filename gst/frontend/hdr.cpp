#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h> /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <linux/v4l2-subdev.h>
#include "hdr.hpp"
#define MAX_NUM_OF_PLANES (3)

static volatile int async_finished = 0;
static volatile int hdr_ready = 0;
static volatile int hdr_finished = 0;
struct buffer
{
	int num_planes;
	int sizes[MAX_NUM_OF_PLANES];
	void *planes[MAX_NUM_OF_PLANES];
	struct v4l2_buffer v4l2_buf;
	int free;
};

struct buffer *buffers[2];
static unsigned int n_buffers[2];

static int xioctl(int fh, uint32_t request, void *arg)
{
	int r;

	do
	{
		r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

static int open_device(char *dev_name)
{
	struct stat st;
	static int fd = 0;
	int mode = O_RDWR;
	if (-1 == stat(dev_name, &st))
	{
		fprintf(stderr, "Cannot identify '%s': %d, %s\n", dev_name,
				errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!S_ISCHR(st.st_mode))
	{
		fprintf(stderr, "%s is no device\n", dev_name);
		exit(EXIT_FAILURE);
	}

	mode |= O_NONBLOCK;
	fd = open(dev_name, mode /* required */, 0);

	if (-1 == fd)
	{
		fprintf(stderr, "Cannot open '%s': %d, %s\n", dev_name, errno,
				strerror(errno));
		exit(EXIT_FAILURE);
	}
	return fd;
}

int set_format(int fd, int capture, int width, int height, int pix_fmt, int num_planes)
{
	struct v4l2_format fmt;

	fmt.type = capture ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.width = width;
	fmt.fmt.pix_mp.height = height;
	fmt.fmt.pix_mp.pixelformat = pix_fmt;
	fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
	fmt.fmt.pix_mp.num_planes = num_planes;
	if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
	{
		return -EINVAL;
	}
	return 0;
}

static int set_fps(int fd, int fps)
{
	struct v4l2_streamparm parm = {0};
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = fps;

	return xioctl(fd, VIDIOC_S_PARM, &parm);
}

int start_stream(int fd, int index)
{
	enum v4l2_buf_type type;

	type = index == 0 ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
		return -1;

	return 0;
}

static void stop_stream(int fd, int index)
{
	enum v4l2_buf_type type;

	type = index == 0 ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
		return;
}

static int queue_buffer(int fd, int fd_index, int index)
{
	if (index < 0 || (uint32_t)index >= n_buffers[fd_index])
	{
		return -EINVAL;
	}

	return xioctl(fd, VIDIOC_QBUF, &buffers[fd_index][index].v4l2_buf);
}

static int queue_buffers(int fd, int index)
{
	uint32_t i = 0;
	int ret;
	for (i = 0; i < n_buffers[index]; ++i)
	{
		ret = queue_buffer(fd, index, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int init_buffers(int fd, int index, int num_planes)
{
	struct v4l2_requestbuffers req;
	int type;
	memset(&req, 0, sizeof(struct v4l2_requestbuffers));

	req.count = 10;
	type = index == 0 ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req.type = type;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req))
	{
		return -EINVAL;
	}
	if (req.count < 10)
	{
		return -ENOMEM;
	}

	buffers[index] = (buffer *)calloc(req.count, sizeof(*buffers[index]));

	if (!buffers[index])
	{
		printf("cant allocate memory!\n");
		return -ENOMEM;
	}

	for (n_buffers[index] = 0; n_buffers[index] < req.count; ++n_buffers[index])
	{
		struct v4l2_buffer buf;
		uint32_t plane;

		memset(&buf, 0, sizeof(struct v4l2_buffer));

		buf.type = type;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers[index];
		buf.length = num_planes;
		buf.m.planes = (v4l2_plane *)malloc(num_planes *
											sizeof(struct v4l2_plane));
		memset(buf.m.planes, 0,
			   num_planes * sizeof(struct v4l2_plane));

		if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
		{
			printf("querybuf failed!\n");
			return errno;
		}

		buffers[index][n_buffers[index]].num_planes = buf.length;
		for (plane = 0; plane < buf.length; ++plane)
		{
			buffers[index][n_buffers[index]].sizes[plane] =
				buf.m.planes[plane].length;
			buffers[index][n_buffers[index]].planes[plane] =
				mmap(NULL /* start anywhere */,
					 buf.m.planes[plane].length,
					 PROT_READ | PROT_WRITE /* required */,
					 MAP_SHARED /* recommended */, fd,
					 buf.m.planes[plane].m.mem_offset);

			if (MAP_FAILED == buffers[index][n_buffers[index]].planes[plane])
			{
				printf("mmap failed!\n");
				return -ENOMEM;
			}
		}
		buffers[index][n_buffers[index]].free = 1;
		memcpy(&buffers[index][n_buffers[index]].v4l2_buf, &buf,
			   sizeof(struct v4l2_buffer));
	}
	return 0;
}
#if 0
static void free_buffers(int index)
{
	int frame;
	int plane;
	for (frame = 0; frame < n_buffers[index]; ++frame) {
		free(buffers[index][frame].v4l2_buf.m.planes);
		for (plane = 0; plane < buffers[index][frame].v4l2_buf.length; ++plane)
			munmap(buffers[index][frame].planes[plane],
			       buffers[index][frame].sizes[plane]);
	}

	free(buffers[index]);
}
#endif

static int read_frame(int fd, int index, int num_planes)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[MAX_NUM_OF_PLANES];
	memset(&buf, 0, sizeof(struct v4l2_buffer));

	buf.type = index == 0 ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.length = num_planes;
	buf.m.planes = planes;
	memset(buf.m.planes, 0, MAX_NUM_OF_PLANES * sizeof(struct v4l2_plane));
	if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
	{
		return -1;
	}

	if (buf.index >= n_buffers[index])
	{
		return -1;
	}

	return buf.index;
}

static int get_empty_frame(int fd, int index)
{
	uint32_t i;
	for (i = 0; i < n_buffers[index]; ++i)
	{
		if (buffers[index][i].free)
		{
			return i;
		}
	}

	return -1;
}

static void *loop_video3(void *foo)
{
	int fd = *(int *)foo;
	int index;
	while (1)
	{
		index = read_frame(fd, 1, 1);
		if (index < 0)
			continue;
		buffers[1][index].free = 1;
	}
	return 0;
}
static void *loop_video2(void *foo)
{
	int fd = *(int *)foo;
	int index;
	while (1)
	{
		index = get_empty_frame(fd, 0);
		if (index < 0)
			continue;
		if (!queue_buffer(fd, 0, index))
			buffers[0][index].free = 0;
	}
	return 0;
}

void hdr_loop(int fd_video2, int fd_video3, HailortAsyncStitching *stitcher)
{
	int index2 = 0;
	int index3;

	while (!hdr_ready)
	{
		continue;
	}

	while (index2 >= 0)
	{
		index2 = read_frame(fd_video2, 0, 3);
		buffers[0][index2].free = 1;
	}

	pthread_t id2, id3;
	pthread_create(&id3, NULL, loop_video3, &fd_video3);
	pthread_create(&id2, NULL, loop_video2, &fd_video2);

	while (!hdr_finished)
	{
		index2 = read_frame(fd_video2, 0, 3);
		index3 = get_empty_frame(fd_video3, 1);
		if (index2 == -1 || index3 == -1)
			continue;
		stitcher->process(buffers[0][index2].planes, buffers[1][index3].planes[0]);
		while (!async_finished)
		{
			continue;
		}
		queue_buffer(fd_video3, 1, index3);
		queue_buffer(fd_video2, 0, index2);
		buffers[0][index2].free = 1;
		buffers[1][index3].free = 0;
		async_finished = 0;
	}
}

void hdr_async_callback(void *output_buffer)
{
	async_finished = 1;
}

void hdr_start_loop()
{
	hdr_ready = 1;
}

void hdr_stop_loop()
{
	hdr_finished = 1;
	// hdr_async_callback(NULL);
}

int hdr_init(hdr_hailort_params_t params, hdr_params_t *hdr_params)
{
	char dev2[] = "/dev/video2";
	char dev3[] = "/dev/video3";
	static int fd_video2 = 0, fd_video3 = 0;
	int ret;
	ret = -EINVAL;
	printf("Starting Hailo15 mcm manager\n");
	HailortAsyncStitching *stitcher = new HailortAsyncStitching(hdr_async_callback);
	stitcher->init(params);

	fd_video2 = open_device(dev2);
	if (fd_video2 < 0)
	{
		printf("unable to open video device 2\n");
		return -1;
	}

	fd_video3 = open_device(dev3);
	if (fd_video3 < 0)
	{
		printf("unable to open video device 3\n");
		return -1;
	}

	ret = set_format(fd_video2, 1, 1920, 1080, V4L2_PIX_FMT_SRGGB12, 3);
	if (ret)
	{
		printf("unable to set format video2\n");
		return -1;
	}

	ret = set_fps(fd_video2, 30);
	if (ret)
	{
		printf("unable to set fps for video2\n");
		return -1;
	}

	ret = set_format(fd_video3, 0, 1920, 1080, V4L2_PIX_FMT_SRGGB12, 1);
	if (ret)
	{
		printf("unable to set format video3\n");
		return -1;
	}

	ret = init_buffers(fd_video2, 0, 3);
	if (ret)
	{
		printf("unable to init buffers video2\n");
		return -1;
	}

	ret = init_buffers(fd_video3, 1, 1);
	if (ret)
	{
		printf("unable to init buffers video3\n");
		return -1;
	}

	ret = queue_buffers(fd_video2, 0);
	if (ret)
	{
		printf("unable to queue buffers video2\n");
		return -1;
	}

	ret = start_stream(fd_video2, 0);
	if (ret)
	{
		printf("unable to start stream video2\n");
		return -1;
	}
	ret = start_stream(fd_video3, 1);
	if (ret)
	{
		printf("unable to start stream video3\n");
		return -1;
	}

	hdr_params->fd_video2 = fd_video2;
	hdr_params->fd_video3 = fd_video3;
	hdr_params->stitcher = stitcher;

	return ret;
}

void hdr_finish(int fd_video2, int fd_video3, HailortAsyncStitching *stitcher)
{
	stop_stream(fd_video2, 0);
	stop_stream(fd_video3, 1);
	close(fd_video2);
	close(fd_video3);
	// delete stitcher;
}
