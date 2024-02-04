#include "buffer_utils.hpp"
#include "media_library/encoder.hpp"
#include "media_library/vision_pre_proc.hpp"
#include <algorithm>
#include <fstream>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <tl/expected.hpp>

#define FONT_1_PATH "/usr/share/fonts/ttf/LiberationMono-Regular.ttf"
#define FONT_2_PATH "/usr/share/fonts/ttf/LiberationSans-Italic.ttf"

struct MediaLibrary
{
    MediaLibraryVisionPreProcPtr vision_preproc;
    MediaLibraryEncoderPtr encoder;
};

const char *VISION_PREPROC_CONFIG_FILE = "/usr/bin/preproc_config_example.json";
const char *ENCODER_OSD_CONFIG_FILE = "/usr/bin/encoder_config_example.json";
const char *OUTPUT_FILE = "/var/volatile/tmp/vision_preproc_example.h264";

static gboolean waiting_eos = FALSE;
static gboolean caught_sigint = FALSE;

static void sigint_restore(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;

    sigaction(SIGINT, &action, NULL);
}

/* we only use sighandler here because the registers are not important */
static void sigint_handler_sighandler(int signum)
{
    /* If we were waiting for an EOS, we still want to catch
     * the next signal to shutdown properly (and the following one
     * will quit the program). */
    if (waiting_eos)
    {
        waiting_eos = FALSE;
    }
    else
    {
        sigint_restore();
    }
    /* we set a flag that is checked by the mainloop, we cannot do much in the
     * interrupt handler (no mutex or other blocking stuff) */
    caught_sigint = TRUE;
}

void add_sigint_handler(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = sigint_handler_sighandler;

    sigaction(SIGINT, &action, NULL);
}

/* is called every 250 milliseconds (4 times a second), the interrupt handler
 * will set a flag for us. We react to this by posting a message. */
static gboolean check_sigint(GstElement *pipeline)
{
    if (!caught_sigint)
    {
        return TRUE;
    }
    else
    {
        caught_sigint = FALSE;
        waiting_eos = TRUE;
        GST_INFO_OBJECT(pipeline, "handling interrupt. send EOS");
        GST_ERROR_OBJECT(pipeline, "handling interrupt. send EOS");
        gst_element_send_event(pipeline, gst_event_new_eos());

        /* remove timeout handler */
        return FALSE;
    }
}

