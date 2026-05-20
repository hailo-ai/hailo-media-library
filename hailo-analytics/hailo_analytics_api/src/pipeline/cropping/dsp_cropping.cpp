#include <hailodsp.h>
#include <hailodsp_base.h>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <media_library/buffer_pool.hpp>
#include <media_library/media_library_buffer.hpp>
#include <media_library/media_library_types.hpp>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/cropping/dsp_cropping.hpp"
#include "media_library/dsp_utils.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"

namespace hailo_analytics::pipeline::cropping
{

DspBaseCropStage::DspBaseCropStage(std::string name, int output_pool_size, int input_width, int input_height,
                                   int output_width, int output_height, std::string main_sub_name,
                                   std::string sub_sub_name, size_t queue_size, bool leaky,
                                   bool trace_processing_operations, StagePoolMode pool_mode,
                                   size_t crop_every_x_frames, dsp_scaling_mode_t scaling_mode,
                                   dsp_color_t letterbox_color, bool release_input_after_dsp)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, leaky, trace_processing_operations),
      m_output_pool_size(output_pool_size), m_input_width(input_width), m_input_height(input_height),
      m_output_width(output_width), m_output_height(output_height), m_main_subscriber(main_sub_name),
      m_sub_subscriber(sub_sub_name), m_pool_mode(pool_mode), m_crop_every_x_frames(crop_every_x_frames),
      m_frame_counter(0), m_scaling_mode(scaling_mode), m_letterbox_color(letterbox_color),
      m_release_input_after_dsp(release_input_after_dsp)
{
}

AppStatus DspBaseCropStage::init()
{
    dsp_status status = dsp_utils::acquire_device();
    if (status != DSP_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("DspBaseCropStage: Failed to acquire DSP device, status={}", status);
        return AppStatus::CONFIGURATION_ERROR;
    }
    return AppStatus::SUCCESS;
}

void DspBaseCropStage::set_crop_every_x_frames(int crop_every_x_frames)
{
    HAILO_ANALYTICS_LOG_INFO("Stage {}: updating crop_every_x_frames from {} to {}", get_name(), m_crop_every_x_frames,
                             crop_every_x_frames);
    m_crop_every_x_frames = crop_every_x_frames;
    m_frame_counter = 0;
}

void DspBaseCropStage::setup_pool_notification()
{
    if (m_buffer_pool && m_pool_mode == StagePoolMode::BLOCKING)
    {
        m_buffer_pool->set_on_release_callback([this](void * /*unused*/) { m_available_buffers_cv.notify_all(); });
    }
}

void DspBaseCropStage::prepare_single_crop_dim(HailoBBox bbox, std::vector<dsp_crop_api_t> &crop_resize_dims,
                                               int input_width, int input_height)
{
    dsp_crop_api_t crop_resize_dim = {
        .start_x = (size_t)std::clamp((bbox.xmin() * input_width), (float)0.0, ((float)input_width) - (float)1.0),
        .start_y = (size_t)std::clamp((bbox.ymin() * input_height), (float)0.0, ((float)input_height) - (float)1.0),
        .end_x = (size_t)std::clamp(((bbox.xmin() * input_width) + (bbox.width() * input_width)), (float)1.0,
                                    (float)input_width),
        .end_y = (size_t)std::clamp(((bbox.ymin() * input_height) + (bbox.height() * input_height)), (float)1.0,
                                    (float)input_height),
    };

    /* DSP API can't get dimension that are not even */
    if (crop_resize_dim.start_x % 2 != 0)
        crop_resize_dim.start_x += 1;

    if (crop_resize_dim.start_y % 2 != 0)
        crop_resize_dim.start_y += 1;

    if (crop_resize_dim.end_x % 2 != 0)
        crop_resize_dim.end_x += 1;

    if (crop_resize_dim.end_y % 2 != 0)
        crop_resize_dim.end_y += 1;

    // The DSP also caps downscale at the same ratio, but hitting that would mean a
    // model input that is many times smaller than the source crop - which no model we use does.
    const size_t min_src_w = (m_output_width + DSP_MAX_RESIZE_RATIO - 1) / DSP_MAX_RESIZE_RATIO;
    const size_t min_src_h = (m_output_height + DSP_MAX_RESIZE_RATIO - 1) / DSP_MAX_RESIZE_RATIO;
    const size_t crop_w = crop_resize_dim.end_x - crop_resize_dim.start_x;
    const size_t crop_h = crop_resize_dim.end_y - crop_resize_dim.start_y;
    if (crop_w < min_src_w || crop_h < min_src_h)
    {
        HAILO_ANALYTICS_LOG_DEBUG("{}: skipping narrow crop ({}x{}) below DSP threshold ({}x{})", m_stage_name, crop_w,
                                  crop_h, min_src_w, min_src_h);
        return;
    }

    crop_resize_dims.push_back(crop_resize_dim);
}

