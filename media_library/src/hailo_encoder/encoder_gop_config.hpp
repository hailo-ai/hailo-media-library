#pragma once
#include <iostream>
#include <vector>
#include "encoder_class.hpp"


struct GopPicConfig
{
    int m_frame_num;
    VCEncPictureCodingType m_type;
    uint8_t m_poc;
    int m_qp_offset;
    float m_qp_factor;
    int m_num_ref_pics;
    std::vector<int> m_ref_pics;
    std::vector<int> m_used_by_cur;
    GopPicConfig(int frame_num, VCEncPictureCodingType type, uint8_t poc, int qp_offset, float qp_factor, int num_ref_pics, std::vector<int> ref_pics, std::vector<int> used_by_cur)
    : m_frame_num(frame_num), m_type(type), m_poc(poc), m_qp_offset(qp_offset), m_qp_factor(qp_factor), m_num_ref_pics(num_ref_pics), m_ref_pics(ref_pics), m_used_by_cur(used_by_cur)
    {
    };
};

const std::vector<GopPicConfig> RpsDefault_GOPSize_1 = {
    GopPicConfig(1, VCENC_PREDICTED_FRAME, 1, 0, 0.8, 1, {-1,}, {1,})
};

const std::vector<GopPicConfig> RpsDefault_H264_GOPSize_1 = {
    GopPicConfig(1, VCENC_PREDICTED_FRAME, 1, 0, 0.4, 1, {-1,}, {1,})
};

const std::vector<GopPicConfig> RpsDefault_GOPSize_2 = {
    GopPicConfig(1, VCENC_PREDICTED_FRAME, 2, 0, 0.6, 1, {-2}, {1}),
    GopPicConfig(2, VCENC_BIDIR_PREDICTED_FRAME, 1, 0, 0.68, 2, {-1, 1}, {1, 1}),
};

const std::vector<GopPicConfig> RpsDefault_GOPSize_3 = {
    GopPicConfig(1, VCENC_PREDICTED_FRAME, 3, 0, 0.5, 1, {-3}, {1}),
    GopPicConfig(2, VCENC_BIDIR_PREDICTED_FRAME, 1, 0, 0.5, 2, {-1, 2}, {1, 1}),
    GopPicConfig(3, VCENC_BIDIR_PREDICTED_FRAME, 2, 0, 0.68, 2, {-1, 1}, {1, 1}),
};

const std::vector<GopPicConfig> RpsDefault_GOPSize_4 = {
    GopPicConfig(1, VCENC_PREDICTED_FRAME, 4, 0, 0.5, 1, {-4}, {1}),
    GopPicConfig(2, VCENC_BIDIR_PREDICTED_FRAME, 2, 0, 0.3536, 2, {-2, 2}, {1, 1}),
    GopPicConfig(3, VCENC_BIDIR_PREDICTED_FRAME, 1, 0, 0.5, 3, {-1, 1, 3}, {1, 1, 0}),
    GopPicConfig(4, VCENC_BIDIR_PREDICTED_FRAME, 3, 0, 0.5, 2, {-1, 1}, {1, 1}),
};

const std::vector<GopPicConfig> RpsDefault_GOPSize_5 = {
    GopPicConfig(1, VCENC_PREDICTED_FRAME, 5, 0, 0.442, 1, {-5}, {1}),
    GopPicConfig(2, VCENC_BIDIR_PREDICTED_FRAME, 2, 0, 0.3536, 2, {-2, 3}, {1, 1}),
    GopPicConfig(3, VCENC_BIDIR_PREDICTED_FRAME, 1, 0, 0.68, 3, {-1, 1, 4}, {1, 1, 0}),
    GopPicConfig(4, VCENC_BIDIR_PREDICTED_FRAME, 3, 0, 0.3536, 2, {-1, 2}, {1, 1}),
    GopPicConfig(5, VCENC_BIDIR_PREDICTED_FRAME, 4, 0, 0.68, 2, {-1, 1}, {1, 1}),
};

const std::vector<GopPicConfig> RpsDefault_GOPSize_6 = {
    GopPicConfig(1, VCENC_PREDICTED_FRAME, 6, 0, 0.442, 1, {-6}, {1}),
    GopPicConfig(2, VCENC_BIDIR_PREDICTED_FRAME, 3, 0, 0.3536, 2, {-3, 3}, {1, 1}),
    GopPicConfig(3, VCENC_BIDIR_PREDICTED_FRAME, 1, 0, 0.3536, 3, {-1, 2, 5}, {1, 1, 0}),
    GopPicConfig(4, VCENC_BIDIR_PREDICTED_FRAME, 2, 0, 0.68, 3, {-1, 1, 4}, {1, 1, 0}),
    GopPicConfig(5, VCENC_BIDIR_PREDICTED_FRAME, 4, 0, 0.3536, 2, {-1, 2}, {1, 1}),
    GopPicConfig(6, VCENC_BIDIR_PREDICTED_FRAME, 5, 0, 0.68, 2, {-1, 1}, {1, 1}),
};

