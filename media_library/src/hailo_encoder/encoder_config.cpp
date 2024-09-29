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
#include "config_manager.hpp"
#include "encoder_class.hpp"
#include "encoder_config.hpp"
#include "encoder_internal.hpp"
#include "media_library_logger.hpp"
#include "encoder_config_presets.hpp"

#define MIN_MONITOR_FRAMES (10)
#define MAX_MONITOR_FRAMES (120)
#define VCENC_MAX_BITRATE (100000*1000)  /* Level 6 main tier limit */
#define VCENC_MIN_BITRATE (10000)

inline void strip_string_syntax(std::string &pipeline_input)
{
    if (pipeline_input.front() == '\'' && pipeline_input.back() == '\'')
    {
        pipeline_input.erase(0, 1);
        pipeline_input.pop_back();
    }
}

EncoderConfig::EncoderConfig(const std::string &json_string)
    : m_json_string(json_string)
{
    if (configure(json_string) != MEDIA_LIBRARY_SUCCESS)
    {
        throw std::runtime_error("encoder's JSON config file is not valid");
    }
}

media_library_return EncoderConfig::configure(const std::string &json_string)
{
    std::string strapped_json = json_string;
    strip_string_syntax(strapped_json);

    m_config_manager =
        std::make_shared<ConfigManager>(ConfigSchema::CONFIG_SCHEMA_ENCODER);

    media_library_return config_ret = m_config_manager->config_string_to_struct<encoder_config_t>(strapped_json, m_config);
    if (config_ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("encoder's JSON config conversion failed: {}", strapped_json);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    nlohmann::json parsed_json = nlohmann::json::parse(strapped_json);
    std::string encoder_name;

    type = ConfigManager::get_encoder_type(parsed_json);
    switch (type)
    {
    case EncoderType::Jpeg:
        encoder_name = "jpeg_encoder";
        break;
    case EncoderType::Hailo:
        encoder_name = "hailo_encoder";
        break;
    case EncoderType::None:
        // Should not get here, config_string_to_struct would have returned an error in this case
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    m_doc = parsed_json["encoding"][encoder_name];
    m_json_string = strapped_json;
    m_user_config = m_config;

    if (type == EncoderType::Hailo)
    {
        auto &config = std::get<hailo_encoder_config_t>(m_config);
        if (EncoderConfigPresets::get_instance().apply_preset(config) != MEDIA_LIBRARY_SUCCESS)
        {
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return EncoderConfig::configure(const encoder_config_t &encoder_config)
{
    m_user_config = m_config = encoder_config;

    if (type == EncoderType::Hailo)
    {
        auto &config = std::get<hailo_encoder_config_t>(m_config);
        if (EncoderConfigPresets::get_instance().apply_preset(config) != MEDIA_LIBRARY_SUCCESS)
        {
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

encoder_config_t EncoderConfig::get_config()
{
    return m_config;
}

encoder_config_t EncoderConfig::get_user_config()
{
    return m_user_config;
}

hailo_encoder_config_t EncoderConfig::get_hailo_config()
{
    return std::get<hailo_encoder_config_t>(m_config);
}

jpeg_encoder_config_t EncoderConfig::get_jpeg_config()
{
    return std::get<jpeg_encoder_config_t>(m_config);
}

const nlohmann::json &EncoderConfig::get_doc() const { return m_doc; }

bool Encoder::Impl::gop_config_update_required(const hailo_encoder_config_t &old_config, const hailo_encoder_config_t &new_config)
{
    if (new_config.gop.gop_size != old_config.gop.gop_size || new_config.gop.b_frame_qp_delta != old_config.gop.b_frame_qp_delta)
    {
        return true;
    }

    return false;
}

bool Encoder::Impl::hard_restart_required(const hailo_encoder_config_t &old_config, const hailo_encoder_config_t &new_config,
                                          bool gop_update_required)
{
    // Output stream configs requires hard restart
    if ((new_config.output_stream.codec != old_config.output_stream.codec) ||
        (new_config.output_stream.level.value() != old_config.output_stream.level.value()) ||
        (new_config.output_stream.profile.value() != old_config.output_stream.profile.value()))
    {
        return true;
    }

    // Input stream configs requires hard restart
    if ((new_config.input_stream.width != old_config.input_stream.width) ||
        (new_config.input_stream.height != old_config.input_stream.height) ||
        (new_config.input_stream.framerate != old_config.input_stream.framerate) ||
        (new_config.input_stream.format != old_config.input_stream.format))
    {
        return true;
    }

    // Gop size change requires hard restart
    if (gop_update_required)
    {
        return true;
    }

    return false;
}

uint32_t Encoder::Impl::get_codec()
{
    auto output_stream = m_config->get_hailo_config().output_stream;
    if (output_stream.codec == CODEC_TYPE_H264)
        return 1;
    else if (output_stream.codec == CODEC_TYPE_HEVC)
        return 0;
    else
        return 0;
}

VCEncProfile Encoder::Impl::get_profile(bool codecH264)
{
    auto output_stream = m_config->get_hailo_config().output_stream;
    std::string profile = output_stream.profile.value();

    if (profile == "auto")
    {
        auto resolution = m_config->get_hailo_config().input_stream.width * m_config->get_hailo_config().input_stream.height;
        auto bitrate = m_config->get_hailo_config().rate_control.bitrate.target_bitrate;

        if ((resolution <= 1280 * 720) && (bitrate <= 5000000))
        {
            return (codecH264 ? VCENC_H264_MAIN_PROFILE : VCENC_HEVC_MAIN_PROFILE);
        }
        else
        {
            return (codecH264 ? VCENC_H264_HIGH_PROFILE : VCENC_HEVC_MAIN_10_PROFILE);
        }
    }
    else if (profile == "VCENC_H264_BASE_PROFILE")
        return VCENC_H264_BASE_PROFILE;
    else if (profile == "VCENC_H264_MAIN_PROFILE")
        return VCENC_H264_MAIN_PROFILE;
    else if (profile == "VCENC_H264_HIGH_PROFILE")
        return VCENC_H264_HIGH_PROFILE;
    else if (profile == "VCENC_HEVC_MAIN_STILL_PICTURE_PROFILE")
        return VCENC_HEVC_MAIN_STILL_PICTURE_PROFILE;
    else
        return VCENC_HEVC_MAIN_PROFILE;
}

VCEncPictureType Encoder::Impl::get_input_format(std::string format)
{
    if (input_formats.find(format) != input_formats.end())
        return input_formats.find(format)->second;
    else
        throw std::invalid_argument("Invalid format");
}

media_library_return Encoder::Impl::validate_bitrate_limitations(rate_control_config_t rate_control_config)
{
    if(rate_control_config.picture_rc || rate_control_config.picture_skip || rate_control_config.hrd)
    {
        if(rate_control_config.bitrate.target_bitrate > VCENC_MAX_BITRATE)
        {
            LOGGER__ERROR("Requested bitrate ({}) is higher than the maximum supported bitrate ({})", rate_control_config.bitrate.target_bitrate, VCENC_MAX_BITRATE);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
        if(rate_control_config.bitrate.target_bitrate < VCENC_MIN_BITRATE)
        {
            LOGGER__ERROR("Requested bitrate ({}) is lower than the minimum supported bitrate ({})", rate_control_config.bitrate.target_bitrate, VCENC_MIN_BITRATE);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::Impl::validate_level_limitations(std::string level, bool codecH264, u32 width, u32 height, u32 framerate, u32 framerate_denom)
{
    if (codecH264)
        return MEDIA_LIBRARY_SUCCESS;

    auto find_level = h265_level.find(level);
    if(find_level == h265_level.end())
    {
        LOGGER__ERROR("Requested level ({}) does not exist in the supported HEVC levels", level);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    //get the index of the level in the map
    auto level_index = std::distance(h265_level.begin(), find_level);
    u32 max_fs;
    if (VCEncGetMaxFS((u32)level_index, &max_fs) != VCENC_OK)
    {
        LOGGER__ERROR("Failed to get max resolution limitation for level {}", level);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    if ((u32)(width * height) > max_fs)
    {
        LOGGER__ERROR("Requested HEVC level {} is not supported for requested resolution {}x{}. Please refer to to the limitations on the hailo_encoder documentation", level, width, height);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    u32 max_sbps;
    if (VCEncGetMaxSBPS((u32)level_index, &max_sbps) != VCENC_OK)
    {
        LOGGER__ERROR("Failed to get framerate limitation for level {}", level);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    u32 sample_rate = (framerate * width * height) / framerate_denom;
    if (sample_rate > max_sbps)
    {
        LOGGER__ERROR("Requested HEVC level {} is not supported for requested resolution {}x{} and framerate {}. Please refer to to the limitations on the hailo_encoder documentation", level, width, height, framerate);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::Impl::get_level(std::string level, bool codecH264, u32 width, u32 height, u32 framerate, u32 framerate_denom, VCEncLevel &vc_level_out)
{
    if (level == "auto")
    {
        auto resolution = m_config->get_hailo_config().input_stream.width * m_config->get_hailo_config().input_stream.height;
        auto bitrate = m_config->get_hailo_config().rate_control.bitrate.target_bitrate;
        const auto &auto_level_map = codecH264 ? h264_auto_level_map : h265_auto_level_map;

        for (auto &resolution_map : auto_level_map)
        {
            if (resolution <= resolution_map.first)
            {
                for (auto &bitrate_map : resolution_map.second)
                {
                    if (bitrate <= bitrate_map.first)
                    {
                        level = bitrate_map.second;
                        break;
                    }
                }

                break;
            }
        }
    }

    if(validate_level_limitations(level, codecH264, width, height, framerate, framerate_denom) != MEDIA_LIBRARY_SUCCESS)
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;

    if (codecH264)
    {
        if (h264_level.find(level) != h264_level.end())
        {
            vc_level_out = h264_level.find(level)->second;
        }
        else
        {
            LOGGER__ERROR("Invalid H264 level: {}", level);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }
    else
    {
        if (h265_level.find(level) != h265_level.end())
        {
            vc_level_out = h265_level.find(level)->second;
        }
        else
        {
            LOGGER__ERROR("Invalid HEVC level: {}", level);
            return MEDIA_LIBRARY_CONFIGURATION_ERROR;
        }
    }

    return MEDIA_LIBRARY_SUCCESS;
}

void Encoder::Impl::updateArea(coding_roi_area_t &area, VCEncPictureArea &vc_area)
{
    if (area.enable)
    {
        vc_area.enable = 1;
        vc_area.top = area.top;
        vc_area.left = area.left;
        vc_area.bottom = area.bottom;
        vc_area.right = area.right;
        // vc_area.qpDelta = area.qp_delta;
    }
    else
    {
        vc_area.enable = 0;
        vc_area.top = vc_area.left = vc_area.bottom = vc_area.right = -1;
    }
}

void Encoder::Impl::updateArea(coding_roi_t &area, VCEncPictureArea &vc_area)
{
    if (area.enable)
    {
        vc_area.enable = 1;
        vc_area.top = area.top;
        vc_area.left = area.left;
        vc_area.bottom = area.bottom;
        vc_area.right = area.right;
    }
    else
    {
        vc_area.enable = 0;
        vc_area.top = vc_area.left = vc_area.bottom = vc_area.right = -1;
    }
}

void Encoder::Impl::create_gop_config()
{
    LOGGER__DEBUG("Encoder - init_gop_config");
    auto codec = get_codec();
    auto gop_config_json = m_config->get_hailo_config().gop;
    int32_t bframe_qp_delta = gop_config_json.b_frame_qp_delta;
    int32_t gop_size = gop_config_json.gop_size;
    memset(&m_enc_in.gopConfig, 0, sizeof(VCEncGopConfig));
    m_gop_cfg = std::make_unique<gopConfig>(&(m_enc_in.gopConfig), gop_size,
                                            bframe_qp_delta, codec);
}

media_library_return Encoder::Impl::init_gop_config()
{
    auto gop_config = m_config->get_hailo_config().gop;
    auto codec = get_codec();

    memset(&m_enc_in.gopConfig, 0, sizeof(VCEncGopConfig));
    memset(&m_enc_in.gopConfig.pGopPicCfg, 0, sizeof(VCEncGopPicConfig));
    media_library_return ret = m_gop_cfg->init_config(&(m_enc_in.gopConfig), gop_config.gop_size, gop_config.b_frame_qp_delta, codec);
    if (ret != MEDIA_LIBRARY_SUCCESS)
    {
        LOGGER__ERROR("Failed to init gop config");
        return ret;
    }

    m_enc_in.gopConfig.pGopPicCfg = m_gop_cfg->get_gop_pic_cfg();
    // m_enc_in.gopSize = m_gop_cfg->get_gop_size();
    return ret;
}

media_library_return Encoder::Impl::init_rate_control_config()
{
    LOGGER__DEBUG("Encoder - init_rate_control_config");
    VCEncRet ret = VCENC_OK;

    auto rate_control = m_config->get_hailo_config().rate_control;
    /* Encoder setup: rate control */
    if ((ret = VCEncGetRateCtrl(m_inst, &m_vc_rate_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
        LOGGER__ERROR("Failed to get rate control configuration on VCEnc error code {}", ret);
        return MEDIA_LIBRARY_ERROR;
    }

    m_vc_rate_cfg.qpHdr = rate_control.quantization.qp_hdr;
    m_vc_rate_cfg.qpMin = rate_control.quantization.qp_min.value();
    m_vc_rate_cfg.qpMax = rate_control.quantization.qp_max.value();
    m_vc_rate_cfg.pictureSkip = rate_control.picture_skip;
    m_vc_rate_cfg.pictureRc = rate_control.picture_rc;
    m_vc_rate_cfg.ctbRc = rate_control.ctb_rc.value();

    switch (rate_control.block_rc_size.value())
    {
    case 64:
        m_vc_rate_cfg.blockRCSize = 0;
        break;
    case 32:
        m_vc_rate_cfg.blockRCSize = 1;
        break;
    case 16:
        m_vc_rate_cfg.blockRCSize = 2;
        break;
    default:
        throw std::invalid_argument("Invalid block_rc_size");
    }

    m_vc_rate_cfg.bitPerSecond = rate_control.bitrate.target_bitrate;
    m_vc_rate_cfg.bitVarRangeI = rate_control.bitrate.bit_var_range_i.value();
    m_vc_rate_cfg.bitVarRangeP = rate_control.bitrate.bit_var_range_p.value();
    m_vc_rate_cfg.bitVarRangeB = rate_control.bitrate.bit_var_range_b.value();
    m_vc_rate_cfg.tolMovingBitRate = rate_control.bitrate.tolerance_moving_bitrate.value();

    if (validate_bitrate_limitations(rate_control) != MEDIA_LIBRARY_SUCCESS)
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;

    uint32_t monitor_frames = rate_control.monitor_frames.value();
    if (monitor_frames != 0)
        m_vc_rate_cfg.monitorFrames = monitor_frames;
    else
        m_vc_rate_cfg.monitorFrames =
            (m_vc_cfg.frameRateNum + m_vc_cfg.frameRateDenom - 1) /
            m_vc_cfg.frameRateDenom;

    if (m_vc_rate_cfg.monitorFrames > MAX_MONITOR_FRAMES)
        m_vc_rate_cfg.monitorFrames = MAX_MONITOR_FRAMES;
    if (m_vc_rate_cfg.monitorFrames < MIN_MONITOR_FRAMES)
        m_vc_rate_cfg.monitorFrames = MIN_MONITOR_FRAMES;

    m_vc_rate_cfg.hrd = rate_control.hrd.value();
    m_vc_rate_cfg.hrdCbrFlag = rate_control.padding.value();
    m_vc_rate_cfg.cvbr = rate_control.cvbr.value();

    if (rate_control.hrd_cpb_size.value() == 0)
    {
        m_vc_rate_cfg.hrdCpbSize = rate_control.bitrate.target_bitrate * 2;
    }
    else
    {
        m_vc_rate_cfg.hrdCpbSize = rate_control.hrd_cpb_size.value();
    }

    if (rate_control.gop_length.value() == 0)
    {
        m_vc_rate_cfg.gopLen =
            (m_vc_cfg.frameRateNum + m_vc_cfg.frameRateDenom - 1) / m_vc_cfg.frameRateDenom;
    }
    else
    {
        m_vc_rate_cfg.gopLen = rate_control.gop_length.value();
    }

    m_intra_pic_rate = rate_control.intra_pic_rate;

    m_vc_rate_cfg.intraQpDelta = rate_control.quantization.intra_qp_delta.value();
    m_vc_rate_cfg.fixedIntraQp = rate_control.quantization.fixed_intra_qp.value();

    if ((ret = VCEncSetRateCtrl(m_inst, &m_vc_rate_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
        LOGGER__ERROR("Failed to set rate control configuration on VCEnc error code {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::Impl::init_coding_control_config()
{
    LOGGER__DEBUG("Encoder - init_coding_control_config");
    VCEncRet ret = VCENC_OK;
    auto coding_control = m_config->get_hailo_config().coding_control;

    /* Encoder setup: coding control */
    if ((ret = VCEncGetCodingCtrl(m_inst, &m_vc_coding_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
        LOGGER__ERROR("Failed to get coding control configuration on VCEnc error code {}", ret);
        return MEDIA_LIBRARY_ERROR;
    }

    m_vc_coding_cfg.sliceSize = 0;
    m_vc_coding_cfg.disableDeblockingFilter = 0;
    m_vc_coding_cfg.tc_Offset = coding_control.deblocking_filter.tc_offset;
    m_vc_coding_cfg.beta_Offset = coding_control.deblocking_filter.beta_offset;
    m_vc_coding_cfg.enableSao = 1;
    m_vc_coding_cfg.enableDeblockOverride = coding_control.deblocking_filter.deblock_override ? 1 : 0;
    m_vc_coding_cfg.deblockOverride = coding_control.deblocking_filter.deblock_override ? 1 : 0;
    m_vc_coding_cfg.enableCabac = 1;
    m_vc_coding_cfg.cabacInitFlag = 0;
    m_vc_coding_cfg.vuiVideoFullRange = 1;
    m_vc_coding_cfg.seiMessages = coding_control.sei_messages ? 1 : 0;

    /* Disabled */
    m_vc_coding_cfg.gdrDuration = 0;
    m_vc_coding_cfg.fieldOrder = 0;

    m_vc_coding_cfg.cirStart = 0;
    m_vc_coding_cfg.cirInterval = 0;

    m_vc_coding_cfg.pcm_loop_filter_disabled_flag = 0;

    updateArea(coding_control.roi_area1, m_vc_coding_cfg.roi1Area);
    updateArea(coding_control.roi_area2, m_vc_coding_cfg.roi2Area);
    updateArea(coding_control.intra_area, m_vc_coding_cfg.intraArea);
    updateArea(coding_control.ipcm_area1, m_vc_coding_cfg.ipcm1Area);
    updateArea(coding_control.ipcm_area2, m_vc_coding_cfg.ipcm2Area);

    // TODO: check if need to be changed.
    m_vc_coding_cfg.ipcmMapEnable = 0;
    m_vc_coding_cfg.pcm_enabled_flag = 0;

    m_vc_coding_cfg.codecH264 = m_vc_cfg.codecH264;

    m_vc_coding_cfg.roiMapDeltaQpEnable = 0;
    m_vc_coding_cfg.roiMapDeltaQpBlockUnit = 0;

    m_vc_coding_cfg.enableScalingList = 0;
    m_vc_coding_cfg.chroma_qp_offset = 0;

    /* low latency */
    m_vc_coding_cfg.inputLineBufEn = 0;
    m_vc_coding_cfg.inputLineBufLoopBackEn = 0;
    m_vc_coding_cfg.inputLineBufDepth = 0;
    m_vc_coding_cfg.inputLineBufHwModeEn = 0;
    m_vc_coding_cfg.inputLineBufCbFunc = VCEncInputLineBufDone;
    m_vc_coding_cfg.inputLineBufCbData = NULL;

    /* denoise */
    m_vc_coding_cfg.noiseReductionEnable = 0;
    m_vc_coding_cfg.noiseLow = 10;
    m_vc_coding_cfg.firstFrameSigma = 11;

    if ((ret = VCEncSetCodingCtrl(m_inst, &m_vc_coding_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
        LOGGER__ERROR("Failed to set coding control configuration on VCEnc error code {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }
    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::Impl::init_preprocessing_config()
{
    LOGGER__DEBUG("Encoder - init_preprocessing_config");
    VCEncRet ret;
    /* PreP setup */
    if ((ret = VCEncGetPreProcessing(m_inst, &m_vc_pre_proc_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
        LOGGER__ERROR("Failed to get pre processing configuration on VCEnc error code {}", ret);
        return MEDIA_LIBRARY_ERROR;
    }
    auto input_stream = m_config->get_hailo_config().input_stream;

    m_vc_pre_proc_cfg.inputType =
        get_input_format(std::string(input_stream.format));
    // No Rotation
    m_vc_pre_proc_cfg.rotation = (VCEncPictureRotation)0;

    m_vc_pre_proc_cfg.origWidth = m_input_stride;
    m_vc_pre_proc_cfg.origHeight = input_stream.height;

    m_vc_pre_proc_cfg.xOffset = 0;
    m_vc_pre_proc_cfg.yOffset = 0;
    m_vc_pre_proc_cfg.colorConversion.type = (VCEncColorConversionType)0;

    // For the future RGB to YUV
    if (m_vc_pre_proc_cfg.colorConversion.type == VCENC_RGBTOYUV_USER_DEFINED)
    {
        m_vc_pre_proc_cfg.colorConversion.coeffA = 20000;
        m_vc_pre_proc_cfg.colorConversion.coeffB = 44000;
        m_vc_pre_proc_cfg.colorConversion.coeffC = 5000;
        m_vc_pre_proc_cfg.colorConversion.coeffE = 35000;
        m_vc_pre_proc_cfg.colorConversion.coeffF = 38000;
    }

    m_vc_pre_proc_cfg.scaledWidth = 0;
    m_vc_pre_proc_cfg.scaledHeight = 0;

    m_vc_pre_proc_cfg.busAddressScaledBuff = 0;
    m_vc_pre_proc_cfg.virtualAddressScaledBuff = NULL;
    m_vc_pre_proc_cfg.sizeScaledBuff = 0;
    m_vc_pre_proc_cfg.alignment = 0;

    /* Set overlay area*/
    for (int i = 0; i < MAX_OVERLAY_NUM; i++)
    {
        m_vc_pre_proc_cfg.overlayArea[i].xoffset = 0;
        m_vc_pre_proc_cfg.overlayArea[i].cropXoffset = 0;
        m_vc_pre_proc_cfg.overlayArea[i].yoffset = 0;
        m_vc_pre_proc_cfg.overlayArea[i].cropYoffset = 0;
        m_vc_pre_proc_cfg.overlayArea[i].width = 0;
        m_vc_pre_proc_cfg.overlayArea[i].cropWidth = 0;
        m_vc_pre_proc_cfg.overlayArea[i].height = 0;
        m_vc_pre_proc_cfg.overlayArea[i].cropHeight = 0;
        m_vc_pre_proc_cfg.overlayArea[i].format = 0;
        m_vc_pre_proc_cfg.overlayArea[i].alpha = 0;
        m_vc_pre_proc_cfg.overlayArea[i].enable = 0;
        m_vc_pre_proc_cfg.overlayArea[i].Ystride = 0;
        m_vc_pre_proc_cfg.overlayArea[i].UVstride = 0;
        m_vc_pre_proc_cfg.overlayArea[i].bitmapY = 0;
        m_vc_pre_proc_cfg.overlayArea[i].bitmapU = 0;
        m_vc_pre_proc_cfg.overlayArea[i].bitmapV = 0;
    }

    if ((ret = VCEncSetPreProcessing(m_inst, &m_vc_pre_proc_cfg)) != VCENC_OK)
    {
        VCEncRelease(m_inst);
        LOGGER__ERROR("Failed to set pre processing configuration on VCEnc error code {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::Impl::init_encoder_config()
{
    memset(&m_vc_cfg, 0, sizeof(m_vc_cfg));
    LOGGER__DEBUG("Encoder - init_encoder_config");
    VCEncRet ret = VCENC_OK;
    auto input_stream = m_config->get_hailo_config().input_stream;
    auto output_stream = m_config->get_hailo_config().output_stream;

    m_input_stride = input_stream.width;

    m_vc_cfg.width = input_stream.width;
    m_vc_cfg.height = input_stream.height;
    m_vc_cfg.frameRateNum = input_stream.framerate;
    m_vc_cfg.frameRateDenom = 1;
    /* intra tools in sps and pps */
    m_vc_cfg.strongIntraSmoothing = 1;
    m_vc_cfg.streamType = VCENC_BYTE_STREAM;
    m_vc_cfg.codecH264 = get_codec();

    if (!m_config->get_hailo_config().rate_control.picture_rc &&
        (output_stream.profile.value() == "auto" || output_stream.level.value() == "auto"))
    {
        LOGGER__ERROR("Profile and level cannot be set to 'auto' when rate control is disabled");
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    m_vc_cfg.profile = get_profile(m_vc_cfg.codecH264);
    if(get_level(output_stream.level.value(), m_vc_cfg.codecH264, m_vc_cfg.width, m_vc_cfg.height, m_vc_cfg.frameRateNum, m_vc_cfg.frameRateDenom, m_vc_cfg.level) != MEDIA_LIBRARY_SUCCESS)
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;

    m_vc_cfg.bitDepthLuma = 8;
    m_vc_cfg.bitDepthChroma = 8;

    // default maxTLayer
    m_vc_cfg.maxTLayers = 1;
    m_vc_cfg.interlacedFrame = 0;
    /* Find the max number of reference frame */
    u32 maxRefPics = 0;
    i32 maxTemporalId = 0;
    for (int idx = 0; idx < m_enc_in.gopConfig.size; idx++)
    {
        VCEncGopPicConfig *cfg = &(m_enc_in.gopConfig.pGopPicCfg[idx]);
        if (cfg->codingType != VCENC_INTRA_FRAME)
        {
            if (maxRefPics < cfg->numRefPics)
                maxRefPics = cfg->numRefPics;

            if (maxTemporalId < cfg->temporalId)
                maxTemporalId = cfg->temporalId;
        }
    }
    m_vc_cfg.refFrameAmount = maxRefPics + m_vc_cfg.interlacedFrame +
                              (m_enc_in.gopConfig.ltrInterval > 0);
    m_vc_cfg.maxTLayers = maxTemporalId + 1;
    // TODO: Change compressor from json
    m_vc_cfg.compressor = 3;
    m_vc_cfg.enableOutputCuInfo = 0;
    m_vc_cfg.exp_of_alignment = 0;
    m_vc_cfg.refAlignmentExp = 0;
    m_vc_cfg.AXIAlignment = 0;
    m_vc_cfg.AXIreadOutstandingNum = 64;  // ENCH2_ASIC_AXI_READ_OUTSTANDING_NUM;
    m_vc_cfg.AXIwriteOutstandingNum = 64; // ENCH2_ASIC_AXI_WRITE_OUTSTANDING_NUM;
    ret = VCEncInit(&m_vc_cfg, &m_inst);
    if (ret != VCENC_OK)
    {
        LOGGER__ERROR("Failed to init encoder configuration on VCEnc error code {}", ret);
        return MEDIA_LIBRARY_CONFIGURATION_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}

media_library_return Encoder::Impl::init_monitors_config()
{
    LOGGER__DEBUG("Encoder - init_monitors_config");
    VCEncRet ret = VCENC_OK;

    auto monitors_control = m_config->get_hailo_config().monitors_control;
    m_bitrate_monitor.enabled = monitors_control.bitrate_monitor.enable;
    m_bitrate_monitor.period = monitors_control.bitrate_monitor.period;

    m_cycle_monitor.enabled = monitors_control.cycle_monitor.enable;
    m_cycle_monitor.start_delay = monitors_control.cycle_monitor.start_delay;
    m_cycle_monitor.deviation_threshold = monitors_control.cycle_monitor.deviation_threshold;

    if (monitors_control.bitrate_monitor.output_result_to_file)
    {
        m_bitrate_monitor.output_file = std::ofstream(monitors_control.bitrate_monitor.result_output_path,
                                                      std::ios::out | std::ios::trunc);
        if (m_bitrate_monitor.output_file.bad())
        {
            LOGGER__ERROR("Encoder - Failed to open bitrate output file");
            ret = VCENC_SYSTEM_ERROR;
        }
    }

    if (monitors_control.cycle_monitor.output_result_to_file)
    {
        m_cycle_monitor.output_file = std::ofstream(monitors_control.cycle_monitor.result_output_path,
                                                    std::ios::out | std::ios::trunc);
        if (m_cycle_monitor.output_file.bad())
        {
            LOGGER__ERROR("Encoder - Failed to open cycle output file");
            ret = VCENC_SYSTEM_ERROR;
        }
    }

    if (ret != VCENC_OK)
    {
        return MEDIA_LIBRARY_ERROR;
    }

    return MEDIA_LIBRARY_SUCCESS;
}