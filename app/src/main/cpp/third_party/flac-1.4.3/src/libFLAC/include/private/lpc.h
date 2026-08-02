/* libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2000-2009  Josh Coalson
 * Copyright (C) 2011-2023  Xiph.Org Foundation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the Xiph.org Foundation nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef FLAC__PRIVATE__LPC_H
#define FLAC__PRIVATE__LPC_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "private/cpu.h"
#include "private/float.h"
#include "FLAC/format.h"

#ifndef FLAC__INTEGER_ONLY_LIBRARY

void FLAC__lpc_window_data(const FLAC__int32 in[], const FLAC__real window[], FLAC__real out[], uint32_t data_len);
void FLAC__lpc_window_data_wide(const FLAC__int64 in[], const FLAC__real window[], FLAC__real out[], uint32_t data_len);
void FLAC__lpc_window_data_partial(const FLAC__int32 in[], const FLAC__real window[], FLAC__real out[], uint32_t data_len, uint32_t part_size, uint32_t data_shift);
void FLAC__lpc_window_data_partial_wide(const FLAC__int64 in[], const FLAC__real window[], FLAC__real out[], uint32_t data_len, uint32_t part_size, uint32_t data_shift);

void FLAC__lpc_compute_autocorrelation(const FLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
#ifndef FLAC__NO_ASM
#  if (defined FLAC__CPU_IA32 || defined FLAC__CPU_X86_64) && FLAC__HAS_X86INTRIN
#    ifdef FLAC__SSE2_SUPPORTED
void FLAC__lpc_compute_autocorrelation_intrin_sse2_lag_8(const FLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void FLAC__lpc_compute_autocorrelation_intrin_sse2_lag_10(const FLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void FLAC__lpc_compute_autocorrelation_intrin_sse2_lag_14(const FLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
#    endif
#  endif
#  if defined FLAC__CPU_X86_64 && FLAC__HAS_X86INTRIN
#    ifdef FLAC__FMA_SUPPORTED
void FLAC__lpc_compute_autocorrelation_intrin_fma_lag_8(const FLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void FLAC__lpc_compute_autocorrelation_intrin_fma_lag_12(const FLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void FLAC__lpc_compute_autocorrelation_intrin_fma_lag_16(const FLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
#    endif
#  endif
#if defined FLAC__CPU_ARM64 && FLAC__HAS_NEONINTRIN && FLAC__HAS_A64NEONINTRIN
void FLAC__lpc_compute_autocorrelation_intrin_neon_lag_8(const FLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void FLAC__lpc_compute_autocorrelation_intrin_neon_lag_10(const FLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
void FLAC__lpc_compute_autocorrelation_intrin_neon_lag_14(const FLAC__real data[], uint32_t data_len, uint32_t lag, double autoc[]);
#endif
#endif

void FLAC__lpc_compute_lp_coefficients(const double autoc[], uint32_t *max_order, FLAC__real lp_coeff[][FLAC__MAX_LPC_ORDER], double error[]);

int FLAC__lpc_quantize_coefficients(const FLAC__real lp_coeff[], uint32_t order, uint32_t precision, FLAC__int32 qlp_coeff[], int *shift);

void FLAC__lpc_compute_residual_from_qlp_coefficients(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
void FLAC__lpc_compute_residual_from_qlp_coefficients_wide(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
FLAC__bool FLAC__lpc_compute_residual_from_qlp_coefficients_limit_residual(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
FLAC__bool FLAC__lpc_compute_residual_from_qlp_coefficients_limit_residual_33bit(const FLAC__int64 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
#ifndef FLAC__NO_ASM
#   ifdef FLAC__CPU_ARM64
void FLAC__lpc_compute_residual_from_qlp_coefficients_intrin_neon(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
void FLAC__lpc_compute_residual_from_qlp_coefficients_wide_intrin_neon(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
#   endif

#  if (defined FLAC__CPU_IA32 || defined FLAC__CPU_X86_64) && FLAC__HAS_X86INTRIN
#    ifdef FLAC__SSE2_SUPPORTED
void FLAC__lpc_compute_residual_from_qlp_coefficients_16_intrin_sse2(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
void FLAC__lpc_compute_residual_from_qlp_coefficients_intrin_sse2(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
#    endif
#    ifdef FLAC__SSE4_1_SUPPORTED
void FLAC__lpc_compute_residual_from_qlp_coefficients_intrin_sse41(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
void FLAC__lpc_compute_residual_from_qlp_coefficients_wide_intrin_sse41(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
#    endif
#    ifdef FLAC__AVX2_SUPPORTED
void FLAC__lpc_compute_residual_from_qlp_coefficients_16_intrin_avx2(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
void FLAC__lpc_compute_residual_from_qlp_coefficients_intrin_avx2(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
void FLAC__lpc_compute_residual_from_qlp_coefficients_wide_intrin_avx2(const FLAC__int32 *data, uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 residual[]);
#    endif
#  endif
#endif

#endif

uint32_t FLAC__lpc_max_prediction_before_shift_bps(uint32_t subframe_bps, const FLAC__int32 qlp_coeff[], uint32_t order);
uint32_t FLAC__lpc_max_residual_bps(uint32_t subframe_bps, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization);

void FLAC__lpc_restore_signal(const FLAC__int32 residual[], uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 data[]);
void FLAC__lpc_restore_signal_wide(const FLAC__int32 residual[], uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int32 data[]);
void FLAC__lpc_restore_signal_wide_33bit(const FLAC__int32 residual[], uint32_t data_len, const FLAC__int32 qlp_coeff[], uint32_t order, int lp_quantization, FLAC__int64 data[]);

#ifndef FLAC__INTEGER_ONLY_LIBRARY

double FLAC__lpc_compute_expected_bits_per_residual_sample(double lpc_error, uint32_t total_samples);
double FLAC__lpc_compute_expected_bits_per_residual_sample_with_error_scale(double lpc_error, double error_scale);

uint32_t FLAC__lpc_compute_best_order(const double lpc_error[], uint32_t max_order, uint32_t total_samples, uint32_t overhead_bits_per_order);

#endif

#endif
