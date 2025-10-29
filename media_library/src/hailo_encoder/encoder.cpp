/*
 * Copyright (c) 2017-2024 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <thread>
#include <fstream>

#include "encoder_class.hpp"
#include "encoder_internal.hpp"
#include "media_library_logger.hpp"
#include "media_library_utils.hpp"
#include "snapshot.hpp"

#define MODULE_NAME LoggerType::Encoder

#define BITS_IN_BYTE 8

Encoder::Encoder(std::string json_string)
{
    m_impl = std::make_unique<Impl>(json_string);
}

Encoder::~Encoder() = default;

Encoder::Impl::~Impl()
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder - Destructor");
    release();
    dispose();
}

media_library_return Encoder::Impl::allocate_output_memory(HailoMediaLibraryBufferPtr buffer_ptr)
{
    i32 ret;
    if (NULL == m_ewl)
    {
        return MEDIA_LIBRARY_ERROR;
    }

    if (m_buffer_pool == nullptr)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "buffer pool not allocated");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }

    if (m_buffer_pool->acquire_buffer(buffer_ptr) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to acquire buffer");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }
    int planeFd = buffer_ptr->get_plane_fd(0);
    // Retrieve the physical address of the plane
    ret = EWLShareDmabuf(m_ewl, planeFd, &m_enc_in.busOutBuf);

    if (ret != EWL_OK)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Could not get physical address of plane {} planeFd {}", 0, planeFd);
        ret = EWLUnshareDmabuf(m_ewl, buffer_ptr->get_plane_fd(0));
        if (ret != EWL_OK)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Could not unshare buffer plane {} planeFd {}", 0, planeFd);
        }
        return MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS;
    }
    m_enc_in.outBufSize = (u32)buffer_ptr->get_plane_size(0);
    m_enc_in.pOutBuf = (u32 *)buffer_ptr->get_plane_ptr(0);
    m_enc_in.outBufFd = planeFd;
    return MEDIA_LIBRARY_SUCCESS;
}

tl::expected<EncoderOutputBuffer, media_library_return> Encoder::Impl::encode_executer(encoder_operation_t op)
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    VCEncRet encoder_ret_code;
    i32 unshare_ret_code;
    HailoMediaLibraryBufferPtr buffer_ptr = std::make_shared<hailo_media_library_buffer>();
    if (allocate_output_memory(buffer_ptr) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to allocate output memory");
        return tl::make_unexpected(MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR);
    }

    switch (op)
    {
    case encoder_operation_t::ENCODER_OPERATION_START: {
        encoder_ret_code = VCEncStrmStart(m_inst, &m_enc_in, &m_enc_out);
        if (VCENC_OK != encoder_ret_code)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start stream Encoder error {}", encoder_ret_code);
            ret = MEDIA_LIBRARY_ERROR;
        }
        break;
    }
    case encoder_operation_t::ENCODER_OPERATION_STOP: {
        encoder_ret_code = VCEncStrmEnd(m_inst, &m_enc_in, &m_enc_out);
        if (VCENC_OK != encoder_ret_code)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to stop stream Encoder error {}", encoder_ret_code);
            ret = MEDIA_LIBRARY_ERROR;
        }
        break;
    }
    case encoder_operation_t::ENCODER_OPERATION_ENCODE: {
        encoder_ret_code = VCEncStrmEncode(m_inst, &m_enc_in, &m_enc_out, NULL, NULL);
        if (VCENC_FRAME_READY != encoder_ret_code)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to encode stream Encoder error {}", encoder_ret_code);
            ret = MEDIA_LIBRARY_ERROR;
        }
        break;
    }
    default:
        LOGGER__MODULE__ERROR(MODULE_NAME, "Invalid encoder operation");
        ret = MEDIA_LIBRARY_ERROR;
    }
    unshare_ret_code = EWLUnshareDmabuf(m_ewl, buffer_ptr->get_plane_fd(0));
    if (unshare_ret_code != EWL_OK)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to unshare dmabuf");
        ret = MEDIA_LIBRARY_ERROR;
    }
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }

    EncoderOutputBuffer output_buffer;
    output_buffer.buffer = buffer_ptr;
    output_buffer.size = m_enc_out.streamSize;
    output_buffer.frame_type = m_enc_in.codingType;
    // Will be initalized later
    output_buffer.frame_number = -1;
    output_buffer.encoder_ret_code = encoder_ret_code;
    return output_buffer;
}

void Encoder::Impl::init_buffer_pool(uint pool_size)
{
    if (m_buffer_pool == nullptr)
    {
        std::string name = "encoder_output";
        m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(m_vc_cfg.width, m_vc_cfg.height, HAILO_FORMAT_GRAY8,
                                                                 (pool_size), HAILO_MEMORY_TYPE_DMABUF, name);
        if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder - init_buffer_pool - Failed to init buffer pool");
        }
    }
}

Encoder::Impl::Impl(std::string json_string) : m_config(std::make_unique<EncoderConfig>(json_string))
{
    m_state = ENCODER_STATE_UNINITIALIZED;
    m_previous_optical_zoom_magnification = 1.0f;
    m_zooming_boost_enabled = false;

    init();
}

media_library_return Encoder::Impl::dispose()
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    if (m_buffer_pool != nullptr)
    {
        ret = m_buffer_pool->free(false);
        m_buffer_pool.reset();
        m_buffer_pool = nullptr;
    }
    return ret;
}

media_library_return Encoder::Impl::release()
{
    if (m_state == ENCODER_STATE_UNINITIALIZED)
    {
        LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder - dispose requested - but it is already in uninitialized state");
        return MEDIA_LIBRARY_SUCCESS;
    }

    m_header.buffer = nullptr;
    VCEncRelease(m_inst);
    if (NULL != m_ewl)
        (void)EWLRelease((const void *)m_ewl);

    while (!m_bitrate_monitor.frame_sizes.empty())
        m_bitrate_monitor.frame_sizes.pop();

    if (m_bitrate_monitor.output_file.is_open())
    {
        m_bitrate_monitor.output_file.close();
    }

    if (m_cycle_monitor.output_file.is_open())
    {
        m_cycle_monitor.output_file.close();
    }

    m_state = ENCODER_STATE_UNINITIALIZED;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::release()
{
    return m_impl->release();
}

media_library_return Encoder::dispose()
{
    return m_impl->dispose();
}

media_library_return Encoder::init()
{
    return m_impl->init();
}

media_library_return Encoder::Impl::init()
{
    memset(&m_enc_out, 0, sizeof(VCEncOut));
    memset(&m_enc_in, 0, sizeof(VCEncIn));
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    m_multislice_encoding = false;
    m_is_encoding_multiple_frames = false;
    m_next_gop_size = 0;
    m_encoder_version = VCEncGetApiVersion();
    m_encoder_build = VCEncGetBuild();
    create_gop_config();
    if ((ret = init_gop_config()) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init gop config");
        return ret;
    }
    if (init_encoder_config() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init encoder config");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    init_buffer_pool(MAX_GOP_SIZE + 3);
    EWLInitParam_t ewl_params;
    ewl_params.clientType = EWL_CLIENT_TYPE_HEVC_ENC;
    m_ewl = (void *)EWLInit(&ewl_params);

    // Update timescale to be framerate denom (must happen after init_encoder_config)
    m_enc_in.timeIncrement = 0;
    m_enc_in.vui_timing_info_enable = 1;

    m_bitrate_monitor.enabled = true;
    if (m_vc_cfg.frameRateDenom == 0)
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Encoder - Frame rate denominator is 0");
        m_vc_cfg.frameRateDenom = 1;
    }
    m_bitrate_monitor.fps = m_vc_cfg.frameRateNum / m_vc_cfg.frameRateDenom;
    m_bitrate_monitor.period = 5;
    m_bitrate_monitor.sum_period = 0;
    m_bitrate_monitor.ma_bitrate = 0;
    m_bitrate_monitor.frame_sizes = std::queue<u32>();

    m_cycle_monitor.enabled = true;
    m_cycle_monitor.deviation_threshold = 5;
    m_cycle_monitor.monitor_frames = 60;
    m_cycle_monitor.start_time = static_cast<std::time_t>(-1);
    m_cycle_monitor.start_delay = 1;
    m_cycle_monitor.frame_count = 0;
    m_cycle_monitor.sum = 0;

    // The init functions must be in this order
    if ((ret = init_coding_control_config()) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init coding control config");
        return ret;
    }

    if ((ret = init_preprocessing_config()) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init preprocessing config");
        return ret;
    }

    if ((ret = init_rate_control_config()) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init rate control config");
        return ret;
    }

    if ((ret = init_monitors_config()) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init monitors config");
        return ret;
    }

    m_update_required = {};
    m_is_user_set_bitrate = false;
    m_stream_restart = STREAM_RESTART_NONE;
    m_state = ENCODER_STATE_INITIALIZED;
    m_header.buffer = nullptr;
    m_header.size = 0;
    return MEDIA_LIBRARY_SUCCESS;
}

EncoderOutputBuffer Encoder::get_encoder_header_output_buffer()
{
    return m_impl->get_encoder_header_output_buffer();
}

EncoderOutputBuffer Encoder::Impl::get_encoder_header_output_buffer()
{
    return m_header;
}

media_library_return Encoder::configure(std::string json_string)
{
    return m_impl->configure(json_string);
}

media_library_return Encoder::Impl::configure(std::string json_string)
{
    encoder_config_t temp_prev_encoder_config = m_config->get_config();
    if (m_config->configure(json_string) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure encoder");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    if (m_config->config_struct_equal(m_config->get_config(), temp_prev_encoder_config))
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "No configuration change detected, skipping configuration");
        return MEDIA_LIBRARY_SUCCESS;
    }

    m_update_required = {ENCODER_CONFIG_GOP, ENCODER_CONFIG_CODING_CONTROL, ENCODER_CONFIG_PRE_PROCESSING,
                         ENCODER_CONFIG_RATE_CONTROL};

    hailo_encoder_config_t old_hailo_encoder_config = std::get<hailo_encoder_config_t>(temp_prev_encoder_config);
    if (m_config->get_hailo_config().rate_control.bitrate.target_bitrate !=
        old_hailo_encoder_config.rate_control.bitrate.target_bitrate)
    {
        m_is_user_set_bitrate = true;
        // std::cout << "Update bitrate required from " << old_hailo_encoder_config.rate_control.bitrate.target_bitrate
        //           << " to " << m_config->get_hailo_config().rate_control.bitrate.target_bitrate << std::endl;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::configure(const encoder_config_t &config)
{
    return m_impl->configure(config);
}

media_library_return Encoder::Impl::configure(const encoder_config_t &config)
{
    auto enc_conf = std::get<hailo_encoder_config_t>(config);
    auto monitors_conf = &enc_conf.monitors_control;
    m_bitrate_monitor.enabled = monitors_conf->bitrate_monitor.enable;
    m_bitrate_monitor.period = monitors_conf->bitrate_monitor.period;
    m_cycle_monitor.enabled = monitors_conf->cycle_monitor.enable;
    m_cycle_monitor.start_delay = monitors_conf->cycle_monitor.start_delay;
    m_cycle_monitor.deviation_threshold = monitors_conf->cycle_monitor.deviation_threshold;

    auto old_config = m_config->get_hailo_config();
    if (m_config->configure(config) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure encoder");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // Read the configuration again after the configuration is done
    auto new_config = m_config->get_hailo_config();

    if (m_config->config_struct_equal(old_config, new_config))
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "No configuration change detected, skipping configuration");
        return MEDIA_LIBRARY_SUCCESS;
    }

    m_update_required = {ENCODER_CONFIG_CODING_CONTROL, ENCODER_CONFIG_PRE_PROCESSING, ENCODER_CONFIG_RATE_CONTROL};
    bool gop_update_required = gop_config_update_required(old_config, new_config);
    bool hard_restart = hard_restart_required(old_config, new_config, gop_update_required);

    if (new_config.rate_control.bitrate.target_bitrate != old_config.rate_control.bitrate.target_bitrate)
    {
        m_is_user_set_bitrate = true;
    }

    // Gop change update required
    if (gop_update_required)
    {
        m_update_required.emplace_back(ENCODER_CONFIG_GOP);
    }

    if (hard_restart)
    {
        m_update_required.emplace_back(ENCODER_CONFIG_STREAM);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::Impl::update_gop_configurations()
{
    if (m_update_required.empty())
    {
        return MEDIA_LIBRARY_SUCCESS;
    }

    auto it_gop = std::find(m_update_required.begin(), m_update_required.end(), ENCODER_CONFIG_GOP);
    if (it_gop != m_update_required.end())
    {
        if (init_gop_config() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init gop config");
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }

        // remove gop from update required list
        m_update_required.erase(it_gop);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::Impl::update_configurations()
{
    media_library_return ret = MEDIA_LIBRARY_SUCCESS;
    for (auto &config : m_update_required)
    {
        switch (config)
        {
        case ENCODER_CONFIG_RATE_CONTROL: {
            ret = init_rate_control_config();
            break;
        }
        case ENCODER_CONFIG_PRE_PROCESSING: {
            ret = init_preprocessing_config();
            break;
        }
        case ENCODER_CONFIG_CODING_CONTROL: {
            ret = init_coding_control_config();
            break;
        }
        case ENCODER_CONFIG_MONITORS: {
            ret = init_monitors_config();
            break;
        }
        case ENCODER_CONFIG_GOP: {
            // handled before in update_gop_configurations
            break;
        }
        case ENCODER_CONFIG_STREAM: {
            break;
        }
        default:
            LOGGER__MODULE__ERROR(MODULE_NAME, "Unknown configuration type");
            m_update_required.clear();
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    // Clear update required list
    m_update_required.clear();

    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to update configurations");
    }

    return ret;
}

media_library_return Encoder::Impl::stream_restart()
{
    VCEncRet enc_ret = VCEncStrmEnd(m_inst, &m_enc_in, &m_enc_out);
    if (enc_ret != VCENC_OK)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder restart - Failed to end stream, returned {}", enc_ret);
        return MEDIA_LIBRARY_ERROR;
    }

    if (m_stream_restart == STREAM_RESTART_HARD)
    {
        m_header.buffer = nullptr;
        if ((enc_ret = VCEncRelease(m_inst)) != VCENC_OK)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder HARD restart - Failed to release encoder, returned {}",
                                  enc_ret);
            return MEDIA_LIBRARY_ERROR;
        }
    }

    if (update_gop_configurations() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder restart - Failed to update gop configurations");
        return MEDIA_LIBRARY_ERROR;
    }

    if (m_stream_restart == STREAM_RESTART_HARD)
    {
        media_library_return ret = init_encoder_config();
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder HARD restart - Failed to init encoder config");
            return ret;
        }
    }

    if (update_configurations() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder restart - Failed to update configurations");
        return MEDIA_LIBRARY_ERROR;
    }

    if (encode_header() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder restart - Failed to encode header");
        return MEDIA_LIBRARY_ERROR;
    }
    m_stream_restart = STREAM_RESTART_NONE;
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::Impl::encode_header()
{
    if (m_inst == NULL)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder not initialized");
        return MEDIA_LIBRARY_UNINITIALIZED;
    }
    auto expected_encoded_header = encode_executer(encoder_operation_t::ENCODER_OPERATION_START);
    if (!expected_encoded_header.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to encode header");
        return MEDIA_LIBRARY_ERROR;
    }
    m_header = expected_encoded_header.value();
    // Default gop size as IPPP
    m_enc_in.poc = 0;
    // m_enc_in.gopSize =  m_next_gop_size = ((enc_params->gopSize == 0) ? 1 :
    // enc_params->gopSize);
    m_enc_in.gopSize = m_next_gop_size = get_gop_size();
    m_next_coding_type = VCENC_INTRA_FRAME;
    return MEDIA_LIBRARY_SUCCESS;
}

void Encoder::update_stride(uint32_t stride)
{
    m_impl->update_stride(stride);
}

void Encoder::Impl::update_stride(uint32_t stride)
{
    if (stride != m_input_stride)
    {
        m_input_stride = stride;
        if (init_preprocessing_config() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to init preprocessing config");
            // TOTO return error
        }
    }
}

int Encoder::get_gop_size()
{
    return m_impl->get_gop_size();
}

int Encoder::Impl::get_gop_size()
{
    return m_gop_cfg->get_gop_size();
}

void Encoder::force_keyframe()
{
    m_impl->force_keyframe();
}

void Encoder::Impl::force_keyframe()
{
    if (m_state != ENCODER_STATE_START)
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Encoder is not started, skipping force keyframe");
        return;
    }

    LOGGER__MODULE__INFO(
        MODULE_NAME, "Encoder internal - Force Keyframe, setting next coding type to INTRA_FRAME poc to 0 and removing "
                     "oldest input buffer");
    m_enc_in.codingType = m_next_coding_type = VCENC_INTRA_FRAME;
    m_enc_in.poc = 0;
    m_counters.last_idr_picture_cnt = m_counters.picture_cnt;

    if (m_inputs.size() > 0)
    {
        // remove oldest buffer from m_inputs
        HailoMediaLibraryBufferPtr buf = m_inputs[0].second;
        m_inputs.erase(m_inputs.begin());
    }
}

encoder_config_t Encoder::get_config()
{
    return m_impl->get_config();
}

encoder_config_t Encoder::get_user_config()
{
    return m_impl->get_user_config();
}

encoder_config_t Encoder::Impl::get_config()
{
    return m_config->get_config();
}

encoder_config_t Encoder::Impl::get_user_config()
{
    return m_config->get_user_config();
}

tl::expected<EncoderOutputBuffer, media_library_return> Encoder::start()
{
    return m_impl->start();
}

tl::expected<EncoderOutputBuffer, media_library_return> Encoder::Impl::start()
{
    LOGGER__MODULE__INFO(MODULE_NAME, "Encoder - Start the stream");

    if (m_state == ENCODER_STATE_UNINITIALIZED)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder is not initialized");
        m_header.buffer = nullptr;
        m_header.size = 0;
        return tl::make_unexpected(MEDIA_LIBRARY_UNINITIALIZED);
    }

    if (m_state == ENCODER_STATE_START)
    {
        LOGGER__MODULE__WARNING(MODULE_NAME, "Encoder is already started");
        return m_header;
    }

    m_enc_in.gopSize = get_gop_size();

    auto expected_encoded_header = encode_executer(encoder_operation_t::ENCODER_OPERATION_START);
    if (!expected_encoded_header.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to start encoder");
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    m_header = expected_encoded_header.value();
    // Default gop size as IPPP
    m_enc_in.poc = 0;
    m_enc_in.gopSize = m_next_gop_size = get_gop_size();
    m_next_coding_type = VCENC_INTRA_FRAME;
    m_counters = {};
    m_state = ENCODER_STATE_START;
    return m_header;
}

void Encoder::stop()
{
    m_impl->stop();
}

tl::expected<EncoderOutputBuffer, media_library_return> Encoder::finish()
{
    return m_impl->finish();
}

void Encoder::Impl::stop()
{
    m_state = ENCODER_STATE_STOP;
    std::unique_lock<std::mutex> lck(m_is_encoding_multiple_frames_mtx);
    m_is_encoding_multiple_frames_cv.wait(lck, [this]() { return !m_is_encoding_multiple_frames; });
    m_inputs.clear();
}

tl::expected<EncoderOutputBuffer, media_library_return> Encoder::Impl::finish()
{
    auto expected_encoded_eos = encode_executer(encoder_operation_t::ENCODER_OPERATION_STOP);
    if (!expected_encoded_eos.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to stop encoder");
        return tl::make_unexpected(MEDIA_LIBRARY_ERROR);
    }
    m_header = expected_encoded_eos.value();
    return m_header;
}

media_library_return Encoder::Impl::update_input_buffer(HailoMediaLibraryBufferPtr buf)
{
    int ret;
    uint32_t num_of_planes = buf->get_num_of_planes();
    u32 *plane_ptr = nullptr;
    int planeFd = -1;
    u32 plane_size = 0;
    std::array<u32 *, 3> bus_addresses = {&(m_enc_in.busLuma), &(m_enc_in.busChromaU), &(m_enc_in.busChromaV)};

    if (num_of_planes == 0 || num_of_planes > 3)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Could not get number of planes of buffer - Invalid number of planes {}",
                              num_of_planes);
        return MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS;
    }
    update_stride(buf->get_plane_stride(0));

    if (buf->is_dmabuf())
    {
        for (uint32_t i = 0; i < num_of_planes; i++)
        {
            planeFd = buf->get_plane_fd(i);
            if (planeFd <= 0)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Could not get dmabuf fd of plane {}", i);
                return MEDIA_LIBRARY_BUFFER_NOT_FOUND;
            }
            ret = EWLShareDmabuf(m_ewl, planeFd, bus_addresses[i]);
            if (ret != EWL_OK)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Could not get physical address of plane {}", i);
                for (uint32_t j = 0; j <= i; j++)
                {
                    EWLUnshareDmabuf(m_ewl, buf->get_plane_fd(j));
                }
                return MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS;
            }
        }
    }
    else
    {
        for (uint32_t i = 0; i < num_of_planes; i++)
        {
            plane_ptr = static_cast<u32 *>(buf->get_plane_ptr(i));
            plane_size = buf->get_plane_size(i);
            if (plane_ptr == nullptr || plane_size == 0)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Could not get plane {} of buffer", i);
                return MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS;
            }
            ret = EWLGetBusAddress(m_ewl, plane_ptr, bus_addresses[i], plane_size);
            if (ret != EWL_OK)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Could not get physical address of plane {}", i);
                return MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS;
            }
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::Impl::encode_multiple_frames(std::vector<EncoderOutputBuffer> &outputs)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder - encode_multiple_frames");
    media_library_return ret = MEDIA_LIBRARY_UNINITIALIZED;
    auto gop_size = m_enc_in.gopSize;
    if (gop_size == 0)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder - encode_multiple_frames - gop size is 0");
        return MEDIA_LIBRARY_ERROR;
    }

    // Assuming enc_params->encIn.gopSize is not 0.
    std::unique_lock<std::mutex> lck(m_is_encoding_multiple_frames_mtx);
    m_is_encoding_multiple_frames = true;
    lck.unlock();
    for (uint8_t i = 0; i < gop_size; i++)
    {
        auto idx = m_enc_in.gopPicIdx + m_gop_cfg->get_gop_cfg_offset()[m_enc_in.gopSize];
        auto poc = m_gop_cfg->get_gop_pic_cfg()[idx].poc;
        ret = encode_frame(m_inputs[poc - 1].second, outputs, m_inputs[poc - 1].first);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Error encoding frame {} with error {}", i, ret);
            break;
        }
    }
    lck.lock();
    m_is_encoding_multiple_frames = false;
    lck.unlock();
    m_is_encoding_multiple_frames_cv.notify_all();
    return ret;
}

static int64_t time_diff(const struct timespec after, const struct timespec before)
{
    return ((int64_t)after.tv_sec - (int64_t)before.tv_sec) * (int64_t)1000 +
           ((int64_t)after.tv_nsec - (int64_t)before.tv_nsec) / 1000000;
}

static void releaseDmabuf(HailoMediaLibraryBufferPtr buf, void *ewl)
{
    for (uint32_t i = 0; i < buf->get_num_of_planes(); i++)
    {
        int planeFd = buf->get_plane_fd(i);
        if (planeFd <= 0)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Could not get dmabuf fd of plane {}", i);
            continue;
        }
        if (EWLUnshareDmabuf(ewl, planeFd) != EWL_OK)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Could not get physical address of plane {} fd {}", i, planeFd);
        }
    }
}

media_library_return Encoder::Impl::encode_frame(HailoMediaLibraryBufferPtr buf,
                                                 std::vector<EncoderOutputBuffer> &outputs, uint32_t frame_number)
{
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder - encode_frame");
    VCEncRet enc_ret = VCENC_OK;
    media_library_return ret = MEDIA_LIBRARY_UNINITIALIZED;
    struct timespec start_encode, end_encode;
    ret = update_input_buffer(buf);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder - encode_frame - Failed to update input buffer");
        return ret;
    }

    m_enc_in.codingType = (m_enc_in.poc == 0) ? VCENC_INTRA_FRAME : m_next_coding_type;
    if (m_enc_in.codingType == VCENC_INTRA_FRAME)
    {
        m_enc_in.poc = 0;
        m_enc_in.resendSPS = 1;
        m_enc_in.resendPPS = 1;
        m_counters.last_idr_picture_cnt = m_counters.picture_cnt;
    }
    else
    {
        m_enc_in.resendSPS = 0;
        m_enc_in.resendPPS = 0;
    }

    clock_gettime(CLOCK_MONOTONIC, &start_encode);
    auto expected_encoded_frame = encode_executer(encoder_operation_t::ENCODER_OPERATION_ENCODE);
    if (!expected_encoded_frame.has_value())
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to encode frame");
        return MEDIA_LIBRARY_ENCODER_ENCODE_ERROR;
    }
    EncoderOutputBuffer output = expected_encoded_frame.value();
    enc_ret = output.encoder_ret_code;
    clock_gettime(CLOCK_MONOTONIC, &end_encode);
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoding of frame took {} ms", time_diff(end_encode, start_encode));
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoding performance is {} cycles", VCEncGetPerformance(m_inst));

    switch (enc_ret)
    {
    case VCENC_FRAME_READY: {
        m_counters.picture_enc_cnt++;
        if (!m_multislice_encoding)
        {
            if (m_bitrate_monitor.enabled)
                bitrate_monitor_sample();
            if (m_cycle_monitor.enabled)
                cycle_monitor_sample();

            if (m_enc_out.streamSize == 0)
            {
                LOGGER__MODULE__INFO(MODULE_NAME, "Dropping frame {} of type {}", m_counters.picture_enc_cnt - 1,
                                     m_enc_in.codingType);

                /* restart with yuv of next frame for IDR or GOP start */
                if (m_enc_in.poc == 0 || m_enc_in.gopPicIdx == 0)
                {
                    m_counters.picture_cnt++;
                    m_counters.last_idr_picture_cnt++;
                }
                /* follow current GOP, handling frame skip in API */
                m_next_coding_type = find_next_pic();
                output.size = 0;
                outputs.emplace_back(std::move(output));
            }
            else
            {
                output.buffer->copy_metadata_from(buf);
                outputs.emplace_back(std::move(output));
                m_counters.validencodedframenumber++;
                m_next_coding_type = find_next_pic();
                if (m_next_coding_type == VCENC_INTRA_FRAME)
                {
                    if (!m_update_required.empty())
                    {
                        m_stream_restart = STREAM_RESTART;
                        if (m_is_user_set_bitrate)
                        {
                            // Disable zoom boost feature
                            m_settings_boost_start_time = std::chrono::steady_clock::time_point::min();
                            apply_constant_optical_zoom_boost(buf->optical_zoom_magnification);
                            m_is_user_set_bitrate = false;
                        }
                        // check if m_update_required contains CONFIG_STREAM
                        if (std::find(m_update_required.begin(), m_update_required.end(), ENCODER_CONFIG_STREAM) !=
                            m_update_required.end())
                        {
                            m_stream_restart = STREAM_RESTART_HARD;
                        }
                    }
                }
            }
            outputs.at(outputs.size() - 1).frame_number = frame_number;
        }
        ret = MEDIA_LIBRARY_SUCCESS;
        break;
    }
    case VCENC_OUTPUT_BUFFER_OVERFLOW: {
        m_counters.picture_enc_cnt++;
        LOGGER__MODULE__WARNING(MODULE_NAME, "Got buffer overflow IRQ for frame {} in resolution {}x{}",
                                m_counters.picture_enc_cnt - 1, m_vc_cfg.width, m_vc_cfg.height);
        if (m_bitrate_monitor.enabled)
            bitrate_monitor_sample();
        if (m_cycle_monitor.enabled)
            cycle_monitor_sample();

        EncoderOutputBuffer output;

        /* restart with yuv of next frame for IDR or GOP start */
        if (m_enc_in.codingType == VCENC_INTRA_FRAME)
        {
            m_counters.picture_cnt++;
            m_counters.last_idr_picture_cnt++;
        }
        else
        {
            LOGGER__MODULE__WARNING(MODULE_NAME, "Buffer overflow on inter frame (type:{}), restart stream",
                                    m_enc_in.codingType);
            m_stream_restart = STREAM_RESTART_HARD;
        }
        output.size = 0;
        output.frame_number = frame_number;
        outputs.emplace_back(std::move(output));
        ret = MEDIA_LIBRARY_ENCODER_ENCODE_ERROR;
        break;
    }
    default: {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder - encode_frame - Error encoding frame {}", enc_ret);
        ret = MEDIA_LIBRARY_ENCODER_ENCODE_ERROR;
        break;
    }
    }

    if (buf->is_dmabuf())
    {
        releaseDmabuf(buf, m_ewl);
    }
    return ret;
}

