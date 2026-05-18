/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#include "common/tensors.hpp"

namespace common
{

xt::xarray<uint8_t> get_xtensor(HailoTensorPtr &tensor)
{
    xt::xarray<uint8_t> xtensor = xt::adapt(tensor->data(), tensor->size(), xt::no_ownership(), tensor->shape());
    return xtensor;
}

xt::xarray<uint16_t> get_xtensor_uint16(HailoTensorPtr &tensor)
{
    uint16_t *data = (uint16_t *)(tensor->data());
    xt::xarray<uint16_t> xtensor = xt::adapt(data, tensor->size(), xt::no_ownership(), tensor->shape());
    return xtensor;
}

xt::xarray<float> get_xtensor_float(HailoTensorPtr &tensor)
{
    xt::xarray<uint8_t> xtensor = get_xtensor(tensor);
    return dequantize(xtensor, tensor->qp_scale(), tensor->qp_zp());
}

std::vector<HailoTensorPtr> get_tensor_values(const std::map<std::string, HailoTensorPtr> &tensors)
{
    std::vector<HailoTensorPtr> _tensors;
    _tensors.reserve(tensors.size());
    for (auto &tensor_pair : tensors)
    {
        _tensors.emplace_back(tensor_pair.second);
    }
    return _tensors;
}

} // namespace common
