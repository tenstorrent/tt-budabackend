// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ckernel_defs.h"
#include "ckernel.h"
#include "noc_nonblocking_api.h"
#include "ckernel_sfpu_exp.h"
#include "ckernel_sfpu_load_config.h"

#include "sfpi.h"

using namespace sfpi;

namespace ckernel
{
namespace sfpu
{

template <bool APPROXIMATION_MODE>
inline vFloat _calculate_gelu_core_(vFloat in)
{
    // SFPU microcode:
    // result = (APPROX_MODE == 1)
    //   ? (1 + erf(x/sqrt(2)))
    //   : (1 + tanh( sqrt(2/pi) * (x + 0.044715*x^3) )
    vFloat result;
    if constexpr (APPROXIMATION_MODE) {
        result = in;
    } else {
        // f = (0.044715*x^3 + x)
        result = (in * in) * (in * s2vFloat16b(0.044715f)) + in;
        result *= s2vFloat16b(0.79788f);
    }

    return result;
}

template <bool APPROXIMATION_MODE, int ITERATIONS>
inline void _calculate_gelu_(const int iterations)
{
    vUInt l0 = l_reg[LRegs::LReg0];
    vUInt l1 = l_reg[LRegs::LReg1];
    vUInt l2 = l_reg[LRegs::LReg2];
    vUInt l4 = l_reg[LRegs::LReg4];
    vUInt l5 = l_reg[LRegs::LReg5];
    vUInt l6 = l_reg[LRegs::LReg6];

    #pragma GCC unroll 8
    for (int d = 0; d < iterations; d++)
    {
        // vFloat in = dst_reg[0];
        // vFloat result = calculate_gelu_core<APPROXIMATION_MODE>(in);

        // vFloat half_in = in * half;
        // result = lut(result, l0, l1, l2);
        // result = half_in * result + half_in;

        //dst_reg[0] = result;

        vFloat in = dst_reg[0];
        vFloat half = vConstFloatPrgm0;
        vFloat half_in = in * half;
        vFloat result = lut2_sign(in, l0, l1, l2, l4, l5, l6);
        result = half_in + result;

        dst_reg[0] = result;

        dst_reg++;

        // dst_reg++;
        //TTI_SFPLOAD(3, 0, 1/*load addr mode*/,0);    // load from dest
        ////TTI_SFPMUL(3,11,9,7,0);           // lreg7 = 0.5*lreg3
        //TTI_SFPLUTFP32(7, 2);                // lreg7= LUT(3)
        //TTI_SFPMAD(3,12,7,3,0);            // lreg3 = 0.5*lreg3+lregm7
        //TTI_SFPSTORE(3, 0, 3/*store_addr_mod3*/, 0);   // and INCRWC by 4 using mode 3
    }

    l_reg[LRegs::LReg0] = l0;
    l_reg[LRegs::LReg1] = l1;
    l_reg[LRegs::LReg2] = l2;
    l_reg[LRegs::LReg4] = l4;
    l_reg[LRegs::LReg5] = l5;
    l_reg[LRegs::LReg6] = l6;
}

template <bool APPROXIMATION_MODE, int ITERATIONS>
inline void _calculate_gelu_derivative_(const int iterations)
{
    if constexpr (APPROXIMATION_MODE) {
        constexpr int lut_mode = 1; // SFPLUTFP32_MOD0_FP16_6ENTRY_TABLE1

        vUInt l0 = l_reg[LRegs::LReg0];
        vUInt l1 = l_reg[LRegs::LReg1];
        vUInt l2 = l_reg[LRegs::LReg2];
        vUInt l4 = l_reg[LRegs::LReg4];
        vUInt l5 = l_reg[LRegs::LReg5];
        vUInt l6 = l_reg[LRegs::LReg6];

        // SFPU microcode:
        #pragma GCC unroll 0
        for (int d = 0; d < iterations; d++)
        {
            vFloat val = dst_reg[0];
            val = lut2(val, l0, l1, l2, l4, l5, l6, lut_mode);
            v_if (val < 0.0F) {
                val = val + 1.0f;
            }
            v_endif;
            dst_reg[0] = val;
            dst_reg++;

        }

        l_reg[LRegs::LReg0] = l0;
        l_reg[LRegs::LReg1] = l1;
        l_reg[LRegs::LReg2] = l2;
        l_reg[LRegs::LReg4] = l4;
        l_reg[LRegs::LReg5] = l5;
        l_reg[LRegs::LReg6] = l6;
    } else {
        constexpr uint imm2 = 0xFF10;

        vUInt l0 = l_reg[LRegs::LReg0];
        vUInt l1 = l_reg[LRegs::LReg1];

        // SFPU microcode:
        #pragma GCC unroll 0
        for (int d = 0; d < iterations; d++)
        {
            vFloat in = dst_reg[0];
            vFloat neg_half_sq_in = in * in * -0.5f;

            // exp = e^(val)
            vFloat exp = _calculate_exponential_body_<false>(neg_half_sq_in);

            // exp = exp * 1/sqrt(2*pi)
            vFloat partial = exp * in * s2vFloat16b(0.3989423F);

            vFloat result = _calculate_gelu_core_<true>(in);

            result = lut(result, l0, l1, imm2);

            dst_reg[0] = partial + result + 0.5f;
            dst_reg++;
        }

        l_reg[LRegs::LReg0] = l0;
        l_reg[LRegs::LReg1] = l1;
    }
}

template <bool APPROXIMATION_MODE>
inline void _init_gelu_()
{
    vConstFloatPrgm0 = 0.5f;

    // // >= 3.0f
    // lreg2_hi=0.50;//3800
    // lreg6_hi=0.0f;//7c00
    // // 2.0f -> 3.0f
    // lreg2_lo= 0.5402f;//3852
    // lreg6_lo= -0.1194f;//AFA4
    // // 1.5f -> 2.0f
    // lreg1_hi= .6099f; //38E1
    // lreg5_hi= -.2635f; //B437
    // // 1.0f -> 1.5f
    // lreg1_lo=0.6189;//38F3
    // lreg5_lo=-.2797;//B479
    // // 0.5f -> 1.0f
    // lreg0_hi=.4939f;//37E7
    // lreg4_hi=-.1605f;//B122
    // // 0.0f -> 0.5f
    // lreg0_lo=0.1928f;//322B
    // lreg4_lo=-0.0150f;//A3AE
    _sfpu_load_imm32_(0,0x37E7322B);
    _sfpu_load_imm32_(4,0xB12286D8);

    _sfpu_load_imm32_(1,0x38E138F3);
    _sfpu_load_imm32_(5,0xB437B479);

    _sfpu_load_imm32_(2,0x38003852);
    _sfpu_load_imm32_(6,0x7c00afa4);
}

template <bool APPROXIMATION_MODE>
inline void _init_gelu_derivative_()
{
    vConstFloatPrgm0 = 1.442695f; // ln2_recip
    vConstFloatPrgm1 = 2.0f;
    vConstFloatPrgm2 = 0.863281f;

    uint imm0;
    uint imm1;
    uint imm2;
    uint imm3;
    uint imm4;
    uint imm5;

    if constexpr (APPROXIMATION_MODE) {
        // Using a 6 piece LUT to calculate and model gelu_derivative directly
        // x <= 0.5 --> 0.8x + 0.5
        // x <= 1.0 --> 0.4x + 0.7
        // x <= 1.5 --> 0.1x + 0.99
        // x <= 2.0 --> -0.09x + 1.27
        // x <= 3.0 --> -0.075x + 1.235
        // x >  3.0 --> 1.0
        // imm0[15:0] = A0=0.8    = 0x3A66 -- imm0[31:16] = A1=0.4   = 0x3666
        imm0 = 0x36663A66;
        // imm1[15:0] = A2=0.1    = 0x2E66 -- imm1[31:16] = A3=-0.09 = 0xADC3
        imm1 = 0xADC32E66;
        // imm2[15:0] = A4=-0.075 = 0xACCD -- imm2[31:16] = A5=0     = 0x7C00
        imm2 = 0x7C00ACCD;
        // imm3[15:0] = B0=0.5    = 0x3800 -- imm3[31:16] = B1=0.7   = 0x399A
        imm3 = 0x399A3800;
        // imm4[15:0] = B2=0.99   = 0x3BEC -- imm4[31:16] = B3=1.27  = 0x3D14
        imm4 = 0x3D143BEC;
        // imm5[15:0] = B4=1.235  = 0x3CF1 -- imm5[31:16] = B5=1.0   = 0x3C00
        imm5 = 0x3C003CF1;
        _sfpu_load_imm32_(0, imm0);
        _sfpu_load_imm32_(1, imm1);
        _sfpu_load_imm32_(2, imm2);
        _sfpu_load_imm32_(4, imm3);
        _sfpu_load_imm32_(5, imm4);
        _sfpu_load_imm32_(6, imm5);
    } else {
        imm0 = 0x28FF;
        imm1 = 0x3020;
        _sfpu_load_imm16_(0, imm0);
        _sfpu_load_imm16_(1, imm1);
    }
}

} // namespace sfpu
} // namespace ckernel