AppStatus DspBaseCropStage::process(BufferPtr data)
{
    std::chrono::steady_clock::time_point total_begin = std::chrono::steady_clock::now();

    // Check if we're cropping every x frames
    m_frame_counter++;
    if (m_crop_every_x_frames > 1 && (m_frame_counter % m_crop_every_x_frames) != 0)
    {
        CroppingMetadataPtr cropping_meta = std::make_shared<CroppingMetadata>(0);
        data->add_metadata(cropping_meta);
        send_to_specific_subscriber(m_main_subscriber, data);
        return AppStatus::SUCCESS;
    }
    if (m_frame_counter == m_crop_every_x_frames)
        m_frame_counter = 0;

    dsp_status status;

    std::chrono::steady_clock::time_point prep_begin = std::chrono::steady_clock::now();
    std::vector<dsp_crop_resize_params_t> crops_params;
    std::vector<dsp_crop_api_t> crop_resize_dims;
    std::vector<HailoMediaLibraryBufferPtr> cropped_buffers;
    std::vector<hailo_dsp_buffer_data_t> output_dsp_buffers;
    prepare_crops(data, crop_resize_dims);
    std::chrono::steady_clock::time_point prep_end = std::chrono::steady_clock::now();

    crops_params.reserve(crop_resize_dims.size());
    output_dsp_buffers.reserve(crop_resize_dims.size());

    std::chrono::steady_clock::time_point buffer_acq_begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < crop_resize_dims.size(); ++i)
    {
        // Check buffer availability BEFORE acquiring to avoid error logs from the pool
        if (m_buffer_pool->get_available_buffers_count() <= 1)
        {
            if (m_pool_mode == StagePoolMode::USE_AVAILABLE_BUFFERS)
            {
                HAILO_ANALYTICS_LOG_WARN("{} Not enough buffers! Requested: {}, Acquired: {}", m_stage_name,
                                         crop_resize_dims.size(), cropped_buffers.size());
                break;
            }
            else if (m_pool_mode == StagePoolMode::BLOCKING)
            {
                std::unique_lock<std::mutex> lock(m_buff_pool_mutex);
                m_available_buffers_cv.wait(lock,
                                            [this]() { return m_buffer_pool->get_available_buffers_count() > 1; });
            }
            else if (m_pool_mode == StagePoolMode::LEAKY)
            {
                for (auto &buffer : cropped_buffers)
                {
                    buffer.reset();
                }
                return AppStatus::SUCCESS;
            }
            else
            {
                /* FAIL_ON_EMPTY_POOL */
                for (auto &buffer : cropped_buffers)
                {
                    buffer.reset();
                }
                return AppStatus::BUFFER_ALLOCATION_ERROR;
            }
        }

        auto &dims = crop_resize_dims[i];

        HailoMediaLibraryBufferPtr cropped_buffer = std::make_shared<hailo_media_library_buffer>();
        if (m_buffer_pool->acquire_buffer(cropped_buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            HAILO_ANALYTICS_LOG_WARN("{} Buffer acquire failed unexpectedly after availability check", m_stage_name);
            break;
        }

        cropped_buffer->copy_metadata_from(data->get_buffer());

        output_dsp_buffers.emplace_back(std::move(cropped_buffer->buffer_data->As<hailo_dsp_buffer_data_t>()));
        dsp_crop_resize_params_t crop_resize_params{};
        crop_resize_params.crop = &dims;
        crop_resize_params.dst[0] = &output_dsp_buffers[i].properties;

        // Configure scaling mode (letterbox or stretch) for batched operation
        crop_resize_params.scaling_params[0].scaling_mode = m_scaling_mode;
        crop_resize_params.scaling_params[0].color = m_letterbox_color;

        crops_params.emplace_back(std::move(crop_resize_params));
        cropped_buffers.emplace_back(cropped_buffer);
    }
    std::chrono::steady_clock::time_point buffer_acq_end = std::chrono::steady_clock::now();

    // If no buffers were acquired, skip cropping and forward the frame
    if (cropped_buffers.empty())
    {
        CroppingMetadataPtr cropping_meta = std::make_shared<CroppingMetadata>(0);
        data->add_metadata(cropping_meta);
        send_to_specific_subscriber(m_main_subscriber, data);
        post_crop(data);
        return AppStatus::SUCCESS;
    }

    std::chrono::steady_clock::time_point dsp_begin = std::chrono::steady_clock::now();
    HailoMediaLibraryBufferPtr input_buffer = data->get_buffer();
    hailo_dsp_buffer_data_t in_buffer_data = input_buffer->buffer_data->As<hailo_dsp_buffer_data_t>();
    dsp_multi_crop_resize_params_t multi_crop_resize_params = {
        .src = &in_buffer_data.properties,
        .crop_resize_params = crops_params.data(),
        .crop_resize_params_count = cropped_buffers.size(),
        .interpolation = INTERPOLATION_TYPE_BILINEAR,
    };

    status = dsp_utils::perform_dsp_telescopic_multi_resize(&multi_crop_resize_params);
    std::chrono::steady_clock::time_point dsp_end = std::chrono::steady_clock::now();

    if (status != DSP_SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to perform dsp multi resize on stage {}", m_stage_name);
        return AppStatus::DSP_OPERATION_ERROR;
    }

    // Release the input frame buffer when downstream stages only consume metadata
    if (m_release_input_after_dsp)
    {
        input_buffer.reset();
        data->release_frame_data();
    }

    std::chrono::steady_clock::time_point send_begin = std::chrono::steady_clock::now();
    CroppingMetadataPtr cropping_meta = std::make_shared<CroppingMetadata>(cropped_buffers.size());
    data->add_metadata(cropping_meta);
    send_to_specific_subscriber(m_main_subscriber, data);

    for (std::size_t i = 0; i < cropped_buffers.size(); ++i)
    {
        HailoROIPtr roi = get_crop_roi(i);
        BufferPtr cropped_buffer_ptr = std::make_shared<Buffer>(cropped_buffers[i], roi);
        BatchMetadataPtr batch_meta = std::make_shared<BatchMetadata>(cropped_buffers.size(), i);
        cropped_buffer_ptr->add_metadata(batch_meta);

        // Set the ROI of the cropped buffer to the scale of the parent ROI
        // Note, this will make overlay incorrect if the bboxes are not flattened
        cropped_buffer_ptr->get_roi()->set_scaling_bbox(get_crop_bbox(i));
        cropped_buffer_ptr->get_buffer()->isp_timestamp_ns = data->get_buffer()->isp_timestamp_ns;

        send_to_specific_subscriber(m_sub_subscriber, cropped_buffer_ptr);
    }

    post_crop(data);
    std::chrono::steady_clock::time_point send_end = std::chrono::steady_clock::now();

    if (m_trace_processing_operations)
    {
        auto prep_time = std::chrono::duration_cast<std::chrono::microseconds>(prep_end - prep_begin).count();
        auto buffer_time =
            std::chrono::duration_cast<std::chrono::microseconds>(buffer_acq_end - buffer_acq_begin).count();
        auto dsp_time = std::chrono::duration_cast<std::chrono::microseconds>(dsp_end - dsp_begin).count();
        auto send_time = std::chrono::duration_cast<std::chrono::microseconds>(send_end - send_begin).count();
        auto total_time = std::chrono::duration_cast<std::chrono::microseconds>(send_end - total_begin).count();

        HAILO_ANALYTICS_LOG_TRACE("{} TIMING [crops={}]: Total={}us | Prep={}us | BufAcq={}us | DSP={}us | Send={}us",
                                  m_stage_name, cropped_buffers.size(), total_time, prep_time, buffer_time, dsp_time,
                                  send_time);
    }

    return AppStatus::SUCCESS;
}

} // namespace hailo_analytics::pipeline::cropping
