#include "pipeline/pipeline.hpp"
#include "common/common.hpp"
#include "resources/resources.hpp"
#include <gst/gst.h>

using namespace webserver::pipeline;
using namespace webserver::resources;
#ifndef MEDIALIB_LOCAL_SERVER
using namespace privacy_mask_types;
#endif

std::shared_ptr<Pipeline> Pipeline::create()
{
    auto resources = ResourceRepository::create();
    return std::make_shared<Pipeline>(resources);
}

nlohmann::json Pipeline::create_encoder_osd_config(WebserverResourceRepository resources)
{
    auto osd_resource = std::static_pointer_cast<OsdResource>(resources->get(RESOURCE_OSD));
    nlohmann::json encoder_osd_config;
    encoder_osd_config["osd"] = osd_resource->get_current_osd_config();
    encoder_osd_config["encoding"]["hailo_encoder"] = resources->get(RESOURCE_ENCODER)->get();
    return encoder_osd_config;
}

std::string Pipeline::create_gst_pipeline_string()
{
    auto enabled_apps = std::static_pointer_cast<AiResource>(m_resources->get(RESOURCE_AI))->get_enabled_applications();
    auto detection_pass_through = std::find(enabled_apps.begin(), enabled_apps.end(), AiResource::AI_APPLICATION_DETECTION) != enabled_apps.end() ? "false" : "true";

    auto osd_resource = std::static_pointer_cast<OsdResource>(m_resources->get(RESOURCE_OSD));
    auto fe_resource = std::static_pointer_cast<FrontendResource>(m_resources->get(RESOURCE_FRONTEND));

    nlohmann::json encoder_osd_config;
    encoder_osd_config["osd"] = osd_resource->get_current_osd_config();
    encoder_osd_config["encoding"]["hailo_encoder"] = m_resources->get(RESOURCE_ENCODER)->get();

    // std::string multi_resize_config = R"({"digital_zoom":{"enabled":true,"magnification":1.0,"mode":"DIGITAL_ZOOM_MODE_MAGNIFICATION","roi":{"height":1800,"width":2800,"x":200,"y":200}}, "output_video":{"format":"IMAGE_FORMAT_NV12", "grayscale":false, "method":"INTERPOLATION_TYPE_BILINEAR", "resolutions":[{"framerate":30, "height":640,"pool_max_buffers":20, "width":640}]}})";

    std::ostringstream pipeline;
    pipeline << "hailofrontendbinsrc name=frontend config-string='" << fe_resource->get_frontend_config() << "' ";
    pipeline << "hailomuxer name=mux ";
    pipeline << "frontend. ! ";
    pipeline << "queue name=q4 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "mux. ";
    pipeline << "frontend. ! ";
    pipeline << "queue name=q5 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "video/x-raw, width=640, height=640 ! ";
    pipeline << "hailonet name=detection batch-size=4 hef-path=/home/root/apps/detection/resources/yolov5m_wo_spp_60p_nv12_640.hef pass-through=" << detection_pass_through << " nms-iou-threshold=0.45 nms-score-threshold=0.3 scheduling-algorithm=1 scheduler-threshold=4 scheduler-timeout-ms=1000 vdevice-group-id=1 vdevice-key=1 ! ";
    pipeline << "queue name=q6 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailofilter function-name=yolov5m config-path=/home/root/apps/detection/resources/configs/yolov5.json so-path=/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so qos=false ! ";
    pipeline << "queue name=q7 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "mux. ";
    pipeline << "mux. ! ";
    pipeline << "hailooverlay qos=false ! ";
    pipeline << "queue name=q8 leaky=downstream max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailoencodebin name=enc enforce-caps=false config-string='" << encoder_osd_config.dump() << "' ! ";
    pipeline << "video/x-h264,framerate=30/1 ! ";
    pipeline << "queue name=q9 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "h264parse config-interval=-1 ! ";
    pipeline << "queue name=q10 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "rtph264pay config-interval=1 ! ";
    pipeline << "application/x-rtp, media=(string)video, encoding-name=(string)H264 ! ";
    pipeline << "udpsink host=" << UDP_HOST << " port=5000";
    std::string pipeline_str = pipeline.str();
    std::cout << "Pipeline: \n"
              << pipeline_str << std::endl;
    return pipeline_str;
}