std::vector<EncoderOutputBuffer> Encoder::handle_frame(HailoMediaLibraryBufferPtr buf, uint32_t frame_number)
{
    return m_impl->handle_frame(buf, frame_number);
}

void Encoder::Impl::boost_settings_for_optical_zoom()
{
    const auto &hailo_config = m_config->get_hailo_config();
    const auto &rate_control = hailo_config.rate_control;

    // Check if zooming process mode is enabled
    auto mode = rate_control.zoom_bitrate_adjuster.mode.value_or(ZOOM_BITRATE_ADJUSTER_ZOOMING_PROCESS);
    if (mode != ZOOM_BITRATE_ADJUSTER_ZOOMING_PROCESS && mode != ZOOM_BITRATE_ADJUSTER_BOTH)
    {
        return;
    }

    float zoom_bitrate_adjuster_factor = rate_control.zoom_bitrate_adjuster.zooming_process_bitrate_factor.value_or(
        DEFAULT_ZOOM_BITRATE_ADJUSTER_ZOOMING_BITRATE_FACTOR);
    uint32_t zoom_bitrate_adjuster_max_bitrate =
        rate_control.zoom_bitrate_adjuster.zooming_process_max_bitrate.value_or(
            DEFAULT_ZOOM_BITRATE_ADJUSTER_ZOOMING_MAX_BITRATE);

    std::lock_guard<std::mutex> lock(m_settings_boost_mutex);

    if (!m_zooming_boost_enabled)
    {
        VCEncRet ret = VCEncGetRateCtrl(m_inst, &m_vc_rate_cfg);
        if (ret != VCENC_OK)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to get current bitrate, error: {}", ret);
            return;
        }
        u32 current_bitrate = m_vc_rate_cfg.bitPerSecond;
        u32 baseline_bitrate = rate_control.bitrate.target_bitrate;
        u32 boosted_bitrate = static_cast<u32>(baseline_bitrate * zoom_bitrate_adjuster_factor);

        // Apply max_bitrate limit if set (0 means no limit)
        if (zoom_bitrate_adjuster_max_bitrate > 0)
        {
            boosted_bitrate = std::clamp(boosted_bitrate, 0u, zoom_bitrate_adjuster_max_bitrate);
        }

        m_vc_rate_cfg.bitPerSecond = boosted_bitrate;

        m_original_gop_anomaly_bitrate_adjuster_enable = m_vc_rate_cfg.gop_anomaly_bitrate_adjuster.enable;
        m_vc_rate_cfg.gop_anomaly_bitrate_adjuster.enable = 0; // Disable smooth bitrate during boost

        m_zooming_boost_enabled = true;

        // std::cout << "ZOOM Boost: bitrate from " << current_bitrate << " to " << m_vc_rate_cfg.bitPerSecond
        //           << " (factor: " << zoom_bitrate_adjuster_factor << ", max: "
        //           << (zoom_bitrate_adjuster_max_bitrate > 0 ? std::to_string(zoom_bitrate_adjuster_max_bitrate)
        //                                                     : "unlimited")
        //           << ") due to optical zoom" << std::endl;
        LOGGER__MODULE__INFO(
            MODULE_NAME, "ZOOMING bitrate adjust from {} to {} (factor: {:.1f}, max: {}) due to optical zoom",
            current_bitrate, m_vc_rate_cfg.bitPerSecond, zoom_bitrate_adjuster_factor,
            zoom_bitrate_adjuster_max_bitrate > 0 ? std::to_string(zoom_bitrate_adjuster_max_bitrate) : "unlimited");

        ret = VCEncSetRateCtrl(m_inst, &m_vc_rate_cfg);
        if (ret != VCENC_OK)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set boosted bitrate, error: {}", ret);
        }

        bool zoom_bitrate_adjuster_force_keyframe =
            rate_control.zoom_bitrate_adjuster.zooming_process_force_keyframe.value_or(
                DEFAULT_ZOOM_BITRATE_ADJUSTER_ZOOMING_FORCE_KEYFRAME);
        if (zoom_bitrate_adjuster_force_keyframe)
        {
            LOGGER__MODULE__INFO(MODULE_NAME, "ZOOMING bitrate adjust: Forcing keyframe during optical zoom change");
            force_keyframe();
        }
    }

    // Reset or start the timer
    m_settings_boost_start_time = std::chrono::steady_clock::now();
}

