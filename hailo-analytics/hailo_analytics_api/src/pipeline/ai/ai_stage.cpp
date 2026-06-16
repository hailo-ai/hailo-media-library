#include <hailort.h>
#include <stdint.h>
#include <sys/types.h>
#include <buffer.hpp>
#include <expected.hpp>
#include <hailo_gst_tensor_metadata.hpp>
#include <hailo_postprocess_tools/objects/hailo_objects.hpp>
#include <hailo_postprocess_tools/objects/hailo_tensors.hpp>
#include <hef.hpp>
#include <infer_model.hpp>
#include <media_library/buffer_pool.hpp>
#include <media_library/media_library_buffer.hpp>
#include <media_library/media_library_types.hpp>
#include <vdevice.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hailo_analytics/logger/hailo_analytics_logger.hpp"
#include "hailo_analytics/pipeline/ai/ai_stage.hpp"
#include "hailo_analytics/pipeline/core/error_utils.hpp"
#include "hailo_analytics/pipeline/core/buffer.hpp"
#include "hailo_analytics/pipeline/core/queue.hpp"
#include "hailo_analytics/pipeline/core/stage.hpp"
#include "hailo_analytics/pipeline/core/stage_tracing.hpp"

namespace hailo_analytics::pipeline::ai
{

HailortAsyncStage::HailortAsyncStage(std::string name, std::string hef_path, size_t queue_size, int output_pool_size,
                                     std::string group_id, int batch_size, size_t job_limit, int scheduler_threshold,
                                     bool dynamic_threshold, std::chrono::milliseconds scheduler_timeout,
                                     StagePoolMode pool_mode, float32_t nms_score_threshold,
                                     std::vector<bool> nms_classes_filter_mask,
                                     size_t nms_max_accumulated_mask_size_multiplier, bool trace_processing_operations,
                                     bool use_hailort_service, uint8_t scheduler_priority)
    : hailo_analytics::pipeline::ThreadedStage(name, queue_size, false, trace_processing_operations),
      m_output_pool_size(output_pool_size), m_hef_path(hef_path), m_group_id(group_id),
      m_use_hailort_service(use_hailort_service), m_batch_size(batch_size), m_scheduler_threshold(scheduler_threshold),
      m_dynamic_threshold(dynamic_threshold), m_nms_score_threshold(nms_score_threshold),
      m_nms_classes_filter_mask(std::move(nms_classes_filter_mask)),
      m_nms_max_accumulated_mask_size_multiplier(nms_max_accumulated_mask_size_multiplier),
      m_scheduler_timeout(scheduler_timeout), m_scheduler_priority(scheduler_priority), m_jobs_limit(job_limit),
      m_pool_mode(pool_mode)
{
    m_last_infer_job = nullptr;
    m_active_jobs = 0;
}

AppStatus HailortAsyncStage::init()
{
    hailo_vdevice_params_t vdevice_params{};
    hailo_init_vdevice_params(&vdevice_params);
    vdevice_params.group_id = m_group_id.c_str();
    vdevice_params.multi_process_service = m_use_hailort_service;
    HAILO_ANALYTICS_LOG_INFO(
        "Initializing HailortAsyncStage with hef_path={}, group_id={}, batch_size={}, scheduler_threshold={}, "
        "dynamic_threshold={}, nms_score_threshold={}, nms_max_accumulated_mask_size_multiplier={}, "
        "scheduler_timeout={}ms, scheduler_priority={}, use_hailort_service={}",
        m_hef_path, m_group_id, m_batch_size, m_scheduler_threshold, m_dynamic_threshold, m_nms_score_threshold,
        m_nms_max_accumulated_mask_size_multiplier, m_scheduler_timeout.count(), m_scheduler_priority,
        m_use_hailort_service);
    // Create a vdevice
    auto vdevice_exp = hailort::VDevice::create(vdevice_params);
    if (!vdevice_exp)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed create vdevice, Hailort status = {}", vdevice_exp.status());
        return AppStatus::HAILORT_ERROR;
    }
    m_vdevice = vdevice_exp.release();

