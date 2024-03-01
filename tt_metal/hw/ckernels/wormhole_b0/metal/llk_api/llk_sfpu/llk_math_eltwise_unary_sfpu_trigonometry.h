// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "llk_math_eltwise_unary_sfpu_init.h"
#include "llk_math_eltwise_unary_sfpu_0_param.h"
#include "ckernel_sfpu_trigonometry.h"

namespace ckernel {

// New LLK SFPU APIs

//sine
template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_sine_init() {
    llk_math_eltwise_unary_sfpu_init<SfpuType::sine, APPROXIMATE>(sfpu::sine_init<APPROXIMATE>);
}

template <bool APPROXIMATE, DstSync Dst = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_sine_op(uint dst_index) {
    llk_math_eltwise_unary_sfpu_0_param<APPROXIMATE, Dst>
                                (ckernel::sfpu::calculate_sfpu_trig<SfpuType::sine, APPROXIMATE>,
                                ckernel::sfpu::calculate_sfpu_trig<SfpuType::sine, APPROXIMATE>,
                                dst_index, (int)VectorMode::RC);
}


//cosine
template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_cosine_init() {
    llk_math_eltwise_unary_sfpu_init<SfpuType::cosine, APPROXIMATE>(sfpu::cosine_init<APPROXIMATE>);
}

template <bool APPROXIMATE, DstSync Dst = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_cosine_op(uint dst_index) {
    llk_math_eltwise_unary_sfpu_0_param<APPROXIMATE, Dst>
                                (ckernel::sfpu::calculate_sfpu_trig<SfpuType::cosine, APPROXIMATE>,
                                ckernel::sfpu::calculate_sfpu_trig<SfpuType::cosine, APPROXIMATE>,
                                dst_index, (int)VectorMode::RC);
}


//tangent
template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_tan_init() {
    llk_math_eltwise_unary_sfpu_init<SfpuType::tan, APPROXIMATE>(sfpu::tan_init<APPROXIMATE>);
}

template <bool APPROXIMATE, DstSync Dst = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_tan_op(uint dst_index) {
    llk_math_eltwise_unary_sfpu_0_param<APPROXIMATE, Dst>
                                (ckernel::sfpu::calculate_sfpu_trig<SfpuType::tan, APPROXIMATE>,
                                ckernel::sfpu::calculate_sfpu_trig<SfpuType::tan, APPROXIMATE>,
                                dst_index, (int)VectorMode::RC);

}

//asin
template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_asin_init() {
    llk_math_eltwise_unary_sfpu_init<SfpuType::asin, APPROXIMATE>(sfpu::asin_init<APPROXIMATE>);
}

template <bool APPROXIMATE, DstSync Dst = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_asin(uint dst_index, int vector_mode = (int)VectorMode::RC) {
    llk_math_eltwise_unary_sfpu_0_param<APPROXIMATE, Dst>
                                (ckernel::sfpu::calculate_asin<APPROXIMATE>,
                                ckernel::sfpu::calculate_asin<APPROXIMATE>,
                                dst_index, vector_mode);
}

//acos
template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_acos_init() {
    llk_math_eltwise_unary_sfpu_init<SfpuType::acos, APPROXIMATE>(sfpu::acos_init<APPROXIMATE>);
}

template <bool APPROXIMATE, DstSync Dst = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_acos(uint dst_index, int vector_mode = (int)VectorMode::RC) {
    llk_math_eltwise_unary_sfpu_0_param<APPROXIMATE, Dst>
                                (ckernel::sfpu::calculate_acos<APPROXIMATE>,
                                ckernel::sfpu::calculate_acos<APPROXIMATE>,
                                dst_index, vector_mode);
}

//atan
template <bool APPROXIMATE>
inline void llk_math_eltwise_unary_sfpu_atan_init() {
    llk_math_eltwise_unary_sfpu_init<SfpuType::atan, APPROXIMATE>(sfpu::atan_init<APPROXIMATE>);
}

template <bool APPROXIMATE, DstSync Dst = DstSync::SyncFull>
inline void llk_math_eltwise_unary_sfpu_atan(uint dst_index, int vector_mode = (int)VectorMode::RC) {
    llk_math_eltwise_unary_sfpu_0_param<APPROXIMATE, Dst>
                                (ckernel::sfpu::calculate_atan<APPROXIMATE>,
                                ckernel::sfpu::calculate_atan<APPROXIMATE>,
                                dst_index, vector_mode);
}

}
