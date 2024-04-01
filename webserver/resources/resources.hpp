#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <mutex>
#include "common/isp/v4l2_ctrl.hpp"

#ifndef MEDIALIB_LOCAL_SERVER
#include "privacy_mask_types.hpp"
#include "privacy_mask.hpp"
using namespace privacy_mask_types;
#else
struct vertex
{
    uint x, y;

    vertex(uint x, uint y) : x(x), y(y) {}
};
struct polygon
{
    std::string id;
    std::vector<vertex> vertices;
};
class PrivacyMaskBlender
{
public:
    void add_privacy_mask(polygon mask) {}
    void remove_privacy_mask(std::string id) {}
};
#endif
using namespace webserver::common;

namespace webserver
{
    namespace resources
    {
        enum ResourceType
        {
            RESOURCE_FRONTEND,
            RESOURCE_ENCODER,
            RESOURCE_OSD,
            RESOURCE_AI,
            RESOURCE_ISP,
            RESOURCE_PRIVACY_MASK,
        };

        enum ResourceBehaviorType
        {
            RESOURCE_BEHAVIOR_CONFIG,
            RESOURCE_BEHAVIOR_FUNCTIONAL
        };

        NLOHMANN_JSON_SERIALIZE_ENUM(ResourceType, {
                                                       {RESOURCE_FRONTEND, "frontend"},
                                                       {RESOURCE_ENCODER, "encoder"},
                                                       {RESOURCE_OSD, "osd"},
                                                       {RESOURCE_AI, "ai"},
                                                       {RESOURCE_ISP, "isp"},
                                                       {RESOURCE_PRIVACY_MASK, "privacy_mask"},
                                                   })

        NLOHMANN_JSON_SERIALIZE_ENUM(ResourceBehaviorType, {
                                                               {RESOURCE_BEHAVIOR_CONFIG, "config"},
                                                               {RESOURCE_BEHAVIOR_FUNCTIONAL, "functional"},
                                                           })
        class ResourceState
        {
        };

        class ResourceStateChangeNotification
        {
        public:
            ResourceType resource_type;
            std::shared_ptr<ResourceState> resource_state;
        };

        class ConfigResourceState : public ResourceState
        {
        public:
            std::string config;
            ConfigResourceState(std::string config) : config(config) {}
        };

        typedef std::function<void(ResourceStateChangeNotification)> ResourceChangeCallback;

        class Resource
        {
        protected:
            std::string m_default_config;
            nlohmann::json m_config;
            std::vector<ResourceChangeCallback> m_callbacks;

        public:
            Resource() = default;
            virtual ~Resource() = default;
            virtual std::string name() = 0;
            virtual ResourceType get_type() = 0;
            virtual void http_register(std::shared_ptr<httplib::Server> srv) = 0;

            virtual std::string to_string()
            {
                return m_config.dump();
            }

            virtual nlohmann::json get()
            {
                return m_config;
            }

            virtual ResourceBehaviorType get_behavior_type()
            {
                return RESOURCE_BEHAVIOR_CONFIG; // default
            }

            virtual void on_resource_change(std::shared_ptr<ResourceState> state)
            {
                for (auto &callback : m_callbacks)
                {
                    callback({get_type(), state});
                }
            }

            void subscribe_callback(ResourceChangeCallback callback)
            {
                m_callbacks.push_back(callback);
            }
        };

        class AiResource : public Resource
        {
        public:
            enum AiApplications
            {
                AI_APPLICATION_DETECTION,
                AI_APPLICATION_DENOISE,
                AI_APPLICATION_DEFOG,
            };

            class AiResourceState : public ResourceState
            {
            public:
                std::vector<AiApplications> enabled;
                std::vector<AiApplications> disabled;
            };

            AiResource();
            void http_register(std::shared_ptr<httplib::Server> srv) override;
            std::string name() override { return "ai"; }
            ResourceType get_type() override { return RESOURCE_AI; }
            nlohmann::json get_ai_config(AiApplications app);
            std::vector<AiApplications> get_enabled_applications();

        private:
            void http_patch(nlohmann::json body);
            std::shared_ptr<AiResourceState> parse_state(std::vector<AiApplications> current_enabled, std::vector<AiApplications> prev_enabled);
            nlohmann::json m_defog_config;
            nlohmann::json m_denoise_config;
            std::mutex m_mutex;
        };