    // Create an infer model
    auto infer_model_exp = m_vdevice->create_infer_model(m_hef_path.c_str());
    if (!infer_model_exp)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create infer model, Hailort status = {}", infer_model_exp.status());
        return AppStatus::HAILORT_ERROR;
    }
    m_infer_model = infer_model_exp.release();
    m_infer_model->set_batch_size(m_batch_size);

    for (auto &output : m_infer_model->outputs())
    {
        auto infer_stream = m_infer_model->output(output.name()).expect("Failed to get output tensor");
        if (infer_stream.is_nms() && m_nms_score_threshold > 0.0f)
        {
            infer_stream.set_nms_score_threshold(m_nms_score_threshold);
        }
        if (infer_stream.is_nms() && !m_nms_classes_filter_mask.empty())
        {
            infer_stream.set_nms_classes_filter_mask(m_nms_classes_filter_mask);
        }
        // Set NMS max accumulated mask size if multiplier is specified
        if (m_nms_max_accumulated_mask_size_multiplier > 0)
        {
            infer_stream.set_nms_max_accumulated_mask_size(output.get_frame_size() *
                                                           m_nms_max_accumulated_mask_size_multiplier);
        }
    }

    // Configure the infer model
    auto configured_infer_model_exp = m_infer_model->configure();
    if (!configured_infer_model_exp)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create configured infer model, Hailort status = {}",
                                  configured_infer_model_exp.status());
        return AppStatus::HAILORT_ERROR;
    }
    m_configured_infer_model = configured_infer_model_exp.release();
    m_configured_infer_model.set_scheduler_threshold(m_scheduler_threshold);
    m_configured_infer_model.set_scheduler_timeout(std::chrono::milliseconds(m_scheduler_timeout));
    m_configured_infer_model.set_scheduler_priority(m_scheduler_priority);

    // Create bindings through which to connect buffers for inference
    auto bindings = m_configured_infer_model.create_bindings();
    if (!bindings)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to create infer bindings, Hailort status = {}", bindings.status());
        return AppStatus::HAILORT_ERROR;
    }
    m_bindings = bindings.release();

    // Prepare a buffer pool for each output tensor
    for (auto &output : m_infer_model->outputs())
    {
        size_t tensor_size = output.get_frame_size();
        std::string tensor_name = m_stage_name + "/" + output.name();
        m_tensor_buffer_pools[output.name()] = std::make_shared<MediaLibraryBufferPool>(
            tensor_size, 1, HAILO_FORMAT_GRAY8, m_output_pool_size, HAILO_MEMORY_TYPE_DMABUF, tensor_size, tensor_name);
        if (m_tensor_buffer_pools[output.name()]->init() != MEDIA_LIBRARY_SUCCESS)
        {
            return AppStatus::BUFFER_ALLOCATION_ERROR;
        }
    }

    for (auto &[tensor_name, pool] : m_tensor_buffer_pools)
    {
        if (pool->wait_for_all_buffers_allocated() != MEDIA_LIBRARY_SUCCESS)
        {
            return AppStatus::BUFFER_ALLOCATION_ERROR;
        }
    }

    setup_pool_notifications();

    // Gather the vstream info for each output tensor
    auto vstream_infos = m_infer_model->hef().get_output_vstream_infos();
    if (!vstream_infos)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to get vstream info, Hailort status = {}", vstream_infos.status());
        return AppStatus::HAILORT_ERROR;
    }
    for (const auto &vstream_info : vstream_infos.value())
    {
        m_vstream_infos[vstream_info.name] = vstream_info;
    }

    return AppStatus::SUCCESS;
}

void HailortAsyncStage::setup_pool_notifications()
{
    if (m_pool_mode == StagePoolMode::BLOCKING)
    {
        for (auto &[tensor_name, pool] : m_tensor_buffer_pools)
        {
            pool->set_on_release_callback([this](void * /*unused*/) { m_available_buffers_cv.notify_all(); });
        }
    }
}

void HailortAsyncStage::on_end_of_stream()
{
    {
        std::lock_guard<std::mutex> lock(m_active_jobs_mutex);
        m_active_jobs_cv.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(m_buff_pool_mutex);
        m_available_buffers_cv.notify_all();
    }
}

