#include "buffer_utils.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "infra/stages.hpp"
#include "infra/dsp_stages.hpp"
#include "infra/hailort_stage.hpp"
#include "infra/pipeline.hpp"
#include "user_stages.hpp"

#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <tl/expected.hpp>

#define FRONTEND_CONFIG_FILE "/usr/bin/frontend_native_config_example.json"
#define ENCODER_OSD_CONFIG_FILE(id) get_encoder_osd_config_file(id)
#define OUTPUT_FILE(id) get_output_file(id)
#define RUNTIME_SECONDS 60
#define BATCH_SIZE 4
#define HEF_FILE ("/home/root/apps/internals/frontend_pipelines/resources/yolov5m_wo_spp_60p_nv12_640.hef")

inline std::string get_encoder_osd_config_file(const std::string &id)
{
    return "/usr/bin/frontend_encoder_" + id + ".json";
}

inline std::string get_output_file(const std::string &id)
{
    return "/var/volatile/tmp/frontend_example_" + id + ".h264";
}

void write_encoded_data(HailoMediaLibraryBufferPtr buffer, uint32_t size, std::ofstream &output_file)
{
    char *data = (char *)buffer->get_plane(0);
    if (!data)
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    output_file.write(data, size);
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

void delete_output_file(std::string output_file)
{
    std::ofstream fp(output_file.c_str(), std::ios::out | std::ios::binary);
    if (!fp.good())
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
    fp.close();
}

struct AppResources
{
    MediaLibraryFrontendPtr frontend;
    std::map<output_stream_id_t, MediaLibraryEncoderPtr> encoders;
    std::map<output_stream_id_t, std::ofstream> output_files;
    std::unique_ptr<Pipeline> pipeline;
    std::shared_ptr<ProducableBufferStage> source_stage;
};

void subscribe_elements(std::shared_ptr<AppResources> app_resources)
{
    auto streams = app_resources->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    FrontendCallbacksMap fe_callbacks;

    for (auto s : streams.value())
    {
        std::cout << "subscribing to frontend for '" << s.id << "'" << std::endl;

        if (s.id == "sink0" || s.id == "sink1")
        {
            fe_callbacks[s.id] = [s, app_resources](HailoMediaLibraryBufferPtr media_lib_buffer, size_t size)
            {
                BufferPtr buffer = std::make_shared<Buffer>(false);
                buffer->add_media_lib_buffer(MediaLibraryBufferType::Unknown, std::move(media_lib_buffer));
                app_resources->source_stage->push(buffer);
            };
        }
        else 
        {
            fe_callbacks[s.id] = [s, app_resources](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                app_resources->encoders[s.id]->add_buffer(buffer);
                buffer->decrease_ref_count();
            };
        }
    }
    app_resources->frontend->subscribe(fe_callbacks);

    for (const auto &entry : app_resources->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;
        std::cout << "subscribing to encoder for '" << streamId << "'" << std::endl;
        app_resources->encoders[streamId]->subscribe(
            [app_resources, streamId](HailoMediaLibraryBufferPtr buffer, size_t size)
            {
                write_encoded_data(buffer, size, app_resources->output_files[streamId]);
                buffer->decrease_ref_count();
            });
    }
}

void create_encoder_and_output_file(const std::string& id, std::shared_ptr<AppResources> app_resources)
{
    if (id == "sink0" || id == "sink1")
    {
        return;
    }

    std::cout << "Creating encoder enc_" << id << std::endl;

    // Create and configure encoder
    std::string encoderosd_config_string = read_string_from_file(ENCODER_OSD_CONFIG_FILE(id).c_str());
    tl::expected<MediaLibraryEncoderPtr, media_library_return> encoder_expected = MediaLibraryEncoder::create(encoderosd_config_string, id);
    if (!encoder_expected.has_value())
    {
        std::cout << "Failed to create encoder osd" << std::endl;
        return;
    }
    app_resources->encoders[id] = encoder_expected.value();

    // create and configure output file
    std::string output_file_path = OUTPUT_FILE(id);
    delete_output_file(output_file_path);
    app_resources->output_files[id].open(output_file_path.c_str(), std::ios::out | std::ios::binary | std::ios::app);
    if (!app_resources->output_files[id].good())
    {
        std::cout << "Error occurred at writing time!" << std::endl;
        return;
    }
}

void stop(std::shared_ptr<AppResources> app_resources)
{
    std::cout << "Stopping." << std::endl;
    app_resources->frontend->stop();
    for (const auto &entry : app_resources->encoders)
    {
        entry.second->stop();
    }

    // close all file in media_lib->output_files
    for (auto &entry : app_resources->output_files)
    {
        entry.second.close();
    }

    app_resources->pipeline->stop_pipeline();
}

void configure_frontend(std::shared_ptr<AppResources> app_resources)
{
    std::string frontend_config_string = read_string_from_file(FRONTEND_CONFIG_FILE);
    tl::expected<MediaLibraryFrontendPtr, media_library_return> frontend_expected = MediaLibraryFrontend::create(FRONTEND_SRC_ELEMENT_V4L2SRC, frontend_config_string);
    if (!frontend_expected.has_value())
    {
        std::cout << "Failed to create frontend" << std::endl;
        return;
    }
    app_resources->frontend = frontend_expected.value();

    auto streams = app_resources->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    for (auto s : streams.value())
    {
        create_encoder_and_output_file(s.id, app_resources);
    }
}

void start_frontend(std::shared_ptr<AppResources> app_resources)
{
    for (const auto &entry : app_resources->encoders)
    {
        output_stream_id_t streamId = entry.first;
        MediaLibraryEncoderPtr encoder = entry.second;

        std::cout << "starting encoder for " << streamId << std::endl;
        encoder->start();
    }

    app_resources->pipeline->run_pipeline();
    app_resources->frontend->start();
}

void create_pipeline(std::shared_ptr<AppResources> app_resources)
{
    app_resources->pipeline = std::make_unique<Pipeline>();

    std::shared_ptr<FrontendAggregatorStage> frontend_aggregator_stage = std::make_shared<FrontendAggregatorStage>("frontend_agg", 5);
    std::shared_ptr<TillingCropStage> crop_stage = std::make_shared<TillingCropStage>("tilling_cropper", 5, 40);
    std::shared_ptr<HailortAsyncStage> ai_stage = std::make_shared<HailortAsyncStage>("hrt_detector", 10, 20, HEF_FILE, "0", BATCH_SIZE);
    std::shared_ptr<PostProcessStage> post_process_stage = std::make_shared<PostProcessStage>("post_process", 5 * BATCH_SIZE, 0.03);
    std::shared_ptr<BBoxCropStage> bbox_crop_stage = std::make_shared<BBoxCropStage>("bbox_crop", 5, 30, 640, 480);
    std::shared_ptr<DummyStage> dummy_stage = std::make_shared<DummyStage>("dummy_stage", 20);

    app_resources->source_stage = std::static_pointer_cast<ProducableBufferStage>(frontend_aggregator_stage);

    app_resources->pipeline->add_stage(frontend_aggregator_stage);
    app_resources->pipeline->add_stage(crop_stage);
    app_resources->pipeline->add_stage(ai_stage);
    app_resources->pipeline->add_stage(post_process_stage);
    app_resources->pipeline->add_stage(bbox_crop_stage);
    app_resources->pipeline->add_stage(dummy_stage);

    frontend_aggregator_stage->add_subscriber(crop_stage);
    crop_stage->add_subscriber(ai_stage);
    ai_stage->add_subscriber(post_process_stage);
    post_process_stage->add_subscriber(bbox_crop_stage);
    bbox_crop_stage->add_subscriber(dummy_stage);

    dummy_stage->set_print_fps(true);
    crop_stage->set_print_fps(true);
}

int main(int argc, char *argv[])
{
    std::shared_ptr<AppResources> app_resources = std::make_shared<AppResources>();

    create_pipeline(app_resources);
    configure_frontend(app_resources);
    subscribe_elements(app_resources);
    start_frontend(app_resources);

    std::cout << "Started playing for " << RUNTIME_SECONDS << " seconds." << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(RUNTIME_SECONDS));

    stop(app_resources);

    return 0;
}
