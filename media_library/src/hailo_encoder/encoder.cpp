/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
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

#include "encoder_class.hpp"
#include "encoder_internal.hpp"
#include "media_library_logger.hpp"

Encoder::Encoder(std::string json_string)
{
    m_impl = std::make_unique<Impl>(json_string);
}

Encoder::~Encoder() = default;

Encoder::Impl::~Impl()
{
    VCEncRelease(m_inst);
    if (m_output_memory.virtualAddress != NULL)
        EWLFreeLinear((const void *)m_ewl, &m_output_memory);
    if (NULL != m_ewl)
        (void)EWLRelease((const void *)m_ewl);
}

int Encoder::Impl::allocate_output_memory()
{
    i32 ret;
    u32 outbufSize;
    EWLInitParam_t ewl_params;
    ewl_params.clientType = EWL_CLIENT_TYPE_HEVC_ENC;
    m_ewl = (void *)EWLInit(&ewl_params);
    if (NULL == m_ewl)
    {
        return 1;
    }

    /* Limited amount of memory on some test environment */
    outbufSize = (12 * 1024 * 1024);

    ret = EWLMallocLinear((const void *)m_ewl, outbufSize, 0, &m_output_memory);
    if (ret != EWL_OK)
    {
        m_output_memory.virtualAddress = NULL;
        return 1;
    }

    m_enc_in.busOutBuf = m_output_memory.busAddress;
    m_enc_in.outBufSize = m_output_memory.size;
    m_enc_in.pOutBuf = m_output_memory.virtualAddress;
    return 0;
}

void Encoder::Impl::init_buffer_pool()
{
    m_buffer_pool = std::make_shared<MediaLibraryBufferPool>(
        m_vc_cfg.width, m_vc_cfg.height, DSP_IMAGE_FORMAT_GRAY8, 8, CMA);
    if (m_buffer_pool->init() != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR(
            "Encoder - init_buffer_pool - Failed to init buffer pool");
        // return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }
}

Encoder::Impl::Impl(std::string json_string)
    : m_config(std::make_unique<EncoderConfig>(json_string))
{
    m_multislice_encoding = false;
    m_encoder_version = VCEncGetApiVersion();
    m_encoder_build = VCEncGetBuild();
    init_gop_config();
    allocate_output_memory();
    init_encoder_config();
    init_buffer_pool();

    init_preprocessing_config();
    init_coding_control_config();
    init_rate_control_config();
}

int Encoder::get_gop_size() { return m_impl->get_gop_size(); }

int Encoder::Impl::get_gop_size() { return m_gop_cfg->get_gop_size(); }

void Encoder::force_keyframe() { m_impl->force_keyframe(); }

void Encoder::Impl::force_keyframe()
{
    LOGGER__INFO("Encoder - Force Keyframe");
    m_enc_in.codingType = m_next_coding_type = VCENC_INTRA_FRAME;
    m_enc_in.poc = 0;
    m_counters.last_idr_picture_cnt = m_counters.picture_cnt;
}

std::shared_ptr<EncoderConfig> Encoder::get_config()
{
    return m_impl->get_config();
}

std::shared_ptr<EncoderConfig> Encoder::Impl::get_config() { return m_config; }

EncoderOutputBuffer Encoder::start() { return m_impl->start(); }

EncoderOutputBuffer Encoder::Impl::start()
{
    LOGGER__INFO("Encoder - Start the stream");
    VCEncStrmStart(m_inst, &m_enc_in, &m_enc_out);
    EncoderOutputBuffer output;
    auto ret = create_output_buffer(output);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to create output buffer");
        output.buffer = nullptr;
        output.size = 0;
        return output;
    }
    // Default gop size as IPPP
    m_enc_in.poc = 0;
    // m_enc_in.gopSize =  m_next_gop_size = ((enc_params->gopSize == 0) ? 1 :
    // enc_params->gopSize);
    m_enc_in.gopSize = m_next_gop_size = get_gop_size();
    m_next_coding_type = VCENC_INTRA_FRAME;
    return output;
}

EncoderOutputBuffer Encoder::stop() { return m_impl->stop(); }

EncoderOutputBuffer Encoder::Impl::stop()
{
    VCEncStrmEnd(m_inst, &m_enc_in, &m_enc_out);
    EncoderOutputBuffer output;
    auto ret = create_output_buffer(output);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to create output buffer");
        output.buffer = nullptr;
        output.size = 0;
        return output;
    }
    return output;
}