void Encoder::Impl::check_and_restore_settings(float current_optical_zoom)
{
    std::lock_guard<std::mutex> lock(m_settings_boost_mutex);

    if (m_zooming_boost_enabled)
    {
        // Get current boost configuration from rate_control
        auto hailo_config = m_config->get_hailo_config();
        auto &rate_control = hailo_config.rate_control;

        uint32_t zoom_bitrate_adjuster_timeout_ms =
            rate_control.zoom_bitrate_adjuster.zooming_process_timeout_ms.value_or(
                DEFAULT_ZOOM_BITRATE_ADJUSTER_ZOOMING_TIMEOUT_MS);

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_settings_boost_start_time);

        if (elapsed.count() >= zoom_bitrate_adjuster_timeout_ms)
        {
            VCEncRet ret = VCEncGetRateCtrl(m_inst, &m_vc_rate_cfg);
            u32 config_bitrate =
                get_constant_optical_zoom_boost(current_optical_zoom, rate_control.bitrate.target_bitrate);
            u32 current_bitrate = m_vc_rate_cfg.bitPerSecond;

            m_vc_rate_cfg.bitPerSecond = config_bitrate;
            m_vc_rate_cfg.gop_anomaly_bitrate_adjuster.enable = m_original_gop_anomaly_bitrate_adjuster_enable;
            m_zooming_boost_enabled = false;

            // std::cout << "ZOOM Boost OUT: Restored bitrate from " << current_bitrate << " to " << config_bitrate
            //           << " after " << elapsed.count() << "ms timeout" << " set gop anomaly to "
            //           << m_vc_rate_cfg.gop_anomaly_bitrate_adjuster.enable << std::endl;
            LOGGER__MODULE__INFO(MODULE_NAME, "Restored bitrate from {} to {} after {}ms timeout", current_bitrate,
                                 config_bitrate, zoom_bitrate_adjuster_timeout_ms);

            ret = VCEncSetRateCtrl(m_inst, &m_vc_rate_cfg);
            if (ret != VCENC_OK)
            {
                LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to restore original bitrate, error: {}", ret);
            }

            auto hailo_config = m_config->get_hailo_config();
            bool zoom_bitrate_adjuster_force_keyframe =
                hailo_config.rate_control.zoom_bitrate_adjuster.zooming_process_force_keyframe.value_or(
                    DEFAULT_ZOOM_BITRATE_ADJUSTER_ZOOMING_FORCE_KEYFRAME);
            if (zoom_bitrate_adjuster_force_keyframe)
            {
                LOGGER__MODULE__INFO(MODULE_NAME,
                                     "ZOOMING bitrate adjust done: Forcing keyframe after optical zoom change");
                force_keyframe();
            }
        }
    }
}

