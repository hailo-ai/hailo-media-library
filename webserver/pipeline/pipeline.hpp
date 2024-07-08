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
            WebserverResourceRepository m_resources;
            GstElement *m_pipeline;
            virtual std::string create_gst_pipeline_string() = 0;

        public:
            IPipeline(WebserverResourceRepository resources);
            virtual ~IPipeline() = default;
            virtual void start();
            virtual void stop();
            virtual WebserverResourceRepository get_resources() { return m_resources; };

            static std::shared_ptr<IPipeline> create();
        };

        class Pipeline : public IPipeline
        {
        public:
            Pipeline(WebserverResourceRepository resources);
            static std::shared_ptr<Pipeline> create();

        protected:
            std::string create_gst_pipeline_string() override;

        private:
            void callback_handle_strategy(webserver::resources::ResourceStateChangeNotification notif);
        };

        class DummyPipeline : public IPipeline
        {
        public:
            DummyPipeline(WebserverResourceRepository resources);
            static std::shared_ptr<DummyPipeline> create();

        protected:
            std::string create_gst_pipeline_string() override;

        private:
            void callback_handle_strategy(webserver::resources::ResourceStateChangeNotification notif);
        };
    }
}

typedef std::shared_ptr<webserver::pipeline::IPipeline> WebServerPipeline;