AppStatus HailortAsyncStage::deinit()
{
    HAILO_ANALYTICS_LOG_INFO("[{}] Waiting for {} active jobs to complete", m_stage_name, m_active_jobs.load());
    std::unique_lock<std::mutex> lock(m_active_jobs_mutex);
    auto timeout = std::chrono::milliseconds(10000);
    bool all_jobs_finished = m_active_jobs_cv.wait_for(lock, timeout, [this] { return m_active_jobs == 0; });

    if (!all_jobs_finished)
    {
        HAILO_ANALYTICS_LOG_ERROR("[{}] Timeout waiting for {} active jobs to complete", m_stage_name,
                                  m_active_jobs.load());
    }

    // Wait for last infer to finish (additional safety)
    if (m_last_infer_job)
    {
        auto status = m_last_infer_job->wait(std::chrono::milliseconds(5000));
        if (HAILO_SUCCESS != status)
        {
            HAILO_ANALYTICS_LOG_ERROR("Failed to wait for infer to finish, status = {}", status);
            return AppStatus::HAILORT_ERROR;
        }
    }
    for (auto &queue : m_queues)
    {
        queue->flush();
    }

    return AppStatus::SUCCESS;
}

AppStatus HailortAsyncStage::set_pix_buf(const HailoMediaLibraryBufferPtr buffer)
{
    int y_plane_fd = buffer->get_plane_fd(0);
    uint32_t y_plane_size = buffer->get_plane_size(0);

    int uv_plane_fd = buffer->get_plane_fd(1);
    uint32_t uv_plane_size = buffer->get_plane_size(1);

    hailo_pix_buffer_t pix_buffer{};
    pix_buffer.memory_type = HAILO_PIX_BUFFER_MEMORY_TYPE_DMABUF;
    pix_buffer.number_of_planes = 2;
    pix_buffer.planes[0].bytes_used = y_plane_size;
    pix_buffer.planes[0].plane_size = y_plane_size;
    pix_buffer.planes[0].fd = y_plane_fd;

    pix_buffer.planes[1].bytes_used = uv_plane_size;
    pix_buffer.planes[1].plane_size = uv_plane_size;
    pix_buffer.planes[1].fd = uv_plane_fd;

    auto status = m_bindings.input()->set_pix_buffer(pix_buffer);
    if (HAILO_SUCCESS != status)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to set infer input buffer, Hailort status = {}", status);
        return AppStatus::HAILORT_ERROR;
    }

    return AppStatus::SUCCESS;
}

AppStatus HailortAsyncStage::acquire_and_set_tensor_buffers(std::unordered_map<std::string, BufferPtr> &tensor_buffers)
{
    // Acquire a buffer for each output tensor
    for (auto &output : m_infer_model->outputs())
    {
        auto &pool = m_tensor_buffer_pools[output.name()];

        // Check buffer availability BEFORE acquiring to avoid error logs from the pool
        if (pool->get_available_buffers_count() == 0)
        {
            if (m_pool_mode == StagePoolMode::FAIL_ON_EMPTY_POOL)
            {
                HAILO_ANALYTICS_LOG_WARN("{} Not enough buffers in pool for tensor {}! Requested: {}, Available: {}",
                                         m_stage_name, output.name(), 1, pool->get_available_buffers_count());
                return AppStatus::BUFFER_ALLOCATION_ERROR;
            }
            else if (m_pool_mode == StagePoolMode::BLOCKING)
            {
                HAILO_ANALYTICS_LOG_INFO("{} no available buffers in pool for tensor {}, waiting...", m_stage_name,
                                         output.name());
                // Wait for a free tensor buffer or end-of-stream; m_available_buffers_cv is notified
                // on release and, at shutdown, by on_end_of_stream() (MSW-16172).
                std::unique_lock<std::mutex> lock(m_buff_pool_mutex);
                m_available_buffers_cv.wait(
                    lock, [this, &pool]() { return m_end_of_stream || pool->get_available_buffers_count() >= 1; });
                if (m_end_of_stream)
                {
                    for (auto &buffer : tensor_buffers)
                    {
                        buffer.second.reset();
                    }
                    return AppStatus::SUCCESS;
                }
            }
            else
            {
                // LEAKY or any other mode: drop frame
                for (auto &buffer : tensor_buffers)
                {
                    buffer.second.reset();
                }
                return AppStatus::SUCCESS;
            }
        }

        // Acquire a buffer for this tensor output from the corresponding buffer pool
        HailoMediaLibraryBufferPtr tensor_buffer = std::make_shared<hailo_media_library_buffer>();
        if (pool->acquire_buffer(tensor_buffer) != MEDIA_LIBRARY_SUCCESS)
        {
            HAILO_ANALYTICS_LOG_WARN("{} buffer acquire failed unexpectedly after availability check for tensor {}",
                                     m_stage_name, output.name());
            return AppStatus::BUFFER_ALLOCATION_ERROR;
        }

        // Set entry in map
        BufferPtr tensor_buffer_ptr = std::make_shared<Buffer>(tensor_buffer);
        tensor_buffers[output.name()] = tensor_buffer_ptr;

        // Set the HailoRT bindings for the acquired buffer
        size_t tensor_size = output.get_frame_size();
        auto status = m_bindings.output(output.name())
                          ->set_buffer(hailort::MemoryView(tensor_buffer->get_plane_ptr(0), tensor_size));
        if (HAILO_SUCCESS != status)
        {
            HAILO_ANALYTICS_LOG_ERROR("{} failed to set infer output buffer {} , Hailort status = ", m_stage_name,
                                      output.name(), status);
            return AppStatus::HAILORT_ERROR;
        }
    }
    return AppStatus::SUCCESS;
}

