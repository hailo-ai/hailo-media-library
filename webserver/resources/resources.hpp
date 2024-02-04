#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <httplib.h>

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
            ResourceType m_type;
            std::string m_default_config;
            nlohmann::json m_config;
            std::vector<ResourceChangeCallback> m_callbacks;

        public:
            Resource() = default;
            virtual ~Resource() = default;
            virtual std::string name() = 0;
            virtual void http_register(std::shared_ptr<httplib::Server> srv) = 0;

            virtual std::string to_string()
            {
                return std::string(m_config.dump());
            }

            virtual nlohmann::json get()
            {
                return m_config;
            }

            virtual ResourceType get_type()
            {
                return m_type;
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

        class FrontendResource : public Resource
        {
        public:
            FrontendResource();
            void http_register(std::shared_ptr<httplib::Server> srv) override;
            std::string name() override { return "frontend"; }
        };

        class EncoderResource : public Resource
        {
        public:
            EncoderResource();
            void http_register(std::shared_ptr<httplib::Server> srv) override;
            std::string name() override { return "encoder"; }
        };

        class OsdResource : public Resource
        {
        public:
            OsdResource();
            void http_register(std::shared_ptr<httplib::Server> srv) override;
            std::string name() override { return "osd"; }
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

        private:
            std::vector<AiApplications> get_enabled_applications();
            std::shared_ptr<AiResourceState> parse_state(std::vector<AiApplications> current_enabled, std::vector<AiApplications> prev_enabled);
        };
    }
}

typedef std::shared_ptr<webserver::resources::Resource> WebserverResource;
