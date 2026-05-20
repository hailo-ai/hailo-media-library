/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include "ocr_post.hpp"

#include <stdint.h>
#include <string>
#include <cstddef>
#include <memory>

#include "hailo_postprocess_tools/labels/ppocrv5_char_dict.hpp"
#include "hailo_postprocess_tools/objects/hailo_objects.hpp"
#include "hailo_postprocess_tools/objects/hailo_tensors.hpp"

#ifdef __aarch64__
#include <arm_neon.h>
#endif

//******************************************************************
// LICENSE PLATE OCR POST-PROCESSING
//******************************************************************

// PPOCRv5 CTC decoder — converts quantized softmax tensor to text.
//
// Tensor shape: 1×40×18385 (height=1, width=40 timesteps, features=18385 classes).
// Classes 0,1 are blank/padding (CTC ignored tokens); classes 2..18383 map to
// PPOCRV5_CHAR_DICT[0..18381].
//
// Optimization: argmax is performed on quantized uint8 values directly (valid
// because qp_scale > 0 for softmax output). Only the winning value per timestep
// is dequantized for the confidence score. On AArch64, NEON SIMD processes 16
// bytes at a time for ~8-16x speedup on the argmax scan.

static const char *OUTPUT_TENSOR_NAME = "paddle_ocr_v5_mobile_recognition/ew_mult_softmax3";
static constexpr uint32_t NUM_IGNORED_TOKENS = 2;

// Find argmax (index and value) over a uint8 array of length `len`.
static inline void argmax_uint8(const uint8_t *data, uint32_t len, uint32_t &out_idx, uint8_t &out_val)
{
#ifdef __aarch64__
    // Pass 1: find the maximum value using NEON (16 bytes per iteration)
    uint8x16_t vmax = vdupq_n_u8(0);
    uint32_t c = 0;
    for (; c + 16 <= len; c += 16)
        vmax = vmaxq_u8(vmax, vld1q_u8(data + c));
    uint8_t best_val = vmaxvq_u8(vmax);
    for (; c < len; ++c)
        best_val = (data[c] > best_val) ? data[c] : best_val;

    // Pass 2: find the first index that holds the max value
    uint8x16_t target = vdupq_n_u8(best_val);
    for (c = 0; c + 16 <= len; c += 16)
    {
        uint8x16_t cmp = vceqq_u8(vld1q_u8(data + c), target);
        if (vmaxvq_u8(cmp))
        {
            // At least one match in this 16-byte chunk — find the lane
            uint64_t lo = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0);
            if (lo)
            {
                out_idx = c + static_cast<uint32_t>(__builtin_ctzll(lo) >> 3);
            }
            else
            {
                uint64_t hi = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);
                out_idx = c + 8 + static_cast<uint32_t>(__builtin_ctzll(hi) >> 3);
            }
            out_val = best_val;
            return;
        }
    }
    // Tail: match must be in the remaining elements
    for (; c < len; ++c)
    {
        if (data[c] == best_val)
        {
            out_idx = c;
            out_val = best_val;
            return;
        }
    }
#else
    // Scalar fallback for non-AArch64 builds (host tests, etc.)
    uint32_t best = 0;
    uint8_t best_val = data[0];
    for (uint32_t c = 1; c < len; ++c)
    {
        if (data[c] > best_val)
        {
            best_val = data[c];
            best = c;
        }
    }
    out_idx = best;
    out_val = best_val;
#endif
}

void ocr_postprocess(HailoROIPtr roi)
{
    if (!roi->has_tensors())
        return;

    auto tensor = roi->get_tensor(OUTPUT_TENSOR_NAME);
    const uint32_t num_timesteps = tensor->width();
    const uint32_t num_classes = tensor->features();
    const float qp_scale = tensor->qp_scale();
    const float qp_zp = tensor->qp_zp();
    const uint8_t *data = tensor->data();

    std::string decoded_text;
    float confidence_sum = 0.0f;
    uint32_t num_chars = 0;
    uint32_t prev_idx = UINT32_MAX;

    for (uint32_t t = 0; t < num_timesteps; ++t)
    {
        const uint8_t *ts = data + static_cast<std::size_t>(t) * num_classes;

        // Prefetch next timestep into L1 cache
        if (t + 1 < num_timesteps)
            __builtin_prefetch(ts + num_classes, 0, 1);

        // SIMD-accelerated argmax on quantized values
        uint32_t best = 0;
        uint8_t best_val = 0;
        argmax_uint8(ts, num_classes, best, best_val);

        // Skip blank/padding tokens
        if (best < NUM_IGNORED_TOKENS)
        {
            prev_idx = best;
            continue;
        }

        // CTC duplicate removal
        if (best == prev_idx)
            continue;

        prev_idx = best;

        // Map to dictionary index
        uint32_t dict_idx = best - NUM_IGNORED_TOKENS;
        if (dict_idx >= common::PPOCRV5_DICT_SIZE)
            continue;

        decoded_text += common::PPOCRV5_CHAR_DICT[dict_idx];
        confidence_sum += (static_cast<float>(best_val) - qp_zp) * qp_scale;
        ++num_chars;
    }

    if (decoded_text.empty())
        return;

    float mean_confidence = confidence_sum / static_cast<float>(num_chars);
    roi->add_object(std::make_shared<HailoClassification>("ocr", decoded_text, mean_confidence));
}

void filter(HailoROIPtr roi)
{
    ocr_postprocess(roi);
}
