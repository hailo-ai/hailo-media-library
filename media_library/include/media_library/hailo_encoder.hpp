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
/**
 * @file hailo_encoder.hpp
 * @brief MediaLibrary Encoder CPP API module
 **/

#pragma once
#include "encoder_common.hpp"
#include "gop_config.hpp"

/** @defgroup hailo_encoder_definitions MediaLibrary Hailo Encoder CPP API
 * definitions
 *  @{
 */

void SetDefaultParameters(EncoderParams *enc_params, bool codecH264);
int InitEncoderRateConfig(EncoderParams *enc_params, VCEncInst *pEnc);
int UpdateEncoderROIArea(EncoderParams *enc_params, VCEncInst *pEnc);
int OpenEncoder(VCEncInst *encoder, EncoderParams *enc_params);
int UpdateEncoderConfig(VCEncInst *encoder, EncoderParams *enc_params);
void CloseEncoder(VCEncInst encoder);
int AllocRes(EncoderParams *enc_params);
void FreeRes(EncoderParams *enc_params);
u32 SetupInputBuffer(EncoderParams *enc_params, VCEncIn *pEncIn);
void UpdateEncoderGOP(EncoderParams *enc_params, VCEncInst encoder);
VCEncRet EncodeFrame(EncoderParams *enc_params, VCEncInst encoder,
                     VCEncSliceReadyCallBackFunc sliceReadyCbFunc,
                     void *pAppData);
void ForceKeyframe(EncoderParams *enc_params, VCEncInst encoder);

/** @} */ // end of hailo_encoder_definitions