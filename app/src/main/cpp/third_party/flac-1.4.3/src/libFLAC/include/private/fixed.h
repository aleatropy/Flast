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

#ifndef FLAC__PRIVATE__FIXED_H
#define FLAC__PRIVATE__FIXED_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "private/cpu.h"
#include "private/float.h"
#include "FLAC/format.h"

#ifndef FLAC__INTEGER_ONLY_LIBRARY
uint32_t FLAC__fixed_compute_best_predictor(const FLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
uint32_t FLAC__fixed_compute_best_predictor_wide(const FLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
uint32_t FLAC__fixed_compute_best_predictor_limit_residual(const FLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
uint32_t FLAC__fixed_compute_best_predictor_limit_residual_33bit(const FLAC__int64 data[], uint32_t data_len, float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
# ifndef FLAC__NO_ASM
#  if (defined FLAC__CPU_IA32 || defined FLAC__CPU_X86_64) && FLAC__HAS_X86INTRIN
#   ifdef FLAC__SSE2_SUPPORTED
uint32_t FLAC__fixed_compute_best_predictor_intrin_sse2(const FLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER + 1]);
#   endif
#   ifdef FLAC__SSSE3_SUPPORTED
uint32_t FLAC__fixed_compute_best_predictor_intrin_ssse3(const FLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
#   endif
#   ifdef FLAC__SSE4_2_SUPPORTED
uint32_t FLAC__fixed_compute_best_predictor_limit_residual_intrin_sse42(const FLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
#   endif
#   ifdef FLAC__AVX2_SUPPORTED
uint32_t FLAC__fixed_compute_best_predictor_wide_intrin_avx2(const FLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
uint32_t FLAC__fixed_compute_best_predictor_limit_residual_intrin_avx2(const FLAC__int32 data[], uint32_t data_len, float residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
#   endif
#  endif
# endif
#else
uint32_t FLAC__fixed_compute_best_predictor(const FLAC__int32 data[], uint32_t data_len, FLAC__fixedpoint residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
uint32_t FLAC__fixed_compute_best_predictor_wide(const FLAC__int32 data[], uint32_t data_len, FLAC__fixedpoint residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
uint32_t FLAC__fixed_compute_best_predictor_limit_residual(const FLAC__int32 data[], uint32_t data_len, FLAC__fixedpoint residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
uint32_t FLAC__fixed_compute_best_predictor_limit_residual_33bit(const FLAC__int64 data[], uint32_t data_len, FLAC__fixedpoint residual_bits_per_sample[FLAC__MAX_FIXED_ORDER+1]);
#endif

void FLAC__fixed_compute_residual(const FLAC__int32 data[], uint32_t data_len, uint32_t order, FLAC__int32 residual[]);
void FLAC__fixed_compute_residual_wide(const FLAC__int32 data[], uint32_t data_len, uint32_t order, FLAC__int32 residual[]);
void FLAC__fixed_compute_residual_wide_33bit(const FLAC__int64 data[], uint32_t data_len, uint32_t order, FLAC__int32 residual[]);

void FLAC__fixed_restore_signal(const FLAC__int32 residual[], uint32_t data_len, uint32_t order, FLAC__int32 data[]);
void FLAC__fixed_restore_signal_wide(const FLAC__int32 residual[], uint32_t data_len, uint32_t order, FLAC__int32 data[]);
void FLAC__fixed_restore_signal_wide_33bit(const FLAC__int32 residual[], uint32_t data_len, uint32_t order, FLAC__int64 data[]);

#endif