media_library_return Encoder::Impl::update_input_buffer(EncoderInputBuffer &buf)
{
    int ret;
    u32 *luma = (u32 *)buf.m_planes[0].data;
    u32 luma_size = buf.m_planes[0].size;
    ret = EWLGetBusAddress(m_ewl, luma, (u32 *)&(m_enc_in.busLuma), luma_size);
    if (ret != EWL_OK)
    {
        LOGGER__ERROR("Could not get physical address of luma");
        return MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS;
    }
    if (buf.m_planes.size() > 1)
    {
        u32 *chroma = (u32 *)buf.m_planes[1].data;
        u32 chroma_size = buf.m_planes[1].size;
        ret = EWLGetBusAddress(m_ewl, chroma, (u32 *)&(m_enc_in.busChromaU),
                               chroma_size);
        if (ret != EWL_OK)
        {
            LOGGER__ERROR("Could not get physical address of chroma");
            return MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS;
        }

        if (buf.m_planes.size() > 2)
        {
            u32 *chroma_v = (u32 *)buf.m_planes[2].data;
            u32 chroma_v_size = buf.m_planes[2].size;
            ret = EWLGetBusAddress(
                m_ewl, chroma_v, (u32 *)&(m_enc_in.busChromaV), chroma_v_size);
            if (ret != EWL_OK)
            {
                LOGGER__ERROR("Could not get physical address of chroma v");
                return MEDIA_LIBRARY_ENCODER_COULD_NOT_GET_PHYSICAL_ADDRESS;
            }
        }
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return
Encoder::Impl::create_output_buffer(EncoderOutputBuffer &output_buf)
{
    hailo_media_library_buffer buffer;
    if (m_buffer_pool->acquire_buffer(buffer) != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to acquire buffer");
        return MEDIA_LIBRARY_BUFFER_ALLOCATION_ERROR;
    }
    memcpy(buffer.get_plane(0), m_enc_in.pOutBuf, m_enc_out.streamSize);
    output_buf.buffer =
        std::make_shared<hailo_media_library_buffer>(std::move(buffer));
    output_buf.size = m_enc_out.streamSize;

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return
Encoder::Impl::encode_multiple_frames(std::vector<EncoderOutputBuffer> &outputs)
{
    LOGGER__DEBUG("Encoder - encode_multiple_frames");
    media_library_return ret = MEDIA_LIBRARY_UNINITIALIZED;
    auto gop_size = m_enc_in.gopSize;
    // Assuming enc_params->encIn.gopSize is not 0.
    for (uint8_t i = 0; i < gop_size; i++)
    {
        auto idx = m_enc_in.gopPicIdx +
                   m_gop_cfg->get_gop_cfg_offset()[m_enc_in.gopSize];
        auto poc = m_gop_cfg->get_gop_pic_cfg()[idx].poc;
        ret = encode_frame(m_inputs[poc - 1], outputs);
        if (ret != MEDIA_LIBRARY_SUCCESS)
        {
            LOGGER__ERROR("Error encoding frame {} with error {}", i, ret);
            break;
        }
    }
    return ret;
}

static int64_t time_diff(const struct timespec after,
                         const struct timespec before)
{
    return ((int64_t)after.tv_sec - (int64_t)before.tv_sec) * (int64_t)1000 +
           ((int64_t)after.tv_nsec - (int64_t)before.tv_nsec) / 1000000;
}

media_library_return
Encoder::Impl::encode_frame(EncoderInputBuffer &buf,
                            std::vector<EncoderOutputBuffer> &outputs)
{
    LOGGER__DEBUG("Encoder - encode_frame");
    VCEncRet enc_ret;
    media_library_return ret = MEDIA_LIBRARY_UNINITIALIZED;
    hailo_media_library_buffer buffer;
    struct timespec start_encode, end_encode;
    ret = update_input_buffer(buf);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Encoder - encode_frame - Failed to update input buffer");
        return ret;
    }

    m_enc_in.codingType =
        (m_enc_in.poc == 0) ? VCENC_INTRA_FRAME : m_next_coding_type;
    if (m_enc_in.codingType == VCENC_INTRA_FRAME)
    {
        m_enc_in.poc = 0;
        m_counters.last_idr_picture_cnt = m_counters.picture_cnt;
    }
    clock_gettime(CLOCK_MONOTONIC, &start_encode);
    enc_ret = VCEncStrmEncode(m_inst, &m_enc_in, &m_enc_out, NULL, NULL);
    clock_gettime(CLOCK_MONOTONIC, &end_encode);
    LOGGER__DEBUG("Encoding of frame took {} ms",
                  time_diff(end_encode, start_encode));
    switch (enc_ret)
    {
    case VCENC_FRAME_READY:
        m_counters.picture_enc_cnt++;
        if (m_enc_out.streamSize == 0)
        {
            m_counters.picture_cnt++;
        }
        else
        {
            if (!m_multislice_encoding)
            {
                EncoderOutputBuffer output;
                ret = create_output_buffer(output);
                if (ret != MEDIA_LIBRARY_SUCCESS)
                {
                    LOGGER__ERROR("Encoder - encode_frame - Failed to create "
                                  "output buffer");
                    return ret;
                }
                outputs.emplace_back(std::move(output));
            }
            // pEncIn->timeIncrement = enc_params->frameRateDenom;
            m_counters.validencodedframenumber++;
            m_next_coding_type = find_next_pic();
        }
        ret = MEDIA_LIBRARY_SUCCESS;
        break;
    default:
        LOGGER__ERROR("Encoder - encode_frame - Error encoding frame {}",
                      enc_ret);
        ret = MEDIA_LIBRARY_ENCODER_ENCODE_ERROR;
        break;
    }
    return ret;
}

std::vector<EncoderOutputBuffer> Encoder::handle_frame(EncoderInputBuffer buf)
{
    return m_impl->handle_frame(buf);
}

std::vector<EncoderOutputBuffer>
Encoder::Impl::handle_frame(EncoderInputBuffer buf)
{
    LOGGER__DEBUG("Start Handling Frame");
    std::vector<EncoderOutputBuffer> outputs;
    outputs.clear();
    media_library_return ret = MEDIA_LIBRARY_UNINITIALIZED;
    switch (m_next_coding_type)
    {
    case VCENC_INTRA_FRAME:
    {
        ret = encode_frame(buf, outputs);
        break;
    }
    case VCENC_PREDICTED_FRAME:
    {
        if (m_inputs.size() == (size_t)m_enc_in.gopSize - 1)
        {
            m_inputs.emplace_back(std::move(buf));
            ret = encode_multiple_frames(outputs);
            m_inputs.clear();
        }
        else if (m_inputs.size() < (size_t)m_enc_in.gopSize - 1)
        {
            m_inputs.emplace_back(std::move(buf));
        }
        else
        {
            LOGGER__ERROR("Encoder Error - Too many inputs");
        }
        break;
    }
    case VCENC_BIDIR_PREDICTED_FRAME:
    {
        LOGGER__ERROR("Encoder Error - BIDIR Predicted Frame");
        break;
    }
    default:
    {
        LOGGER__ERROR("Encoder Error - Unknown coding type");
        break;
    }
    }
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Encoder Error - encoding frame returned {}", ret);
    }
    LOGGER__DEBUG("Encoder - handle_frame - returns {} outputs",
                  outputs.size());
    return outputs;
}

VCEncPictureCodingType Encoder::Impl::find_next_pic()
{
    VCEncPictureCodingType nextCodingType;
    int idx, offset, cur_poc, delta_poc_to_next;
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
        offset = gop_cfg_offset[m_next_gop_size];
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
            if ((m_counters.idr_interval) &&
                ((gop_end_pic - m_counters.last_idr_picture_cnt) >=
                 (int)m_counters.idr_interval))
                gop_shorten =
                    1 + ((gop_end_pic - m_counters.last_idr_picture_cnt) -
                         m_counters.idr_interval);

            if (gop_shorten >= m_next_gop_size)
            {
                // for gopsize = 1
                m_counters.picture_cnt = picture_cnt_tmp + 1 - cur_poc;
            }
            else if (gop_shorten > 0)
            {
                // reduce gop size
                const int max_reduced_gop_size = 4;
                m_next_gop_size -= gop_shorten;
                if (m_next_gop_size > max_reduced_gop_size)
                    m_next_gop_size = max_reduced_gop_size;

                idx = gop_cfg_offset[m_next_gop_size];
                delta_poc_to_next = gop_cfg->pGopPicCfg[idx].poc - cur_poc;
                m_counters.picture_cnt = picture_cnt_tmp + delta_poc_to_next;
            }
            m_enc_in.gopSize = m_next_gop_size;
        }

        m_enc_in.poc += m_counters.picture_cnt - picture_cnt_tmp;
        // next coding type
        bool forceIntra =
            m_counters.idr_interval &&
            ((m_counters.picture_cnt - m_counters.last_idr_picture_cnt) >=
             (int)m_counters.idr_interval);
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
        gop_cfg->delta_poc_to_next =
            gop_cfg->pGopPicCfg[gop_cfg->id_next].poc - next_poc;
    }

    return nextCodingType;
}