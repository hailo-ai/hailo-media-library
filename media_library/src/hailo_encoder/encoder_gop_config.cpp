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

#include "encoder_class.hpp"
#include "media_library_logger.hpp"
#include "encoder_internal.hpp"
#include "encoder_gop_config.hpp"

int Encoder::Impl::gopConfig::ParseGopConfigLine(GopPicConfig &pic_cfg, int gopSize)
{
    if (m_gop_cfg->size >= MAX_GOP_PIC_CONFIG_NUM)
    {
        LOGGER__ERROR("GOP Config: Error, Gop size is out of range\n");
        return -1;
    }
    VCEncGopPicConfig *cfg = &(m_gop_cfg->pGopPicCfg[m_gop_cfg->size++]);

    cfg->codingType = pic_cfg.m_type;
    cfg->poc = pic_cfg.m_poc;
    cfg->QpOffset = pic_cfg.m_qp_offset;
    cfg->QpFactor = pic_cfg.m_qp_factor;
    cfg->temporalId = 0;
    cfg->numRefPics = pic_cfg.m_num_ref_pics;
    if (pic_cfg.m_num_ref_pics < 0 || pic_cfg.m_num_ref_pics > VCENC_MAX_REF_FRAMES)
    {
        printf("GOP Config: Error, num_ref_pic can not be more than %d \n", VCENC_MAX_REF_FRAMES);
        return -1;
    }
    for (int i = 0; i < pic_cfg.m_num_ref_pics; i++)
    {
        cfg->refPics[i].ref_pic = pic_cfg.m_ref_pics[i];
        cfg->refPics[i].used_by_cur = pic_cfg.m_used_by_cur[i];
    }
    return 0;
}
int Encoder::Impl::gopConfig::ReadGopConfig(std::vector<GopPicConfig> &config, int gopSize)
{
    int ret = -1;

    if (m_gop_cfg->size >= MAX_GOP_PIC_CONFIG_NUM)
        return -1;

    if (m_gop_cfg_offset)
        m_gop_cfg_offset[gopSize] = m_gop_cfg->size;

    for (auto &pic_cfg : config)
    {
        if (ParseGopConfigLine(pic_cfg, gopSize) == -1)
            return -1;
    }
    ret = 0;
    return ret;
}

media_library_return Encoder::Impl::gopConfig::init_config(VCEncGopConfig *gopConfig, int gop_size,
                                                           int b_frame_qp_delta, bool codec_h264)
{
    m_gop_cfg = gopConfig;
    memset(m_gop_pic_cfg, 0, sizeof(m_gop_pic_cfg));
    m_gop_cfg->pGopPicCfg = m_gop_pic_cfg;
    m_b_frame_qp_delta = b_frame_qp_delta;
    m_codec_h264 = codec_h264;
    m_gop_size = gop_size;
    int i, pre_load_num;
    std::vector<std::vector<GopPicConfig>> default_configs = {m_codec_h264 ? RpsDefault_H264_GOPSize_1
                                                                           : RpsDefault_GOPSize_1,
                                                              RpsDefault_GOPSize_2,
                                                              RpsDefault_GOPSize_3,
                                                              RpsDefault_GOPSize_4,
                                                              RpsDefault_GOPSize_5,
                                                              RpsDefault_GOPSize_6,
                                                              RpsDefault_GOPSize_7,
                                                              RpsDefault_GOPSize_8};

    if (m_gop_size < 0 || m_gop_size > MAX_GOP_SIZE)
    {
        printf("GOP Config: Error, Invalid GOP Size\n");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    // GOP size in rps array for gopSize=N
    // N<=4:      GOP1, ..., GOPN
    // 4<N<=8:   GOP1, GOP2, GOP3, GOP4, GOPN
    // N > 8:       GOP1, GOPN
    // Adaptive:  GOP1, GOP2, GOP3, GOP4, GOP6, GOP8
    if (m_gop_size > 8)
        pre_load_num = 1;
    else if (m_gop_size >= 4 || m_gop_size == 0)
        pre_load_num = 4;
    else
        pre_load_num = m_gop_size;

    m_gop_cfg->ltrInterval = 0;
    for (i = 1; i <= pre_load_num; i++)
    {
        if (ReadGopConfig(default_configs[i - 1], i))
        {
            printf("GOP Config: Error, could not read config %d\n", i);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    if (m_gop_size == 0)
    {
        // gop6
        if (ReadGopConfig(default_configs[5], 6))
        {
            printf("GOP Config: Error, could not read config %d\n", 6);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        // gop8
        if (ReadGopConfig(default_configs[7], 8))
        {
            printf("GOP Config: Error, could not read config %d\n", 8);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }
    else if (m_gop_size > 4)
    {
        // gopSize
        if (ReadGopConfig(default_configs[m_gop_size - 1], m_gop_size))
        {
            printf("GOP Config: Error, could not read config %d\n", m_gop_size);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    if (m_gop_cfg->ltrInterval > 0)
    {
        for (i = 0; i < (m_gop_size == 0 ? m_gop_cfg->size : m_gop_cfg_offset[m_gop_size]); i++)
        {
            // when use long-term, change P to B in default configs (used for last gop)
            VCEncGopPicConfig *cfg = &(m_gop_cfg->pGopPicCfg[i]);
            if (cfg->codingType == VCENC_PREDICTED_FRAME)
                cfg->codingType = VCENC_BIDIR_PREDICTED_FRAME;
        }
    }

    // Compatible with old bFrameQpDelta setting
    if (m_b_frame_qp_delta >= 0)
    {
        for (i = 0; i < m_gop_cfg->size; i++)
        {
            VCEncGopPicConfig *cfg = &(m_gop_cfg->pGopPicCfg[i]);
            if (cfg->codingType == VCENC_BIDIR_PREDICTED_FRAME)
                cfg->QpOffset = m_b_frame_qp_delta;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

Encoder::Impl::gopConfig::gopConfig(VCEncGopConfig *gopConfig, int gopSize, int bFrameQpDelta, bool codecH264)
    : m_gop_cfg(gopConfig), m_gop_size(gopSize), m_b_frame_qp_delta(bFrameQpDelta), m_codec_h264(codecH264)
{
}

int Encoder::Impl::gopConfig::get_gop_size() const
{
    return m_gop_size;
}
