#include "hailo_v4l2/hailo_vsm.h"
#include "hailo_v4l2/hailo_v4l2_meta.h"
#include <gst/check/check.h>
#include <gst/check/gstcheck.h>
#include <gst/gst.h>
#include <stdio.h>
#include <string>
#include <tuple>

#define DEFAULT_VIDEO_DEVICE "/dev/video0"
#define MAX_V4L_BUFFERS 29
#define CONFIG_JSON_FILE_PATH "/home/root/apps/media_lib/resources/vision_config.json"
static uint expected_index = 0;

static GstElement *create_v4l2_pipeline(std::string video_device, std::string format, guint width, guint height, guint num_buffers)
{
    GstElement *pipeline;
    gchar *pipe_desc =
        g_strdup_printf("v4l2src device=%s io-mode=mmap num-buffers=%d ! video/x-raw, format=%s, width=%d, height=%d  ! \
                        queue max-size-buffers=5 name=queue ! \
                        hailovisionpreproc name=visionpreproc config-file-path=%s ! \
                        fakesink name=fakesink",
                        video_device.c_str(), num_buffers, format.c_str(), width, height, CONFIG_JSON_FILE_PATH);

    pipeline = gst_parse_launch(pipe_desc, NULL);
    g_free(pipe_desc);
    return pipeline;
}

static GstPadProbeReturn
buffer_callback(GstObject *pad, GstPadProbeInfo *info, gpointer data)
{
    GstBuffer *buffer;
    struct hailo15_vsm vsm;
    uint index;
    gint isp_ae_fps;

    buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    // Verify buffer contains VSM metadata
    GstHailoV4l2Meta *meta = reinterpret_cast<GstHailoV4l2Meta *>(gst_buffer_get_meta(buffer, g_type_from_name(HAILO_V4L2_META_API_NAME)));
    fail_unless(meta != NULL);

    vsm = meta->vsm;
    index = meta->v4l2_index;
    isp_ae_fps = meta->isp_ae_fps;
    GST_DEBUG("VSM metadata: index=%d, dx=%d, dy=%d isp_ae_fps=%d\n", index, vsm.dx, vsm.dy, isp_ae_fps);
    fail_unless(expected_index == index);

    expected_index = (expected_index + 1) % MAX_V4L_BUFFERS;
    return GST_PAD_PROBE_OK;
}

static void
run_pipeline(GstElement *pipeline, guint timeout_in_seconds)
{
    GstElement *visionpreproc;
    GstPad *pad;
    gulong probe;
    GstBus *bus;
    GstMessage *msg;
    GstStateChangeReturn state_ret;
    expected_index = 0;

    bus = gst_element_get_bus(pipeline);

    visionpreproc = gst_bin_get_by_name(GST_BIN(pipeline), "visionpreproc");
    fail_unless(visionpreproc != NULL);

    pad = gst_element_get_static_pad(visionpreproc, "sink_0");

    probe =
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                          (GstPadProbeCallback)buffer_callback, visionpreproc, NULL);

    state_ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    fail_unless(state_ret != GST_STATE_CHANGE_FAILURE);

    msg = gst_bus_poll(bus, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS), timeout_in_seconds * GST_SECOND);
    fail_unless(msg != NULL, "timeout waiting for error or eos message");

    fail_unless_equals_int(gst_element_set_state(pipeline, GST_STATE_NULL), GST_STATE_CHANGE_SUCCESS);

    gst_message_unref(msg);
    gst_object_unref(bus);
    gst_pad_remove_probe(pad, probe);
    gst_object_unref(pad);
    gst_object_unref(visionpreproc);
}

GST_START_TEST(test_v4l2src_vsm_metadata_nv12_1920x1080)
{
    GstElement *pipeline;

    GST_DEBUG("Creating pipeline: format=NV12, width=1920, height=1080\n");
    pipeline = create_v4l2_pipeline(DEFAULT_VIDEO_DEVICE, "NV12", guint(1920), guint(1080), guint(60));
    fail_unless(pipeline != NULL);

    run_pipeline(pipeline, 20);

    gst_object_unref(pipeline);
}

GST_END_TEST;

GST_START_TEST(test_v4l2src_vsm_metadata_rgb_1920x1080)
{
    GstElement *pipeline;

    GST_DEBUG("Creating pipeline: format=NV12, width=1920, height=1080\n");
    pipeline = create_v4l2_pipeline(DEFAULT_VIDEO_DEVICE, "RGB", guint(1920), guint(1080), guint(60));
    fail_unless(pipeline != NULL);

    run_pipeline(pipeline, 20);

    gst_object_unref(pipeline);
}

GST_END_TEST;

GST_START_TEST(test_v4l2src_vsm_metadata_nv12_640x640)
{
    GstElement *pipeline;

    GST_DEBUG("Creating pipeline: format=NV12, width=640, height=640\n");
    pipeline = create_v4l2_pipeline(DEFAULT_VIDEO_DEVICE, "NV12", guint(640), guint(640), guint(60));
    fail_unless(pipeline != NULL);

    run_pipeline(pipeline, 20);

    gst_object_unref(pipeline);
}

GST_END_TEST;

GST_START_TEST(test_v4l2src_vsm_metadata_nv12_3840x2160)
{
    GstElement *pipeline;

    GST_DEBUG("Creating pipeline: format=NV12, width=3840, height=2160\n");
    pipeline = create_v4l2_pipeline(DEFAULT_VIDEO_DEVICE, "NV12", guint(3840), guint(2160), guint(40));
    fail_unless(pipeline != NULL);

    run_pipeline(pipeline, 20);

    gst_object_unref(pipeline);
}

GST_END_TEST;

GST_START_TEST(test_v4l2src_vsm_metadata_rgb_300x300)
{
    GstElement *pipeline;

    GST_DEBUG("Creating pipeline: format=RGB, width=300, height=300\n");
    pipeline = create_v4l2_pipeline(DEFAULT_VIDEO_DEVICE, "RGB", guint(300), guint(300), guint(60));
    fail_unless(pipeline != NULL);

    run_pipeline(pipeline, 20);

    gst_object_unref(pipeline);
}

GST_END_TEST;

// Suite definition to allow to run a group of test and allow for further control
// Of what test to run
static Suite *
v4l2src_to_visionpreproc_suite(void)
{
    Suite *s = suite_create("v4l2_to_visionpreproc_pipeline");
    TCase *tc_chain = tcase_create("v4l2_vsm_metadata_test");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_v4l2src_vsm_metadata_nv12_1920x1080);
    tcase_add_test(tc_chain, test_v4l2src_vsm_metadata_nv12_640x640);
    tcase_add_test(tc_chain, test_v4l2src_vsm_metadata_nv12_3840x2160);
    tcase_add_test(tc_chain, test_v4l2src_vsm_metadata_rgb_1920x1080);
    tcase_add_test(tc_chain, test_v4l2src_vsm_metadata_rgb_300x300);

    return s;
}

// Defines what suite to run as part of the normal calling
GST_CHECK_MAIN(v4l2src_to_visionpreproc);