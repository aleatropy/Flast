/* libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2004-2009  Josh Coalson
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

#ifndef FLAC__PRIVATE__FLOAT_H
#define FLAC__PRIVATE__FLOAT_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "FLAC/ordinals.h"

#ifndef FLAC__INTEGER_ONLY_LIBRARY

typedef float FLAC__real;
#else

typedef FLAC__int32 FLAC__fixedpoint;
extern const FLAC__fixedpoint FLAC__FP_ZERO;
extern const FLAC__fixedpoint FLAC__FP_ONE_HALF;
extern const FLAC__fixedpoint FLAC__FP_ONE;
extern const FLAC__fixedpoint FLAC__FP_LN2;
extern const FLAC__fixedpoint FLAC__FP_E;

#define FLAC__fixedpoint_trunc(x) ((x)>>16)

#define FLAC__fixedpoint_mul(x, y) ( (FLAC__fixedpoint) ( ((FLAC__int64)(x)*(FLAC__int64)(y)) >> 16 ) )

#define FLAC__fixedpoint_div(x, y) ( (FLAC__fixedpoint) ( ( ((FLAC__int64)(x)<<32) / (FLAC__int64)(y) ) >> 16 ) )

FLAC__uint32 FLAC__fixedpoint_log2(FLAC__uint32 x, uint32_t fracbits, uint32_t precision);

#endif

#endif
