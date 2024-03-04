#include "pipeline/pipeline.hpp"
#include "common/common.hpp"
#include "resources/resources.hpp"
#include <gst/gst.h>
// #include "osd_utils.hpp"

using namespace webserver::pipeline;
using namespace webserver::resources;
#ifndef MEDIALIB_LOCAL_SERVER
using namespace privacy_mask_types;
#endif

std::shared_ptr<Pipeline> Pipeline::create()
{
    auto resources = ResourceRepository::create();
    auto osd_resource = std::static_pointer_cast<OsdResource>(resources->get(RESOURCE_OSD));
    auto ai_resource = std::static_pointer_cast<AiResource>(resources->get(RESOURCE_AI));

    nlohmann::json enc_osd_conf = Pipeline::create_encoder_osd_config(osd_resource->get_current_osd_config(), resources->get(RESOURCE_ENCODER)->get());

    std::ostringstream pipeline;
    pipeline << "v4l2src device=" << V4L2_DEVICE_NAME << " io-mode=mmap ! ";
    pipeline << "video/x-raw,format=NV12,width=3840,height=2160,framerate=30/1 ! ";
    pipeline << "queue name=q1 leaky=downstream max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailodenoise name=denoise config-string='" << ai_resource->get_ai_config(AiResource::AiApplications::AI_APPLICATION_DENOISE) << "' ! ";
    pipeline << "video/x-raw,format=NV12,width=3840,height=2160,framerate=30/1 ! ";
    pipeline << "queue name=q2 leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailodefog name=defog config-string='" << ai_resource->get_ai_config(AiResource::AiApplications::AI_APPLICATION_DEFOG) << "' ! ";
    pipeline << "video/x-raw,format=NV12,width=3840,height=2160,framerate=30/1 ! ";
    pipeline << "queue name=q3 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailofrontend name=frontend config-string='" << resources->get(RESOURCE_FRONTEND)->to_string() << "' ";
    pipeline << "hailomuxer name=mux ";
    pipeline << "frontend. ! ";
    pipeline << "queue name=q4 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "mux. ";
    pipeline << "frontend. ! ";
    pipeline << "queue name=q5 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailovideoscale ! video/x-raw, width=640, height=640 ! ";
    pipeline << "queue name=q6 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailonet2 name=detection hef-path=/home/root/apps/detection/resources/yolov5m_wo_spp_60p_nv12_640.hef pass-through=false nms-iou-threshold=0.45 nms-score-threshold=0.3 scheduling-algorithm=1 vdevice-group-id=device0 ! ";
    pipeline << "queue name=q7 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "hailofilter function-name=yolov5m config-path=/home/root/apps/detection/resources/configs/yolov5.json so-path=/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so qos=false ! ";
    pipeline << "queue name=q8 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "mux. ";
    pipeline << "mux. ! ";
    pipeline << "hailooverlay qos=false ! ";
    pipeline << "hailoencodebin name=enc enforce-caps=false config-string='" << enc_osd_conf.dump() << "' ! ";
    pipeline << "video/x-h264,framerate=30/1 ! ";
    pipeline << "queue name=q10 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "h264parse config-interval=-1 ! ";
    pipeline << "queue name=q11 leaky=no max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! ";
    pipeline << "rtph264pay config-interval=1 ! ";
    pipeline << "application/x-rtp, media=(string)video, encoding-name=(string)H264 ! ";
    pipeline << "udpsink host=" << UDP_HOST << " port=5000";
    std::string pipeline_str = pipeline.str();
    std::cout << "Pipeline: \n"
              << pipeline_str << std::endl;
    return std::make_shared<Pipeline>(resources, pipeline_str);
}

nlohmann::json webserver::pipeline::Pipeline::create_encoder_osd_config(nlohmann::json osd_config, nlohmann::json encoder_config)
{
    nlohmann::json encoder_osd_config;
    encoder_osd_config["osd"] = osd_config;
    encoder_osd_config["hailo_encoder"] = encoder_config;
    return encoder_osd_config;
}

void webserver::pipeline::Pipeline::restart_stream()
{
    std::cout << "Restarting Gstreamer Pipeline!" << std::endl;

    gboolean ret = gst_element_send_event(m_pipeline, gst_event_new_eos());
    if (!ret)
    {
        std::cout << "Failed to send EOS event" << std::endl;
        throw std::runtime_error("Failed to send EOS event");
    }

    m_main_loop_thread->join();

    gst_element_set_state(m_pipeline, GST_STATE_NULL);

    std::cout << "Joined main loop thread" << std::endl;

    gst_object_unref(m_pipeline);
    m_pipeline = gst_parse_launch(m_gst_pipeline_str.c_str(), NULL);
    if (!m_pipeline)
    {
        std::cout << "Failed to create pipeline" << std::endl;
        throw std::runtime_error("Failed to create pipeline");
    }

    std::cout << "Starting pipeline" << std::endl;
    ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        std::cout << "Failed to start pipeline" << std::endl;
        throw std::runtime_error("Failed to start pipeline");
    }

    m_main_loop_thread = std::make_shared<std::thread>(
        [this]()
        {
            wait_for_end_of_pipeline();
        });
}