Pipeline::Pipeline(WebserverResourceRepository resources)
    : IPipeline(resources)
{
    for (const auto &[key, val] : resources->get_all_types())
    {
        for (const auto &resource_type : val)
        {
            auto resource = resources->get(resource_type);
            if (resource == nullptr)
                continue;
            resource->subscribe_callback([this](ResourceStateChangeNotification notif)
                                         { this->callback_handle_strategy(notif); });
        }
    }
}

void Pipeline::callback_handle_strategy(ResourceStateChangeNotification notif)
{
    std::cout << "Pipeline Resource callback, type: " << notif.resource_type << std::endl;
    switch (notif.resource_type)
    {
    case RESOURCE_FRONTEND:
    {
        GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
        auto fe_resource = std::static_pointer_cast<FrontendResource>(m_resources->get(RESOURCE_FRONTEND));
        std::string conf = fe_resource->get_frontend_config().dump();
        g_object_set(frontend, "config-string", conf.c_str(), NULL);
        gst_object_unref(frontend);
        break;
    }
    case RESOURCE_OSD:
    case RESOURCE_ENCODER:
    {
        GstElement *encoder_element = gst_bin_get_by_name(GST_BIN(m_pipeline), "enc");
        auto enc_resource = std::static_pointer_cast<EncoderResource>(m_resources->get(RESOURCE_FRONTEND));
        enc_resource->apply_config(encoder_element);
        gst_object_unref(encoder_element);
        break;
    }
    case RESOURCE_AI:
    {
        auto state = std::static_pointer_cast<AiResource::AiResourceState>(notif.resource_state);

        if (state->enabled.empty() && state->disabled.empty())
        {
            break;
        }

        // disable detection if it's in the disabled list
        if (std::find(state->disabled.begin(), state->disabled.end(), AiResource::AI_APPLICATION_DETECTION) != state->disabled.end())
        {
            GstElement *detection = gst_bin_get_by_name(GST_BIN(m_pipeline), "detection");
            g_object_set(detection, "pass-through", TRUE, NULL);
            gst_object_unref(detection);
        }
        // enable detection if it's in the enabled list
        else if (std::find(state->enabled.begin(), state->enabled.end(), AiResource::AI_APPLICATION_DETECTION) != state->enabled.end())
        {
            GstElement *detection = gst_bin_get_by_name(GST_BIN(m_pipeline), "detection");
            g_object_set(detection, "pass-through", FALSE, NULL);
            gst_object_unref(detection);
        }

        // // if denoise or defog was changed, change pipeline state to null, update config, and return to playing
        // if (std::find(state->disabled.begin(), state->disabled.end(), AiResource::AI_APPLICATION_DEFOG) != state->disabled.end() ||
        //     std::find(state->enabled.begin(), state->enabled.end(), AiResource::AI_APPLICATION_DEFOG) != state->enabled.end() ||
        //     std::find(state->disabled.begin(), state->disabled.end(), AiResource::AI_APPLICATION_DENOISE) != state->disabled.end() ||
        //     std::find(state->enabled.begin(), state->enabled.end(), AiResource::AI_APPLICATION_DENOISE) != state->enabled.end())
        // {
        //     std::cout << "**********************************************" << std::endl;
        //     this->stop();
        //     std::this_thread::sleep_for(std::chrono::milliseconds(500));
        //     this->start();
        //     std::cout << "**********************************************" << std::endl;
        // }
        break;
    }
    case RESOURCE_PRIVACY_MASK:
    {
        auto state = std::static_pointer_cast<PrivacyMaskResource::PrivacyMaskResourceState>(notif.resource_state);
        if (state->enabled.empty() && state->disabled.empty())
        {
            return;
        }
        std::shared_ptr<PrivacyMaskResource> pm_resource = std::static_pointer_cast<PrivacyMaskResource>(m_resources->get(RESOURCE_PRIVACY_MASK));
        auto masks = pm_resource->get_privacy_masks();

        GstElement *frontend = gst_bin_get_by_name(GST_BIN(m_pipeline), "frontend");
        GValue val = G_VALUE_INIT;
        g_object_get_property(G_OBJECT(frontend), "privacy-mask", &val);
        void *value_ptr = g_value_get_pointer(&val);
        auto privacy_blender = (PrivacyMaskBlender *)value_ptr;

        for (std::string id : state->enabled)
        {
            if (masks.find(id) != masks.end())
            {
                privacy_blender->add_privacy_mask(masks[id]);
            }
        }
        for (std::string id : state->disabled)
        {
            if (masks.find(id) != masks.end())
            {
                privacy_blender->remove_privacy_mask(id);
            }
        }

        break;
    }
    case RESOURCE_ISP:
    default:
        break;
    }
}