        class FrontendResource : public Resource
        {
        public:
            FrontendResource(std::shared_ptr<webserver::resources::AiResource> ai_res);
            void http_register(std::shared_ptr<httplib::Server> srv) override;
            std::string name() override { return "frontend"; }
            ResourceType get_type() override { return RESOURCE_FRONTEND; }
            nlohmann::json get_frontend_config();

        private:
            std::shared_ptr<webserver::resources::AiResource> m_ai_resource;
        };

        class IspResource : public Resource
        {
        private:
            std::unique_ptr<common::v4l2Control> m_v4l2;
            std::shared_ptr<AiResource> m_ai_resource;
            stream_isp_params_t m_baseline_stream_params;
            int16_t m_baseline_wdr_params;
            auto_exposure_t get_auto_exposure();
            void set_auto_exposure(nlohmann::json &req, httplib::Response &res);
            bool set_auto_exposure(auto_exposure_t &ae);
            void on_ai_state_change(std::shared_ptr<AiResource::AiResourceState> state);

            struct tuning_profile_context_t
            {
                tuning_profile_t current;
                tuning_profile_t fallback;
            };
            tuning_profile_context_t m_tuning_profile;

        public:
            class IspResourceState : public ResourceState
            {
            public:
                bool isp_3aconfig_updated;
                IspResourceState(bool isp_3aconfig_updated) : isp_3aconfig_updated(isp_3aconfig_updated) {}
            };
            IspResource(std::shared_ptr<AiResource> ai_res);
            void http_register(std::shared_ptr<httplib::Server> srv) override;
            std::string name() override { return "isp"; }
            ResourceType get_type() override { return RESOURCE_ISP; }
            ResourceBehaviorType get_behavior_type() override { return RESOURCE_BEHAVIOR_FUNCTIONAL; }
            void override_configurations();
            void init(bool set_auto_exposure = true, bool set_auto_wb = true);
        };

        class EncoderResource : public Resource
        {
        public:
            enum bitrate_control_t
            {
                VBR = 0,
                CBR = 1
            };

            struct encoder_control_t
            {
                int bitrate;
                bitrate_control_t bitrate_control;
            };

            class EncoderResourceState : public ResourceState
            {
            public:
                encoder_control_t control;
            };

            EncoderResource();
            void http_register(std::shared_ptr<httplib::Server> srv) override;
            std::string name() override { return "encoder"; }
            ResourceType get_type() override { return RESOURCE_ENCODER; }
            encoder_control_t get_encoder_control();

        private:
            encoder_control_t m_encoder_control;
            void set_encoder_control(encoder_control_t &encoder_control);
        };

        NLOHMANN_JSON_SERIALIZE_ENUM(EncoderResource::bitrate_control_t, {{webserver::resources::EncoderResource::VBR, "VBR"},
                                                                          {webserver::resources::EncoderResource::CBR, "CBR"}})

        class OsdResource : public Resource
        {
        private:
            std::vector<nlohmann::json> m_osd_configs;
            nlohmann::json get_osd_config_by_id(std::string id);

        public:
            OsdResource();
            void http_register(std::shared_ptr<httplib::Server> srv) override;
            std::string name() override { return "osd"; }
            ResourceType get_type() override { return RESOURCE_OSD; }
            nlohmann::json get_current_osd_config();
        };

        class PrivacyMaskResource : public Resource
        {
        public:
            class PrivacyMaskResourceState : public ResourceState
            {
            public:
                std::vector<std::string> enabled;
                std::vector<std::string> disabled;
            };

        private:
            std::map<std::string, polygon> m_privacy_masks;
            std::shared_ptr<PrivacyMaskResourceState> parse_state(std::vector<std::string> current_enabled, std::vector<std::string> prev_enabled);
            std::vector<std::string> get_enabled_masks();

        public:
            PrivacyMaskResource();
            void http_register(std::shared_ptr<httplib::Server> srv) override;
            std::string name() override { return "privacy_mask"; }
            ResourceType get_type() override { return RESOURCE_PRIVACY_MASK; }
            ResourceBehaviorType get_behavior_type() override { return RESOURCE_BEHAVIOR_CONFIG; }
            std::map<std::string, polygon> get_privacy_masks() { return m_privacy_masks; }
        };

        void to_json(nlohmann::json &j, const EncoderResource::encoder_control_t &b);
        void from_json(const nlohmann::json &j, EncoderResource::encoder_control_t &b);
    }
}

typedef std::shared_ptr<webserver::resources::Resource> WebserverResource;