GstFlowReturn wait_for_end_of_pipeline(GstElement *pipeline)
{
    GstBus *bus;
    GstMessage *msg;
    GstFlowReturn ret = GST_FLOW_ERROR;
    bus = gst_element_get_bus(pipeline);
    gboolean done = FALSE;
    // This function blocks until an error or EOS message is received.
    while (!done)
    {
        msg = gst_bus_timed_pop_filtered(
            bus, GST_MSECOND * 250,
            (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

        if (msg != NULL)
        {
            GError *err;
            gchar *debug_info;
            done = TRUE;
            waiting_eos = FALSE;
            sigint_restore();
            switch (GST_MESSAGE_TYPE(msg))
            {
            case GST_MESSAGE_ERROR:
            {
                gst_message_parse_error(msg, &err, &debug_info);
                GST_ERROR("Error received from element %s: %s",
                          GST_OBJECT_NAME(msg->src), err->message);

                std::string dinfo =
                    debug_info ? std::string(debug_info) : "none";
                GST_ERROR("Debugging information : %s", dinfo.c_str());

                g_clear_error(&err);
                g_free(debug_info);
                ret = GST_FLOW_ERROR;
                break;
            }
            case GST_MESSAGE_EOS:
            {
                GST_INFO("End-Of-Stream reached");
                ret = GST_FLOW_OK;
                break;
            }
            default:
            {
                // We should not reach here because we only asked for ERRORs and
                // EOS
                GST_WARNING("Unexpected message received %d",
                            GST_MESSAGE_TYPE(msg));
                ret = GST_FLOW_ERROR;
                break;
            }
            }
            gst_message_unref(msg);
        }
        check_sigint(pipeline);
    }
    gst_object_unref(bus);
    return ret;
}

/**
 * Appsink's propose_allocation callback - Adding an GST_VIDEO_META_API_TYPE
 * allocation meta
 *
 * @param[in] appsink               The appsink object.
 * @param[in] appsink               The allocation query.
 * @param[in] callback_data         user data.
 * @return TRUE
 * @note The adding of allocation meta is required to work with v4l2src without
 * it copying each buffer.
 */
static gboolean appsink_propose_allocation(GstAppSink *appsink, GstQuery *query,
                                           gpointer callback_data)
{
    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
    return TRUE;
}

static void update_custom_overlay(std::shared_ptr<osd::Blender> blender,
                                  std::string id, uint value)
{
    auto custom_expected = blender->get_overlay(id);
    if (custom_expected.has_value())
    {
        // clip value to 0-255
        if (value > 255)
        {
            value = 255;
        }
        if (value < 0)
        {
            value = 0;
        }
        auto existing_custom_overlay =
            std::static_pointer_cast<osd::CustomOverlay>(
                custom_expected.value());
        DspImagePropertiesPtr dsp_image = existing_custom_overlay->get_buffer();
        dsp_data_plane_t *planes = dsp_image->planes;
        // iterate over planes
        for (uint i = 0; i < dsp_image->planes_count; i++)
        {
            memset(planes[i].userptr, value, planes[i].bytesused);
        }
    }
}

std::shared_future<media_library_return> running_osd_task;
std::shared_future<media_library_return> add_text_task;

/**
 * Appsink's new_sample callback
 *
 * @param[in] appsink               The appsink object.
 * @param[in] user_data             user data.
 * @return GST_FLOW_OK
 * @note Example only - only mapping the buffer to a GstMapInfo, than unmapping.
 */
static GstFlowReturn appsink_new_sample(GstAppSink *appsink, gpointer user_data)
{
    GstSample *sample;
    GstBuffer *gst_buffer;
    HailoMediaLibraryBufferPtr buffer = nullptr;
    std::vector<hailo_media_library_buffer> outputs;
    MediaLibrary *media_lib = static_cast<MediaLibrary *>(user_data);
    GstFlowReturn return_status = GST_FLOW_OK;

    // get the incoming sample
    sample = gst_app_sink_pull_sample(appsink);
    GstCaps *caps = gst_sample_get_caps(sample);

    gst_buffer = gst_sample_get_buffer(sample);
    if (gst_buffer)
    {
        buffer = hailo_buffer_from_gst_buffer(gst_buffer, caps);
        auto blender = media_lib->encoder->get_blender();
        // add new overlay after 50 frames
        if (GST_BUFFER_OFFSET(gst_buffer) == 50)
        {
            osd::TextOverlay new_text = osd::TextOverlay("e1",
                                                         0.1, 0.3, "Camera Stream", osd::rgb_color_t(0, 0, 255), osd::rgb_color_t(255, 255, 255), 100.0, 1,
                                                         1, FONT_1_PATH, 0, osd::rotation_alignment_policy_t::CENTER);
            add_text_task = blender->add_overlay_async(new_text);
        }

        if (GST_BUFFER_OFFSET(gst_buffer) == 100)
        {
            auto txt_expected = blender->get_overlay("example_text1");
            auto txt = std::static_pointer_cast<osd::TextOverlay>(
                txt_expected.value());
            txt->y += 0.1;
            blender->set_overlay(*txt);
        }
        if (GST_BUFFER_OFFSET(gst_buffer) == 150)
        {
            {
                // get overlay with id e1
                auto overlay_expected = blender->get_overlay("e1");

                if (overlay_expected.has_value())
                {
                    auto overlay = overlay_expected.value();
                    auto text_overlay =
                        std::static_pointer_cast<osd::TextOverlay>(overlay);
                    text_overlay->rgb = osd::rgb_color_t(102, 0, 51);
                    blender->set_overlay(*text_overlay);
                }
            }

            {
                auto txt_expected = blender->get_overlay("example_text2");
                auto txt = std::static_pointer_cast<osd::TextOverlay>(
                    txt_expected.value());
                txt->font_path = FONT_2_PATH;
                blender->set_overlay(*txt);
            }
        }

        if (GST_BUFFER_OFFSET(gst_buffer) % 50 == 0)
        {
            // Update custom overlay
            uint value = 50;
            if (GST_BUFFER_OFFSET(gst_buffer) % 100 == 0)
                value = 125;
            else if (GST_BUFFER_OFFSET(gst_buffer) % 150 == 0)
                value = 200;
            update_custom_overlay(blender, "custom", value);
        }

        if (GST_BUFFER_OFFSET(gst_buffer) % 50 == 0 && GST_BUFFER_OFFSET(gst_buffer) != 0)
        {
            // rotate image
            if (running_osd_task.valid())
                running_osd_task.wait();

            auto img_expected = blender->get_overlay("example_image");
            auto img = std::static_pointer_cast<osd::ImageOverlay>(img_expected.value());
            img->angle += 10;
            running_osd_task = blender->set_overlay_async(*img); // save future to a variable, because destructor blocks until finished
            
        }
    }

    // perform vision_preproc logic
    media_library_return preproc_status = media_lib->vision_preproc->handle_frame(*buffer.get(), outputs);
    if (preproc_status != MEDIA_LIBRARY_SUCCESS)
        return_status = GST_FLOW_ERROR;

    // encode
    HailoMediaLibraryBufferPtr hailo_buffer = std::make_shared<hailo_media_library_buffer>(std::move(outputs[0]));
    media_lib->encoder->add_buffer(hailo_buffer);

    gst_sample_unref(sample);
    return return_status;
}

/**
 * Create the gstreamer pipeline as string
 *
 * @return A string containing the gstreamer pipeline.
 * @note prints the return value to the stdout.
 */
std::string create_src_pipeline_string()
{
    std::string pipeline = "";

    pipeline =
        "v4l2src name=src_element num-buffers=900 device=/dev/video0 "
        "io-mode=mmap ! "
        "video/x-raw,format=NV12,width=3840,height=2160, framerate=30/1 ! "
        "queue leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! "
        "appsink wait-on-eos=false name=hailo_sink";

    std::cout << "Pipeline:" << std::endl;
    std::cout << "gst-launch-1.0 " << pipeline << std::endl;

    return pipeline;
}

/**
 * Set the Appsink callbacks
 *
 * @param[in] pipeline        The pipeline as a GstElement.
 * @note Sets the new_sample and propose_allocation callbacks, without callback
 * user data (NULL).
 */
void set_callbacks(GstElement *pipeline, MediaLibrary *media_lib)
{
    GstAppSinkCallbacks callbacks = {NULL};

    GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "hailo_sink");
    callbacks.new_sample = appsink_new_sample;
    callbacks.propose_allocation = appsink_propose_allocation;

    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks,
                               (gpointer)media_lib, NULL);
    gst_object_unref(appsink);
}