AppStatus HailortAsyncStage::infer(BufferPtr input_buffer,
                                   const std::unordered_map<std::string, BufferPtr> &tensor_buffers)
{
    // wait for infer model to be ready
    auto status = m_configured_infer_model.wait_for_async_ready(std::chrono::milliseconds(1000));
    if (HAILO_SUCCESS != status)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to wait for async ready, Hailort status = {}", status);
        return AppStatus::HAILORT_ERROR;
    }

    // Run the async infer api, when inference is done it will call the given callback
    auto job = m_configured_infer_model.run_async(
        m_bindings, [tensor_buffers, input_buffer, this](const hailort::AsyncInferCompletionInfo &completion_info) {
            // active job finished
            std::unique_lock<std::mutex> lock(m_active_jobs_mutex);
            --this->m_active_jobs;
            m_active_jobs_cv.notify_one();
            inference_tracing_end(input_buffer);

            // check infer status
            if (completion_info.status != HAILO_SUCCESS)
            {
                HAILO_ANALYTICS_LOG_ERROR("Failed to run async infer, Hailort status = {}", completion_info.status);
                return AppStatus::HAILORT_ERROR;
            }

            if (m_end_of_stream)
                return AppStatus::SUCCESS;

            // Add metadata for each output tensor buffer
            for (auto &output : m_infer_model->outputs())
            {
                BufferPtr tensor_buffer = tensor_buffers.at(output.name());
                TensorMetadataPtr tensor_metadata = std::make_shared<TensorMetadata>(tensor_buffer, output.name());
                input_buffer->add_metadata(tensor_metadata);

                // Add the vstream info and data pointer to the HailoRoi for later use (postprocessing)
                input_buffer->get_roi()->add_tensor(std::make_shared<HailoTensor>(
                    reinterpret_cast<uint8_t *>(tensor_buffer->get_buffer()->get_plane_ptr(0)),
                    m_vstream_infos[output.name()]));
            }

            // Send the input buffer to the next stage
            send_to_subscribers(input_buffer);

            return AppStatus::SUCCESS;
        });
    ++m_active_jobs;

    if (!job)
    {
        HAILO_ANALYTICS_LOG_ERROR("Failed to start async infer job, status = {}", job.status());
        return AppStatus::HAILORT_ERROR;
    }

    // detach the job to run in it's own thread on the side
    job->detach();
    m_last_infer_job = std::make_shared<hailort::AsyncInferJob>(job.release());

    return AppStatus::SUCCESS;
}