media_library_return Encoder::Impl::handle_bitrate_adjustment_hooks(HailoMediaLibraryBufferPtr buf,
                                                                    uint32_t frame_number)
{
    if (m_is_user_set_bitrate)
    {
        LOGGER__MODULE__DEBUG(
            MODULE_NAME, "Delaying handle_bitrate_adjustment_hooks - due to bitrate update to {}, requested by user",
            m_config->get_hailo_config().rate_control.bitrate.target_bitrate);
        return MEDIA_LIBRARY_SUCCESS;
    }

    // Check if we need to restore settings after timeout
    float current_optical_zoom = buf->optical_zoom_magnification;
    check_and_restore_settings(current_optical_zoom);

    if (current_optical_zoom != m_previous_optical_zoom_magnification)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Optical zoom magnification changed from {:.2f} to {:.2f} for frame {}",
                             m_previous_optical_zoom_magnification, current_optical_zoom, frame_number);
        m_previous_optical_zoom_magnification = current_optical_zoom;

        boost_settings_for_optical_zoom();
        // Apply constant optical zoom boost if enabled and threshold is exceeded
        apply_constant_optical_zoom_boost(current_optical_zoom);
    }

    if (buf->motion_detected)
    {
        LOGGER__MODULE__INFO(MODULE_NAME, "Motion detected for frame {}", frame_number);
    }

    return MEDIA_LIBRARY_SUCCESS;
}