void write_encoded_data(HailoMediaLibraryBufferPtr buffer, uint32_t size)
{
    char *data = (char *)buffer->get_plane(0);
    std::ofstream fp(OUTPUT_FILE, std::ios::out | std::ios::binary | std::ios::app);
    if (!fp.good())
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    if (!data)
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    fp.write(data, size);
    fp.close();
    buffer->decrease_ref_count();
}

std::string read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error("config path is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)),
                            std::istreambuf_iterator<char>());
    file_to_read.close();
    std::cout << "Read config from file: " << file_path << std::endl;
    return file_string;
}

void delete_output_file()
{
    std::ofstream fp(OUTPUT_FILE, std::ios::out | std::ios::binary);
    if (!fp.good())
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    fp.close();
}

int main(int argc, char *argv[])
{
    GstFlowReturn ret;
    MediaLibrary *media_lib = new MediaLibrary();
    add_sigint_handler();
    delete_output_file();

    // Create and configure vision_pre_proc
    std::string preproc_config_string = read_string_from_file(VISION_PREPROC_CONFIG_FILE);
    tl::expected<MediaLibraryVisionPreProcPtr, media_library_return> vision_preproc_expected = MediaLibraryVisionPreProc::create(preproc_config_string);
    if (!vision_preproc_expected.has_value())
    {
        std::cout << "Failed to create vision_preproc" << std::endl;
        return 1;
    }
    media_lib->vision_preproc = vision_preproc_expected.value();

    // Create and configure encoder
    std::string encoderosd_config_string = read_string_from_file(ENCODER_OSD_CONFIG_FILE);
    tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected = MediaLibraryEncoder::create(std::move(encoderosd_config_string));
    if (!encoder_expected.has_value())
    {
        std::cout << "Failed to create encoder osd" << std::endl;
        return 1;
    }

    media_lib->encoder = encoder_expected.value();

    gst_init(&argc, &argv);
    std::string src_pipeline_string = create_src_pipeline_string();
    std::cout << "Created pipeline string." << std::endl;
    GstElement *pipeline = gst_parse_launch(src_pipeline_string.c_str(), NULL);
    std::cout << "Parsed pipeline." << std::endl;
    set_callbacks(pipeline, media_lib);
    std::cout << "Set probes and callbacks." << std::endl;
    media_lib->encoder->subscribe(
        [&](HailoMediaLibraryBufferPtr buffer, size_t size)
        {
            write_encoded_data(buffer, size);
        });
    media_lib->encoder->start();

    std::cout << "Setting state to playing." << std::endl;
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    auto blender = media_lib->encoder->get_blender();
    osd::CustomOverlay custom_overlay;
    custom_overlay.x = 0.01;
    custom_overlay.y = 0.01;
    custom_overlay.width = 0.1;
    custom_overlay.height = 0.1;
    custom_overlay.id = "custom";
    blender->add_overlay(custom_overlay);
    blender->set_frame_size(1920, 1080);

    update_custom_overlay(blender, "custom", 0);
    ret = wait_for_end_of_pipeline(pipeline);
    media_lib->encoder->stop();

    // Free resources
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    gst_deinit();
    delete media_lib;

    return ret;
}