AppStatus HailortAsyncStage::process(BufferPtr data)
{
    HAILO_ANALYTICS_LOG_DEBUG("[{}] process() called", m_stage_name);
    inference_tracing_begin(data);

    // Wait and set scheduler threshold if dynamic thresholding used
    if (m_dynamic_threshold)
    {
        std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::BATCH);
        if (metadata.size() > 0)
        {
            BatchMetadataPtr batch_metadata = std::dynamic_pointer_cast<BatchMetadata>(metadata[0]);

            // if this is the start of a new batch, wait for the last infer job to finish
            // or if this is the start of a leftover batch (index > batch size)
            if (batch_metadata->get_index() == 0 || ((int)batch_metadata->get_index() >= m_batch_size &&
                                                     (int)batch_metadata->get_index() % m_batch_size == 0))
            {
                HAILO_ANALYTICS_LOG_DEBUG("[{}] Waiting for active jobs to finish before new batch", m_stage_name);
                std::unique_lock<std::mutex> lock(m_active_jobs_mutex);
                m_active_jobs_cv.wait(lock, [this] { return m_active_jobs == 0 || m_end_of_stream; });

                if (m_end_of_stream)
                {
                    HAILO_ANALYTICS_LOG_INFO("[{}] End of stream detected during batch start", m_stage_name);
                    inference_tracing_end(data);

                    return AppStatus::SUCCESS;
                }
                // Dynamic scheduling threshold - set the scheduler threshold to the current size of batch
                // (described by the batch metadata)
                if (batch_metadata->get_total_size() <= (uint)m_batch_size)
                {
                    m_configured_infer_model.set_scheduler_threshold(batch_metadata->get_total_size());
                }
                else if ((int)batch_metadata->get_index() >= m_batch_size)
                {
                    int remainder = batch_metadata->get_total_size() - batch_metadata->get_index();
                    int new_threshold = std::min(remainder, m_batch_size);
                    m_configured_infer_model.set_scheduler_threshold(new_threshold);
                }
                else
                {
                    m_configured_infer_model.set_scheduler_threshold(m_batch_size);
                }
            }
        }
    }

    // wait for available jobs
    HAILO_ANALYTICS_LOG_DEBUG("[{}] Waiting for available job slot (active_jobs: {}, jobs_limit: {})", m_stage_name,
                              m_active_jobs.load(), m_jobs_limit);
    // Wait for a free job slot or end-of-stream; m_active_jobs_cv is notified on job completion
    // and, at shutdown, by on_end_of_stream() (MSW-16172).
    std::unique_lock<std::mutex> lock(m_active_jobs_mutex);
    m_active_jobs_cv.wait(lock, [this] { return m_active_jobs < m_jobs_limit || m_end_of_stream; });
    if (m_end_of_stream)
    {
        inference_tracing_end(data);
        return AppStatus::SUCCESS;
    }

    // Set the input buffer
    HAILO_ANALYTICS_LOG_DEBUG("[{}] Setting pixel buffer for inference", m_stage_name);
    if (set_pix_buf(data->get_buffer()) != AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("[{}] Failed to set pixel buffer", m_stage_name);
        inference_tracing_end(data);

        return AppStatus::HAILORT_ERROR;
    }

    // Acquire and set tensor buffers
    HAILO_ANALYTICS_LOG_DEBUG("[{}] Acquiring and setting tensor buffers", m_stage_name);
    std::unordered_map<std::string, BufferPtr> tensor_buffers;
    if (acquire_and_set_tensor_buffers(tensor_buffers) != AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("[{}] Failed to acquire/set tensor buffers", m_stage_name);
        inference_tracing_end(data);

        return AppStatus::HAILORT_ERROR;
    }

    // Run the inference
    HAILO_ANALYTICS_LOG_DEBUG("[{}] Running inference", m_stage_name);
    if (infer(data, tensor_buffers) != AppStatus::SUCCESS)
    {
        HAILO_ANALYTICS_LOG_ERROR("[{}] Inference failed", m_stage_name);
        inference_tracing_end(data);

        return AppStatus::HAILORT_ERROR;
    }

    HAILO_ANALYTICS_LOG_DEBUG("[{}] process() completed successfully", m_stage_name);
    return AppStatus::SUCCESS;
}

std::string HailortAsyncStage::generate_unique_timestamp_str(BufferPtr data)
{
    // NOTE: We modified Perfetto. Originally, it ignored all numbers in event names, which made it impossible
    // to distinguish between events with the same name. We now use `isp_timestamp_ns` to create a unique string
    // The trick is that if the number is wrapped in curly braces, Perfetto will not ignore it.
    return m_stage_name + "{" + std::to_string(data->get_buffer()->isp_timestamp_ns) + "}";
}

