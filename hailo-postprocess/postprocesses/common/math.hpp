/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once

#include "xtensor/xarray.hpp"
#include "xtensor/xeval.hpp"
#include "xtensor/xsort.hpp"
#include "xtensor/xview.hpp"
#include "xtensor/xio.hpp"

namespace common
{

//-------------------------------
// COMMON FILTERS
//-------------------------------
template <typename T> xt::xarray<int> top_k(xt::xarray<T> &data, const int k)
{
    // First we negate the array so that we sort in descending order.
    auto descending_order_array = xt::eval(-data);
    // Partition the array so the k "smallest" items are first (argpartition returns this as an index array).
    // Since we negated the values first, we are actually partitioning the largest values.
    xt::xarray<int> krange = xt::arange<int>(0, k);
    xt::xarray<int> index_array = xt::argpartition(descending_order_array, krange, xt::xnone());

    // We only want the k first indices, so make a view that "takes" these.
    auto topk_index_array = xt::view(xt::reshape_view(index_array, data.shape()), xt::all(), xt::range(0, k));
    return topk_index_array;
}

xt::xarray<float> vector_normalization(xt::xarray<float> &data);
xt::xarray<float> softmax_xtensor(xt::xarray<float> &scores);
void softmax_1D(float *data, const int size);
void softmax_2D(float *data, const int num_rows, const int num_cols);
void softmax_3D(float *data, const int dim1_size, const int dim2_size, const int dim3_size);
void sigmoid(float *data, const int size);

} // namespace common