std::vector<EncoderOutputBuffer> Encoder::Impl::handle_frame(HailoMediaLibraryBufferPtr buf, uint32_t frame_number)
{
    media_library_return ret = MEDIA_LIBRARY_UNINITIALIZED;
    LOGGER__MODULE__DEBUG(MODULE_NAME, "Start Handling Frame with plane 0 of size {} for buffer id {}",
                          buf->get_plane_size(0), buf->buffer_index);

    std::string name = "encoder_" + std::to_string(m_vc_cfg.width) + "x" + std::to_string(m_vc_cfg.height);
    SnapshotManager::get_instance().take_snapshot(name, buf);

    if (handle_bitrate_adjustment_hooks(buf, frame_number) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to handle hooks for frame {}", frame_number);
        ret = MEDIA_LIBRARY_ERROR;
    }

    std::vector<EncoderOutputBuffer> outputs;
    outputs.clear();

    if (m_stream_restart != STREAM_RESTART_NONE)
    {
        if (stream_restart() != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder - encode_frame - Failed to restart stream");
            // Stream restart failed, clear update required list
            m_update_required.clear();
            ret = MEDIA_LIBRARY_ERROR;
        }
    }

    switch (m_next_coding_type)
    {
    case VCENC_INTRA_FRAME: {
        ret = encode_frame(buf, outputs, frame_number);
        break;
    }
    case VCENC_PREDICTED_FRAME: {
        if (m_inputs.size() == (size_t)m_enc_in.gopSize - 1)
        {
            m_inputs.emplace_back(frame_number, buf);
            ret = encode_multiple_frames(outputs);
            m_inputs.clear();
        }
        else if (m_inputs.size() < (size_t)m_enc_in.gopSize - 1)
        {
            m_inputs.emplace_back(frame_number, buf);
            ret = MEDIA_LIBRARY_SUCCESS;
        }
        else
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder Error - Too many inputs");
            ret = MEDIA_LIBRARY_ERROR;
        }
        break;
    }
    case VCENC_BIDIR_PREDICTED_FRAME: {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder Error - BIDIR Predicted Frame");
        break;
    }
    default: {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder Error - Unknown coding type");
        break;
    }
    }

    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Encoder Error - encoding frame returned {}", ret);
    }

    LOGGER__MODULE__DEBUG(MODULE_NAME, "Encoder - handle_frame - returns {} outputs", outputs.size());
    return outputs;
}