const std::vector<GopPicConfig> RpsDefault_GOPSize_7 = {
    GopPicConfig(1, VCENC_PREDICTED_FRAME, 7, 0, 0.442, 1, {-7}, {1}),
    GopPicConfig(2, VCENC_BIDIR_PREDICTED_FRAME, 3, 0, 0.3536, 2, {-3, 4}, {1, 1}),
    GopPicConfig(3, VCENC_BIDIR_PREDICTED_FRAME, 1, 0, 0.3536, 3, {-1, 2, 6}, {1, 1, 0}),
    GopPicConfig(4, VCENC_BIDIR_PREDICTED_FRAME, 2, 0, 0.68, 3, {-1, 1, 5}, {1, 1, 0}),
    GopPicConfig(5, VCENC_BIDIR_PREDICTED_FRAME, 5, 0, 0.3536, 2, {-2, 2}, {1, 1}),
    GopPicConfig(6, VCENC_BIDIR_PREDICTED_FRAME, 4, 0, 0.68, 3, {-1, 1, 3}, {1, 1, 0}),
    GopPicConfig(7, VCENC_BIDIR_PREDICTED_FRAME, 6, 0, 0.68, 2, {-1, 1}, {1, 1}),
};

const std::vector<GopPicConfig> RpsDefault_GOPSize_8 = {
    GopPicConfig(1, VCENC_PREDICTED_FRAME, 8, 0, 0.442, 1, {-8}, {1}),
    GopPicConfig(2, VCENC_BIDIR_PREDICTED_FRAME, 4, 0, 0.3536, 2, {-4, 4}, {1, 1}),
    GopPicConfig(3, VCENC_BIDIR_PREDICTED_FRAME, 2, 0, 0.3536, 3, {-2, 2, 6}, {1, 1, 0}),
    GopPicConfig(4, VCENC_BIDIR_PREDICTED_FRAME, 1, 0, 0.68, 4, {-1, 1, 3, 7}, {1, 1, 0, 0}),
    GopPicConfig(5, VCENC_BIDIR_PREDICTED_FRAME, 3, 0, 0.68, 3, {-1, 1, 5}, {1, 1, 0}),
    GopPicConfig(6, VCENC_BIDIR_PREDICTED_FRAME, 6, 0, 0.3536, 2, {-2, 2}, {1, 1}),
    GopPicConfig(7, VCENC_BIDIR_PREDICTED_FRAME, 5, 0, 0.68, 3, {-1, 1, 3}, {1, 1, 0}),
    GopPicConfig(8, VCENC_BIDIR_PREDICTED_FRAME, 7, 0, 0.68, 2, {-1, 1}, {1, 1}),
};

const std::vector<GopPicConfig> RpsDefault_Interlace_GOPSize_1 = {
    GopPicConfig(1, VCENC_PREDICTED_FRAME, 1, 0, 0.8, 2, {-1, -2}, {0, 1}),
};

const std::vector<GopPicConfig> RpsLowdelayDefault_GOPSize_1 = {
    GopPicConfig(1, VCENC_BIDIR_PREDICTED_FRAME, 1, 0, 0.65, 2, {-1, -2}, {1, 1}),
};

const std::vector<GopPicConfig> RpsLowdelayDefault_GOPSize_2 = {
    GopPicConfig(1, VCENC_BIDIR_PREDICTED_FRAME, 1, 0, 0.4624, 2, {-1, -3}, {1, 1}),
    GopPicConfig(2, VCENC_BIDIR_PREDICTED_FRAME, 2, 0, 0.578, 2, {-1, -2}, {1, 1}),
};

const std::vector<GopPicConfig> RpsLowdelayDefault_GOPSize_3 = {
    GopPicConfig(1, VCENC_BIDIR_PREDICTED_FRAME, 1, 0, 0.4624, 2, {-1, -4}, {1, 1}),
    GopPicConfig(2, VCENC_BIDIR_PREDICTED_FRAME, 2, 0, 0.4624, 2, {-1, -2}, {1, 1}),
    GopPicConfig(3, VCENC_BIDIR_PREDICTED_FRAME, 3, 0, 0.578, 2, {-1, -3}, {1, 1}),
};

const std::vector<GopPicConfig> RpsLowdelayDefault_GOPSize_4 = {
    GopPicConfig(1, VCENC_BIDIR_PREDICTED_FRAME, 1, 0, 0.4624, 2, {-1, -5}, {1, 1}),
    GopPicConfig(2, VCENC_BIDIR_PREDICTED_FRAME, 2, 0, 0.4624, 2, {-1, -2}, {1, 1}),
    GopPicConfig(3, VCENC_BIDIR_PREDICTED_FRAME, 3, 0, 0.4624, 2, {-1, -3}, {1, 1}),
    GopPicConfig(4, VCENC_BIDIR_PREDICTED_FRAME, 4, 0, 0.578, 2, {-1, -4}, {1, 1}),
};