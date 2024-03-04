#pragma once
#include "resources/resources.hpp"
#include "resources/repository.hpp"
#include <gst/gst.h>

namespace webserver
{
    namespace pipeline
    {
        class IPipeline
        {
        protected:
            std::string m_gst_pipeline_str;
            WebserverResourceRepository m_resources;
            GstElement *m_pipeline;
            std::shared_ptr<std::thread> m_main_loop_thread;

        public:
            IPipeline(WebserverResourceRepository resources, std::string gst_pipeline_str);
            virtual ~IPipeline();
            virtual void start();
            virtual void stop();
            virtual WebserverResourceRepository get_resources() { return m_resources; };
            GstFlowReturn wait_for_end_of_pipeline();

            static std::shared_ptr<IPipeline> create();
        };

        class Pipeline : public IPipeline
        {
        public:
            Pipeline(WebserverResourceRepository resources, std::string gst_pipeline_str);
            static std::shared_ptr<Pipeline> create();

        private:
            void restart_stream();
            void callback_handle_strategy(webserver::resources::ResourceStateChangeNotification notif);
            static nlohmann::json create_encoder_osd_config(nlohmann::json osd_config, nlohmann::json encoder_config);
        };

        class DummyPipeline : public IPipeline
        {
        public:
            DummyPipeline(WebserverResourceRepository resources, std::string gst_pipeline_str);
            static std::shared_ptr<DummyPipeline> create();

        private:
            void callback_handle_strategy(webserver::resources::ResourceStateChangeNotification notif);
        };
    }
}

typedef std::shared_ptr<webserver::pipeline::IPipeline> WebServerPipeline;