VCEncPictureCodingType Encoder::Impl::find_next_pic()
{
    VCEncPictureCodingType nextCodingType;
    int idx, offset, cur_poc, delta_poc_to_next;
    int next_gop_size = m_next_gop_size;
    int picture_cnt_tmp = m_counters.picture_cnt;
    VCEncGopConfig *gop_cfg = (VCEncGopConfig *)(&(m_enc_in.gopConfig));
    const uint8_t *gop_cfg_offset = m_gop_cfg->get_gop_cfg_offset();

    // get current poc within GOP
    if (m_enc_in.codingType == VCENC_INTRA_FRAME)
    {
        // next is an I Slice
        cur_poc = 0;
        m_enc_in.gopPicIdx = 0;
    }
    else
    {
        // Update current idx and poc within a GOP
        idx = m_enc_in.gopPicIdx + gop_cfg_offset[m_enc_in.gopSize];
        cur_poc = gop_cfg->pGopPicCfg[idx].poc;
        m_enc_in.gopPicIdx = (m_enc_in.gopPicIdx + 1) % m_enc_in.gopSize;
        if (m_enc_in.gopPicIdx == 0)
            cur_poc -= m_enc_in.gopSize;
    }

    // a GOP end, to start next GOP
    if (m_enc_in.gopPicIdx == 0)
        offset = gop_cfg_offset[next_gop_size];
    else
        offset = gop_cfg_offset[m_enc_in.gopSize];

    // get next poc within GOP, and delta_poc
    idx = m_enc_in.gopPicIdx + offset;
    delta_poc_to_next = gop_cfg->pGopPicCfg[idx].poc - cur_poc;
    // next picture cnt
    m_counters.picture_cnt = picture_cnt_tmp + delta_poc_to_next;

    // Handle Tail (cut by an I frame)
    {
        // just finished a GOP and will jump to a P frame
        if (m_enc_in.gopPicIdx == 0 && delta_poc_to_next > 1)
        {
            int gop_end_pic = m_counters.picture_cnt;
            int gop_shorten = 0;

            // cut by an IDR
            if ((m_intra_pic_rate) && ((gop_end_pic - m_counters.last_idr_picture_cnt) >= (int)m_intra_pic_rate))
                gop_shorten = 1 + ((gop_end_pic - m_counters.last_idr_picture_cnt) - m_intra_pic_rate);

            if (gop_shorten >= next_gop_size)
            {
                // for gopsize = 1
                m_counters.picture_cnt = picture_cnt_tmp + 1 - cur_poc;
            }
            else if (gop_shorten > 0)
            {
                // reduce gop size
                const int max_reduced_gop_size = 4;
                next_gop_size -= gop_shorten;
                if (next_gop_size > max_reduced_gop_size)
                    next_gop_size = max_reduced_gop_size;

                idx = gop_cfg_offset[next_gop_size];
                delta_poc_to_next = gop_cfg->pGopPicCfg[idx].poc - cur_poc;
                m_counters.picture_cnt = picture_cnt_tmp + delta_poc_to_next;
            }
            m_enc_in.gopSize = next_gop_size;
        }

        m_enc_in.poc += m_counters.picture_cnt - picture_cnt_tmp;
        // next coding type
        bool forceIntra =
            m_intra_pic_rate && ((m_counters.picture_cnt - m_counters.last_idr_picture_cnt) >= (int)m_intra_pic_rate);
        if (forceIntra)
            nextCodingType = VCENC_INTRA_FRAME;
        else
        {
            idx = m_enc_in.gopPicIdx + gop_cfg_offset[m_enc_in.gopSize];
            nextCodingType = gop_cfg->pGopPicCfg[idx].codingType;
        }
    }
    gop_cfg->id = m_enc_in.gopPicIdx + gop_cfg_offset[m_enc_in.gopSize];
    {
        // guess next rps needed for H.264 DPB management (MMO), assume gopSize
        // unchanged. gopSize change only occurs on adaptive GOP or tail GOP
        // (lowdelay = 0). then the next RPS is 1st of default RPS of some
        // gopSize, which only includes the P frame of last GOP
        i32 next_poc = gop_cfg->pGopPicCfg[gop_cfg->id].poc;
        i32 gopPicIdx = (m_enc_in.gopPicIdx + 1) % m_enc_in.gopSize;
        if (gopPicIdx == 0)
            next_poc -= m_enc_in.gopSize;
        gop_cfg->id_next = gopPicIdx + gop_cfg_offset[m_enc_in.gopSize];
        gop_cfg->delta_poc_to_next = gop_cfg->pGopPicCfg[gop_cfg->id_next].poc - next_poc;
    }

    m_enc_in.timeIncrement = m_vc_cfg.frameRateDenom;

    return nextCodingType;
}