Pipeline::Pipeline(WebserverResourceRepository resources, std::string gst_pipeline_str)
    : IPipeline(resources, gst_pipeline_str)
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
        std::string conf = m_resources->get(RESOURCE_FRONTEND)->to_string();
        std::cout << "Setting frontend config: " << conf << std::endl;
        g_object_set(frontend, "config-string", conf.c_str(), NULL);
        gst_object_unref(frontend);
        break;
    }
    case RESOURCE_OSD:
    case RESOURCE_ENCODER:
    {
        GstElement *encoder = gst_bin_get_by_name(GST_BIN(m_pipeline), "enc");
        auto osd_conf = std::static_pointer_cast<OsdResource>(m_resources->get(RESOURCE_OSD))->get();
        auto enc_conf = m_resources->get(RESOURCE_ENCODER)->get();
        std::cout << "Setting encoder config: " << enc_conf << std::endl;
        std::cout << "Setting osd config: " << osd_conf << std::endl;
        nlohmann::json enc_osd_conf = Pipeline::create_encoder_osd_config(osd_conf, enc_conf);
        g_object_set(encoder, "config-string", enc_osd_conf.dump(), NULL);
        gst_object_unref(encoder);
        break;

        // GstElement *osd = gst_bin_get_by_name(GST_BIN(m_pipeline), "osd");

        // auto osd_resource = std::static_pointer_cast<OsdResource>(m_resources->get(RESOURCE_OSD));
        // std::string conf = osd_resource->get_current_osd_config();
        // auto m_blender = gst_hailoosd_get_blender(osd);

        // std::map<std::string, bool> osd_config;
        // for (auto &config : osd_resource->get_current_osd_internal_config())
        // {
        //     osd_config[config["id"]] = config["enabled"];
        // }

        // for (auto &config : osd_config)
        // {
        //     std::string id = config.first;
        //     bool enabled = config.second;

        //     bool is_overlay_contained = m_blender->is_overlay_contained(id);

        //     if (enabled && !is_overlay_contained)
        //     {
        //         auto osd_to_add = osd_resource->get_osd_config_by_id(id);
        //         m_blender->add_overlay(osd_to_add);
        //     }
        //     else if (!enabled && is_overlay_contained)
        //     {
        //         m_blender->remove_overlay(id);
        //     }
        // }

        // gst_object_unref(osd);
        break;
    }
    case RESOURCE_AI:
    {
        auto state = std::static_pointer_cast<AiResource::AiResourceState>(notif.resource_state);

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

        auto ai_resource = std::static_pointer_cast<AiResource>(m_resources->get(RESOURCE_AI));

        // if denoise is in any of the lists, update its config
        if (std::find(state->enabled.begin(), state->enabled.end(), AiResource::AI_APPLICATION_DENOISE) != state->enabled.end() ||
            std::find(state->disabled.begin(), state->disabled.end(), AiResource::AI_APPLICATION_DENOISE) != state->disabled.end())
        {
            GstElement *denoise = gst_bin_get_by_name(GST_BIN(m_pipeline), "denoise");
            g_object_set(denoise, "config-string", ai_resource->get_ai_config(AiResource::AI_APPLICATION_DENOISE).c_str(), NULL);
            gst_object_unref(denoise);
        }

        // if defog is in any of the lists, update its config
        if (std::find(state->enabled.begin(), state->enabled.end(), AiResource::AI_APPLICATION_DEFOG) != state->enabled.end() ||
            std::find(state->disabled.begin(), state->disabled.end(), AiResource::AI_APPLICATION_DEFOG) != state->disabled.end())
        {
            GstElement *defog = gst_bin_get_by_name(GST_BIN(m_pipeline), "defog");
            g_object_set(defog, "config-string", ai_resource->get_ai_config(AiResource::AI_APPLICATION_DEFOG).c_str(), NULL);
            gst_object_unref(defog);
        }

        break;
    }
    case RESOURCE_ISP:
    {
        auto state = std::static_pointer_cast<IspResource::IspResourceState>(notif.resource_state);
        if (state->should_restart_stream)
            this->restart_stream();
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
    default:
        break;
    }
}