uint64_t HailortAsyncStage::get_unique_buffer_identifier(BufferPtr data)
{
    uint64_t isp_timestamp_ns = data->get_buffer()->isp_timestamp_ns;
    size_t batch_index = 0;

    std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::BATCH);
    if (metadata.size() > 0)
    {
        BatchMetadataPtr batch_metadata = std::dynamic_pointer_cast<BatchMetadata>(metadata[0]);
        if (batch_metadata != nullptr)
        {
            batch_index = batch_metadata->get_index();
        }
    }

    uint64_t unique_id = isp_timestamp_ns + batch_index;
    return unique_id;
}

void HailortAsyncStage::inference_tracing_begin(BufferPtr data)
{
    std::string timestamp_str = generate_unique_timestamp_str(data);
    uint64_t unique_id = get_unique_buffer_identifier(data);
    m_tracing->trace_async_event_begin(unique_id, timestamp_str.c_str());
}

void HailortAsyncStage::inference_tracing_end(BufferPtr data)
{
    uint64_t unique_id = get_unique_buffer_identifier(data);
    m_tracing->trace_async_event_end(unique_id);
}

// Builder implementation
HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_stage_name(std::string name)
{
    m_stage_name = name;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_hef_path(std::string path)
{
    m_hef_path = path;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_queue_size(size_t size)
{
    m_queue_size = size;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_output_pool_size(int size)
{
    m_output_pool_size = size;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_group_id(std::string id)
{
    m_group_id = id;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_batch_size(int size)
{
    m_batch_size = size;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_job_limit(size_t size)
{
    m_job_limit = size;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_scheduler_threshold_opt(int threshold)
{
    m_scheduler_threshold = threshold;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_dynamic_threshold_opt(bool activate)
{
    m_dynamic_threshold = activate;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_scheduler_timeout_opt(
    std::chrono::milliseconds timeout)
{
    m_scheduler_timeout = timeout;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_scheduler_priority_opt(uint8_t priority)
{
    m_scheduler_priority = priority;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_pool_mode_opt(StagePoolMode mode)
{
    m_pool_mode = mode;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_nms_score_threshold(float32_t score_threshold)
{
    m_nms_score_threshold = score_threshold;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_nms_classes_filter_mask(std::vector<bool> mask)
{
    m_nms_classes_filter_mask = std::move(mask);
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_nms_max_accumulated_mask_size_multiplier(
    size_t multiplier)
{
    m_nms_max_accumulated_mask_size_multiplier = multiplier;
    return *this;
}
HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_trace_opt(bool activate)
{
    m_trace = activate;
    return *this;
}

HailortAsyncStageBuild::Builder &HailortAsyncStageBuild::Builder::set_use_hailort_service(bool use_service)
{
    m_use_hailort_service = use_service;
    return *this;
}

std::shared_ptr<HailortAsyncStage> HailortAsyncStageBuild::Builder::buildptr() const
{
    THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");
    THROW_IF_MISSING(m_hef_path.has_value(), "set_hef_path");
    THROW_IF_MISSING((m_output_pool_size > 0), "set_output_pool_size");
    THROW_IF_MISSING(m_group_id.has_value(), "set_group_id");
    THROW_IF_MISSING((m_batch_size >= 1), "set_batch_size");
    THROW_IF_MISSING((m_job_limit != 0), "set_job_limit");

    return std::make_shared<HailortAsyncStage>(
        m_stage_name.value(), m_hef_path.value(), m_queue_size, m_output_pool_size, m_group_id.value(), m_batch_size,
        m_job_limit, m_scheduler_threshold, m_dynamic_threshold, m_scheduler_timeout, m_pool_mode,
        m_nms_score_threshold, m_nms_classes_filter_mask, m_nms_max_accumulated_mask_size_multiplier, m_trace,
        m_use_hailort_service, m_scheduler_priority);
}

HailortAsyncStageBuild::Builder HailortAsyncStageBuild::create()
{
    return Builder();
}

} // namespace hailo_analytics::pipeline::ai