void Encoder::Impl::bitrate_monitor_sample()
{
    u32 cur_frame_size = m_enc_out.streamSize * BITS_IN_BYTE;

    // if period changed and frame_sizes is too large, remove frames until we match the new period
    if (m_bitrate_monitor.frame_sizes.size() > m_bitrate_monitor.fps * m_bitrate_monitor.period)
    {
        while (m_bitrate_monitor.frame_sizes.size() > m_bitrate_monitor.fps * m_bitrate_monitor.period)
        {
            m_bitrate_monitor.sum_period -= m_bitrate_monitor.frame_sizes.front();
            m_bitrate_monitor.frame_sizes.pop();
        }
    }
    // normal case - maintian moving average by adding the new frame size and removing the oldest
    else if (m_bitrate_monitor.frame_sizes.size() == m_bitrate_monitor.fps * m_bitrate_monitor.period)
    {
        m_bitrate_monitor.sum_period -= m_bitrate_monitor.frame_sizes.front();
        m_bitrate_monitor.frame_sizes.pop();
        m_bitrate_monitor.sum_period += cur_frame_size;
        m_bitrate_monitor.frame_sizes.push(cur_frame_size);
    }
    // not enough samples yet, just add the new frame size
    else
    {
        m_bitrate_monitor.sum_period += cur_frame_size;
        m_bitrate_monitor.frame_sizes.push(cur_frame_size);
    }

    // if the number of samples is for at least 1 second, update the moving average
    if (m_bitrate_monitor.frame_sizes.size() / m_bitrate_monitor.fps >= 1)
    {
        m_bitrate_monitor.ma_bitrate =
            m_bitrate_monitor.sum_period / (m_bitrate_monitor.frame_sizes.size() / m_bitrate_monitor.fps);
        LOGGER__MODULE__TRACE(MODULE_NAME, "Stream with res: {}x{}, current bitrate = {}", m_vc_cfg.width,
                              m_vc_cfg.height, m_bitrate_monitor.ma_bitrate);

        if (m_bitrate_monitor.output_file.is_open())
        {
            monitor_write_to_file(m_bitrate_monitor.output_file,
                                  "Stream with res: " + std::to_string(m_vc_cfg.width) + "x" +
                                      std::to_string(m_vc_cfg.height) +
                                      ", current bitrate = " + std::to_string(m_bitrate_monitor.ma_bitrate));
        }
    }
}

void Encoder::Impl::cycle_monitor_sample()
{
    if (m_cycle_monitor.frame_count == 0 && m_cycle_monitor.start_time == static_cast<std::time_t>(-1))
    {
        m_cycle_monitor.start_time = std::time(NULL);

        // Delay the start of the monitoring
        if (m_cycle_monitor.start_delay > 0)
            return;
    }

    std::time_t currentTime = std::time(nullptr);
    if (currentTime - m_cycle_monitor.start_time < m_cycle_monitor.start_delay)
        return;

    if (m_cycle_monitor.frame_count < m_cycle_monitor.monitor_frames)
    {
        m_cycle_monitor.frame_count++;
        m_cycle_monitor.sum += VCEncGetPerformance(m_inst);
        return;
    }

    float avg = m_cycle_monitor.sum / m_cycle_monitor.frame_count;
    u32 cur_frame_cycles = VCEncGetPerformance(m_inst);

    if (cur_frame_cycles > (u32)(avg + (avg * m_cycle_monitor.deviation_threshold / 100)) ||
        cur_frame_cycles < (u32)(avg - (avg * m_cycle_monitor.deviation_threshold / 100)))
    {
        LOGGER__MODULE__INFO(MODULE_NAME,
                             "Encoder - Performance Warning - Current frame cycles: {}, Average cycles: {}",
                             cur_frame_cycles, avg);
        if (m_cycle_monitor.output_file.is_open())
        {
            monitor_write_to_file(m_cycle_monitor.output_file,
                                  "Performance Warning - Current frame cycles: " + std::to_string(cur_frame_cycles) +
                                      ", Average cycles: " + std::to_string(avg));
        }
    }
    else
    {
        if (m_cycle_monitor.output_file.is_open())
        {
            monitor_write_to_file(m_cycle_monitor.output_file,
                                  "Current frame cycles: " + std::to_string(cur_frame_cycles));
        }
    }
}

void Encoder::Impl::monitor_write_to_file(std::ofstream &file, const std::string &data)
{
    std::time_t now = std::time(nullptr);
    std::tm *timeinfo = std::localtime(&now);

    char timestamp[24];
    std::strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S]", timeinfo);
    std::string timestampStr(timestamp);
    file << timestampStr << " " << data << std::endl;
}

encoder_monitors Encoder::Impl::get_monitors()
{
    encoder_monitors monitors;
    monitors.bitrate_monitor.enabled = m_bitrate_monitor.enabled;
    monitors.bitrate_monitor.fps = m_bitrate_monitor.fps;
    monitors.bitrate_monitor.period = m_bitrate_monitor.period;
    monitors.bitrate_monitor.ma_bitrate = m_bitrate_monitor.ma_bitrate;
    monitors.cycle_monitor.enabled = m_cycle_monitor.enabled;
    monitors.cycle_monitor.deviation_threshold = m_cycle_monitor.deviation_threshold;
    monitors.cycle_monitor.monitor_frames = m_cycle_monitor.monitor_frames;
    monitors.cycle_monitor.start_delay = m_cycle_monitor.start_delay;

    return monitors;
}

encoder_monitors Encoder::get_monitors()
{
    return m_impl->get_monitors();
}

u32 Encoder::Impl::get_constant_optical_zoom_boost(float optical_zoom_magnification, u32 current_bitrate)
{
    const auto &hailo_config = m_config->get_hailo_config();
    const auto &rate_control = hailo_config.rate_control;

    // Check if zoom level mode is enabled and includes ZOOM_LEVEL or BOTH
    auto mode = rate_control.zoom_bitrate_adjuster.mode.value_or(ZOOM_BITRATE_ADJUSTER_DISABLED);
    if (mode != ZOOM_BITRATE_ADJUSTER_ZOOM_LEVEL && mode != ZOOM_BITRATE_ADJUSTER_BOTH)
    {
        return current_bitrate;
    }

    float threshold = rate_control.zoom_bitrate_adjuster.zoom_level_threshold.value_or(
        DEFAULT_ZOOM_BITRATE_ADJUSTER_ZOOM_LEVEL_THRESHOLD);
    float boost_factor = rate_control.zoom_bitrate_adjuster.zoom_level_bitrate_factor.value_or(
        DEFAULT_ZOOM_BITRATE_ADJUSTER_BITRATE_FACTOR);

    u32 boosted_bitrate = static_cast<u32>(current_bitrate * boost_factor);
    // Check if current zoom level exceeds threshold
    if (optical_zoom_magnification < threshold)
    {
        boosted_bitrate = current_bitrate; // No boost
        LOGGER__MODULE__DEBUG(
            MODULE_NAME, "Optical zoom magnification {:.1f}x is below threshold {:.1f}x, no zoom level boost applied",
            optical_zoom_magnification, threshold);
    }

    return boosted_bitrate;
}

void Encoder::Impl::apply_constant_optical_zoom_boost(float optical_zoom_magnification)
{
    const auto &hailo_config = m_config->get_hailo_config();
    const auto &rate_control = hailo_config.rate_control;

    // Check if zoom level mode is enabled and includes ZOOM_LEVEL or BOTH
    auto mode = rate_control.zoom_bitrate_adjuster.mode.value_or(ZOOM_BITRATE_ADJUSTER_DISABLED);
    if (mode != ZOOM_BITRATE_ADJUSTER_ZOOM_LEVEL && mode != ZOOM_BITRATE_ADJUSTER_BOTH)
    {
        return;
    }

    // Only apply zoom level boost if zooming process boost is not active
    if (m_zooming_boost_enabled)
    {
        return;
    }
    u32 current_bitrate = m_vc_rate_cfg.bitPerSecond;
    u32 boosted_bitrate = get_constant_optical_zoom_boost(optical_zoom_magnification, current_bitrate);

    // Update rate control for zoom level boost
    VCEncRateCtrl temp_rc_cfg = m_vc_rate_cfg;
    VCEncRet ret = VCEncGetRateCtrl(m_inst, &temp_rc_cfg);
    if (temp_rc_cfg.bitPerSecond != boosted_bitrate)
    {
        temp_rc_cfg.bitPerSecond = boosted_bitrate;

        float boost_factor = rate_control.zoom_bitrate_adjuster.zoom_level_bitrate_factor.value_or(
            DEFAULT_ZOOM_BITRATE_ADJUSTER_BITRATE_FACTOR);
        // std::cout << "Applying CONSTANT zoom level boost: bitrate " << current_bitrate << " -> " << boosted_bitrate
        //             << " (factor: " << boost_factor << ") for zoom " << optical_zoom_magnification << "x"
        //             << std::endl;
        ret = VCEncSetRateCtrl(m_inst, &temp_rc_cfg);
        if (ret != VCENC_OK)
        {
            LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to set zoom level boost bitrate, error: {}", ret);
            return;
        }

        LOGGER__MODULE__INFO(MODULE_NAME,
                             "Applied zoom level boost: bitrate {} -> {} (factor: {:.1f}) for zoom {:.1f}x",
                             current_bitrate, boosted_bitrate, boost_factor, optical_zoom_magnification);
    }
}
