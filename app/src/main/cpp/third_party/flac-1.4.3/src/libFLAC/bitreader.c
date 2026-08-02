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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include "private/bitmath.h"
#include "private/bitreader.h"
#include "private/crc.h"
#include "private/cpu.h"
#include "private/macros.h"
#include "FLAC/assert.h"
#include "share/compat.h"
#include "share/endswap.h"

#if (ENABLE_64_BIT_WORDS == 0)

typedef FLAC__uint32 brword;
#define FLAC__BYTES_PER_WORD 4
#define FLAC__BITS_PER_WORD 32
#define FLAC__WORD_ALL_ONES ((FLAC__uint32)0xffffffff)

#if WORDS_BIGENDIAN
#define SWAP_BE_WORD_TO_HOST(x) (x)
#else
#define SWAP_BE_WORD_TO_HOST(x) ENDSWAP_32(x)
#endif

#define COUNT_ZERO_MSBS(word) FLAC__clz_uint32(word)
#define COUNT_ZERO_MSBS2(word) FLAC__clz2_uint32(word)

#else

typedef FLAC__uint64 brword;
#define FLAC__BYTES_PER_WORD 8
#define FLAC__BITS_PER_WORD 64
#define FLAC__WORD_ALL_ONES ((FLAC__uint64)FLAC__U64L(0xffffffffffffffff))

#if WORDS_BIGENDIAN
#define SWAP_BE_WORD_TO_HOST(x) (x)
#else
#define SWAP_BE_WORD_TO_HOST(x) ENDSWAP_64(x)
#endif

#define COUNT_ZERO_MSBS(word) FLAC__clz_uint64(word)
#define COUNT_ZERO_MSBS2(word) FLAC__clz2_uint64(word)

#endif

static const uint32_t FLAC__BITREADER_DEFAULT_CAPACITY = 65536u / FLAC__BITS_PER_WORD;

struct FLAC__BitReader {

	brword *buffer;
	uint32_t capacity;
	uint32_t words;
	uint32_t bytes;
	uint32_t consumed_words;
	uint32_t consumed_bits;
	uint32_t read_crc16;
	uint32_t crc16_offset;
	uint32_t crc16_align;
	FLAC__bool read_limit_set;
	uint32_t read_limit;
	uint32_t last_seen_framesync;
	FLAC__BitReaderReadCallback read_callback;
	void *client_data;
};

static inline void crc16_update_word_(FLAC__BitReader *br, brword word)
{
	register uint32_t crc = br->read_crc16;

	for ( ; br->crc16_align < FLAC__BITS_PER_WORD ; br->crc16_align += 8) {
		uint32_t shift = FLAC__BITS_PER_WORD - 8 - br->crc16_align ;
		crc = FLAC__CRC16_UPDATE ((uint32_t) (shift < FLAC__BITS_PER_WORD ? (word >> shift) & 0xff : 0), crc);
	}

	br->read_crc16 = crc;
	br->crc16_align = 0;
}

static inline void crc16_update_block_(FLAC__BitReader *br)
{
	if(br->consumed_words > br->crc16_offset && br->crc16_align)
		crc16_update_word_(br, br->buffer[br->crc16_offset++]);

	if (br->consumed_words > br->crc16_offset) {
#if FLAC__BYTES_PER_WORD == 4
		br->read_crc16 = FLAC__crc16_update_words32(br->buffer + br->crc16_offset, br->consumed_words - br->crc16_offset, br->read_crc16);
#elif FLAC__BYTES_PER_WORD == 8
		br->read_crc16 = FLAC__crc16_update_words64(br->buffer + br->crc16_offset, br->consumed_words - br->crc16_offset, br->read_crc16);
#else
		unsigned i;

		for (i = br->crc16_offset; i < br->consumed_words; i++)
			crc16_update_word_(br, br->buffer[i]);
#endif
	}

	br->crc16_offset = 0;
}

static FLAC__bool bitreader_read_from_client_(FLAC__BitReader *br)
{
	uint32_t start, end;
	size_t bytes;
	FLAC__byte *target;
#if WORDS_BIGENDIAN
#else
	brword preswap_backup;
#endif

	if(br->consumed_words > 0) {

		br->last_seen_framesync = -1;

		crc16_update_block_(br);

		start = br->consumed_words;
		end = br->words + (br->bytes? 1:0);
		memmove(br->buffer, br->buffer+start, FLAC__BYTES_PER_WORD * (end - start));

		br->words -= start;
		br->consumed_words = 0;
	}

	bytes = (br->capacity - br->words) * FLAC__BYTES_PER_WORD - br->bytes;
	if(bytes == 0)
		return false;
	target = ((FLAC__byte*)(br->buffer+br->words)) + br->bytes;

#if WORDS_BIGENDIAN
#else
	preswap_backup = br->buffer[br->words];
	if(br->bytes)
		br->buffer[br->words] = SWAP_BE_WORD_TO_HOST(br->buffer[br->words]);
#endif

	if(!br->read_callback(target, &bytes, br->client_data)){

#if WORDS_BIGENDIAN
#else
		br->buffer[br->words] = preswap_backup;
#endif
		return false;
	}

#if WORDS_BIGENDIAN
#else
	end = (br->words*FLAC__BYTES_PER_WORD + br->bytes + (uint32_t)bytes + (FLAC__BYTES_PER_WORD-1)) / FLAC__BYTES_PER_WORD;
	for(start = br->words; start < end; start++)
		br->buffer[start] = SWAP_BE_WORD_TO_HOST(br->buffer[start]);
#endif

	end = br->words*FLAC__BYTES_PER_WORD + br->bytes + (uint32_t)bytes;
	br->words = end / FLAC__BYTES_PER_WORD;
	br->bytes = end % FLAC__BYTES_PER_WORD;

	return true;
}

FLAC__BitReader *FLAC__bitreader_new(void)
{
	FLAC__BitReader *br = calloc(1, sizeof(FLAC__BitReader));

	return br;
}

void FLAC__bitreader_delete(FLAC__BitReader *br)
{
	FLAC__ASSERT(0 != br);

	FLAC__bitreader_free(br);
	free(br);
}

FLAC__bool FLAC__bitreader_init(FLAC__BitReader *br, FLAC__BitReaderReadCallback rcb, void *cd)
{
	FLAC__ASSERT(0 != br);

	br->words = br->bytes = 0;
	br->consumed_words = br->consumed_bits = 0;
	br->capacity = FLAC__BITREADER_DEFAULT_CAPACITY;
	br->buffer = malloc(sizeof(brword) * br->capacity);
	if(br->buffer == 0)
		return false;
	br->read_callback = rcb;
	br->client_data = cd;
	br->read_limit_set = false;
	br->read_limit = -1;
	br->last_seen_framesync = -1;

	return true;
}

void FLAC__bitreader_free(FLAC__BitReader *br)
{
	FLAC__ASSERT(0 != br);

	if(0 != br->buffer)
		free(br->buffer);
	br->buffer = 0;
	br->capacity = 0;
	br->words = br->bytes = 0;
	br->consumed_words = br->consumed_bits = 0;
	br->read_callback = 0;
	br->client_data = 0;
	br->read_limit_set = false;
	br->read_limit = -1;
	br->last_seen_framesync = -1;
}

FLAC__bool FLAC__bitreader_clear(FLAC__BitReader *br)
{
	br->words = br->bytes = 0;
	br->consumed_words = br->consumed_bits = 0;
	br->read_limit_set = false;
	br->read_limit = -1;
	br->last_seen_framesync = -1;
	return true;
}

void FLAC__bitreader_set_framesync_location(FLAC__BitReader *br)
{
	br->last_seen_framesync = br->consumed_words * FLAC__BYTES_PER_WORD + br->consumed_bits / 8;
}

FLAC__bool FLAC__bitreader_rewind_to_after_last_seen_framesync(FLAC__BitReader *br)
{
	if(br->last_seen_framesync == (uint32_t)-1) {
		br->consumed_words = br->consumed_bits = 0;
		return false;
	}
	else {
		br->consumed_words = (br->last_seen_framesync + 1) / FLAC__BYTES_PER_WORD;
		br->consumed_bits  = ((br->last_seen_framesync + 1) % FLAC__BYTES_PER_WORD) * 8;
		return true;
	}
}

void FLAC__bitreader_reset_read_crc16(FLAC__BitReader *br, FLAC__uint16 seed)
{
	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);
	FLAC__ASSERT((br->consumed_bits & 7) == 0);

	br->read_crc16 = (uint32_t)seed;
	br->crc16_offset = br->consumed_words;
	br->crc16_align = br->consumed_bits;
}

FLAC__uint16 FLAC__bitreader_get_read_crc16(FLAC__BitReader *br)
{
	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	crc16_update_block_(br);

	FLAC__ASSERT((br->consumed_bits & 7) == 0);
	FLAC__ASSERT(br->crc16_align <= br->consumed_bits);

	if(br->consumed_bits) {
		const brword tail = br->buffer[br->consumed_words];
		for( ; br->crc16_align < br->consumed_bits; br->crc16_align += 8)
			br->read_crc16 = FLAC__CRC16_UPDATE((uint32_t)((tail >> (FLAC__BITS_PER_WORD-8-br->crc16_align)) & 0xff), br->read_crc16);
	}
	return br->read_crc16;
}

inline FLAC__bool FLAC__bitreader_is_consumed_byte_aligned(const FLAC__BitReader *br)
{
	return ((br->consumed_bits & 7) == 0);
}

inline uint32_t FLAC__bitreader_bits_left_for_byte_alignment(const FLAC__BitReader *br)
{
	return 8 - (br->consumed_bits & 7);
}

inline uint32_t FLAC__bitreader_get_input_bits_unconsumed(const FLAC__BitReader *br)
{
	return (br->words-br->consumed_words)*FLAC__BITS_PER_WORD + br->bytes*8 - br->consumed_bits;
}

void FLAC__bitreader_set_limit(FLAC__BitReader *br, uint32_t limit)
{
	br->read_limit = limit;
	br->read_limit_set = true;
}

void FLAC__bitreader_remove_limit(FLAC__BitReader *br)
{
	br->read_limit_set = false;
	br->read_limit = -1;
}

uint32_t FLAC__bitreader_limit_remaining(FLAC__BitReader *br)
{
	FLAC__ASSERT(br->read_limit_set);
	return br->read_limit;
}
void FLAC__bitreader_limit_invalidate(FLAC__BitReader *br)
{
	br->read_limit = -1;
}

FLAC__bool FLAC__bitreader_read_raw_uint32(FLAC__BitReader *br, FLAC__uint32 *val, uint32_t bits)
{
	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	FLAC__ASSERT(bits <= 32);
	FLAC__ASSERT((br->capacity*FLAC__BITS_PER_WORD) * 2 >= bits);
	FLAC__ASSERT(br->consumed_words <= br->words);

	FLAC__ASSERT(FLAC__BITS_PER_WORD >= 32);

	if(bits == 0) {
		*val = 0;
		return true;
	}

	if(br->read_limit_set && br->read_limit < (uint32_t)-1){
		if(br->read_limit < bits) {
			br->read_limit = -1;
			return false;
		}
		else
			br->read_limit -= bits;
	}

	while((br->words-br->consumed_words)*FLAC__BITS_PER_WORD + br->bytes*8 - br->consumed_bits < bits) {
		if(!bitreader_read_from_client_(br))
			return false;
	}
	if(br->consumed_words < br->words) {

		if(br->consumed_bits) {

			const uint32_t n = FLAC__BITS_PER_WORD - br->consumed_bits;
			const brword word = br->buffer[br->consumed_words];
			const brword mask = br->consumed_bits < FLAC__BITS_PER_WORD ? FLAC__WORD_ALL_ONES >> br->consumed_bits : 0;
			if(bits < n) {
				uint32_t shift = n - bits;
				*val = shift < FLAC__BITS_PER_WORD ? (FLAC__uint32)((word & mask) >> shift) : 0;
				br->consumed_bits += bits;
				return true;
			}

			*val = (FLAC__uint32)(word & mask);
			bits -= n;
			br->consumed_words++;
			br->consumed_bits = 0;
			if(bits) {
				uint32_t shift = FLAC__BITS_PER_WORD - bits;
				*val = bits < 32 ? *val << bits : 0;
				*val |= shift < FLAC__BITS_PER_WORD ? (FLAC__uint32)(br->buffer[br->consumed_words] >> shift) : 0;
				br->consumed_bits = bits;
			}
			return true;
		}
		else {
			const brword word = br->buffer[br->consumed_words];
			if(bits < FLAC__BITS_PER_WORD) {
				*val = (FLAC__uint32)(word >> (FLAC__BITS_PER_WORD-bits));
				br->consumed_bits = bits;
				return true;
			}

			*val = (FLAC__uint32)word;
			br->consumed_words++;
			return true;
		}
	}
	else {

		if(br->consumed_bits) {

			FLAC__ASSERT(br->consumed_bits + bits <= br->bytes*8);
			*val = (FLAC__uint32)((br->buffer[br->consumed_words] & (FLAC__WORD_ALL_ONES >> br->consumed_bits)) >> (FLAC__BITS_PER_WORD-br->consumed_bits-bits));
			br->consumed_bits += bits;
			return true;
		}
		else {
			*val = (FLAC__uint32)(br->buffer[br->consumed_words] >> (FLAC__BITS_PER_WORD-bits));
			br->consumed_bits += bits;
			return true;
		}
	}
}

FLAC__bool FLAC__bitreader_read_raw_int32(FLAC__BitReader *br, FLAC__int32 *val, uint32_t bits)
{
	FLAC__uint32 uval, mask;

	if (bits < 1 || ! FLAC__bitreader_read_raw_uint32(br, &uval, bits))
		return false;

	mask = bits >= 33 ? 0 : 1lu << (bits - 1);
	*val = (uval ^ mask) - mask;
	return true;
}

FLAC__bool FLAC__bitreader_read_raw_uint64(FLAC__BitReader *br, FLAC__uint64 *val, uint32_t bits)
{
	FLAC__uint32 hi, lo;

	if(bits > 32) {
		if(!FLAC__bitreader_read_raw_uint32(br, &hi, bits-32))
			return false;
		if(!FLAC__bitreader_read_raw_uint32(br, &lo, 32))
			return false;
		*val = hi;
		*val <<= 32;
		*val |= lo;
	}
	else {
		if(!FLAC__bitreader_read_raw_uint32(br, &lo, bits))
			return false;
		*val = lo;
	}
	return true;
}

FLAC__bool FLAC__bitreader_read_raw_int64(FLAC__BitReader *br, FLAC__int64 *val, uint32_t bits)
{
	FLAC__uint64 uval, mask;

	if (bits < 1 || ! FLAC__bitreader_read_raw_uint64(br, &uval, bits))
		return false;

	mask = bits >= 65 ? 0 : 1llu << (bits - 1);
	*val = (uval ^ mask) - mask;
	return true;
}

inline FLAC__bool FLAC__bitreader_read_uint32_little_endian(FLAC__BitReader *br, FLAC__uint32 *val)
{
	FLAC__uint32 x8, x32 = 0;

	if(!FLAC__bitreader_read_raw_uint32(br, &x32, 8))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(br, &x8, 8))
		return false;
	x32 |= (x8 << 8);

	if(!FLAC__bitreader_read_raw_uint32(br, &x8, 8))
		return false;
	x32 |= (x8 << 16);

	if(!FLAC__bitreader_read_raw_uint32(br, &x8, 8))
		return false;
	x32 |= (x8 << 24);

	*val = x32;
	return true;
}

FLAC__bool FLAC__bitreader_skip_bits_no_crc(FLAC__BitReader *br, uint32_t bits)
{

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	if(bits > 0) {
		const uint32_t n = br->consumed_bits & 7;
		uint32_t m;
		FLAC__uint32 x;

		if(n != 0) {
			m = flac_min(8-n, bits);
			if(!FLAC__bitreader_read_raw_uint32(br, &x, m))
				return false;
			bits -= m;
		}
		m = bits / 8;
		if(m > 0) {
			if(!FLAC__bitreader_skip_byte_block_aligned_no_crc(br, m))
				return false;
			bits %= 8;
		}
		if(bits > 0) {
			if(!FLAC__bitreader_read_raw_uint32(br, &x, bits))
				return false;
		}
	}

	return true;
}

FLAC__bool FLAC__bitreader_skip_byte_block_aligned_no_crc(FLAC__BitReader *br, uint32_t nvals)
{
	FLAC__uint32 x;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);
	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(br));

	if(br->read_limit_set && br->read_limit < (uint32_t)-1){
		if(br->read_limit < nvals*8){
			br->read_limit = -1;
			return false;
		}
	}

	while(nvals && br->consumed_bits) {
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		nvals--;
	}
	if(0 == nvals)
		return true;

	while(nvals >= FLAC__BYTES_PER_WORD) {
		if(br->consumed_words < br->words) {
			br->consumed_words++;
			nvals -= FLAC__BYTES_PER_WORD;
			if(br->read_limit_set)
				br->read_limit -= FLAC__BITS_PER_WORD;
		}
		else if(!bitreader_read_from_client_(br))
			return false;
	}

	while(nvals) {
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		nvals--;
	}

	return true;
}

FLAC__bool FLAC__bitreader_read_byte_block_aligned_no_crc(FLAC__BitReader *br, FLAC__byte *val, uint32_t nvals)
{
	FLAC__uint32 x;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);
	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(br));

	if(br->read_limit_set && br->read_limit < (uint32_t)-1){
		if(br->read_limit < nvals*8){
			br->read_limit = -1;
			return false;
		}
	}

	while(nvals && br->consumed_bits) {
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		*val++ = (FLAC__byte)x;
		nvals--;
	}
	if(0 == nvals)
		return true;

	while(nvals >= FLAC__BYTES_PER_WORD) {
		if(br->consumed_words < br->words) {
			const brword word = br->buffer[br->consumed_words++];
#if FLAC__BYTES_PER_WORD == 4
			val[0] = (FLAC__byte)(word >> 24);
			val[1] = (FLAC__byte)(word >> 16);
			val[2] = (FLAC__byte)(word >> 8);
			val[3] = (FLAC__byte)word;
#elif FLAC__BYTES_PER_WORD == 8
			val[0] = (FLAC__byte)(word >> 56);
			val[1] = (FLAC__byte)(word >> 48);
			val[2] = (FLAC__byte)(word >> 40);
			val[3] = (FLAC__byte)(word >> 32);
			val[4] = (FLAC__byte)(word >> 24);
			val[5] = (FLAC__byte)(word >> 16);
			val[6] = (FLAC__byte)(word >> 8);
			val[7] = (FLAC__byte)word;
#else
			for(x = 0; x < FLAC__BYTES_PER_WORD; x++)
				val[x] = (FLAC__byte)(word >> (8*(FLAC__BYTES_PER_WORD-x-1)));
#endif
			val += FLAC__BYTES_PER_WORD;
			nvals -= FLAC__BYTES_PER_WORD;
			if(br->read_limit_set)
				br->read_limit -= FLAC__BITS_PER_WORD;
		}
		else if(!bitreader_read_from_client_(br))
			return false;
	}

	while(nvals) {
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		*val++ = (FLAC__byte)x;
		nvals--;
	}

	return true;
}

FLAC__bool FLAC__bitreader_read_unary_unsigned(FLAC__BitReader *br, uint32_t *val)
#if 0
{
	uint32_t bit;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	*val = 0;
	while(1) {
		if(!FLAC__bitreader_read_bit(br, &bit))
			return false;
		if(bit)
			break;
		else
			*val++;
	}
	return true;
}
#else
{
	uint32_t i;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	*val = 0;
	while(1) {
		while(br->consumed_words < br->words) {
			brword b = br->consumed_bits < FLAC__BITS_PER_WORD ? br->buffer[br->consumed_words] << br->consumed_bits : 0;
			if(b) {
				i = COUNT_ZERO_MSBS(b);
				*val += i;
				i++;
				br->consumed_bits += i;
				if(br->consumed_bits >= FLAC__BITS_PER_WORD) {
					br->consumed_words++;
					br->consumed_bits = 0;
				}
				return true;
			}
			else {
				*val += FLAC__BITS_PER_WORD - br->consumed_bits;
				br->consumed_words++;
				br->consumed_bits = 0;

			}
		}

		if(br->bytes*8 > br->consumed_bits) {
			const uint32_t end = br->bytes * 8;
			brword b = (br->buffer[br->consumed_words] & (FLAC__WORD_ALL_ONES << (FLAC__BITS_PER_WORD-end))) << br->consumed_bits;
			if(b) {
				i = COUNT_ZERO_MSBS(b);
				*val += i;
				i++;
				br->consumed_bits += i;
				FLAC__ASSERT(br->consumed_bits < FLAC__BITS_PER_WORD);
				return true;
			}
			else {
				*val += end - br->consumed_bits;
				br->consumed_bits = end;
				FLAC__ASSERT(br->consumed_bits < FLAC__BITS_PER_WORD);

			}
		}
		if(!bitreader_read_from_client_(br))
			return false;
	}
}
#endif

#if 0
FLAC__bool FLAC__bitreader_read_rice_signed(FLAC__BitReader *br, int *val, uint32_t parameter)
{
	FLAC__uint32 lsbs = 0, msbs = 0;
	uint32_t uval;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);
	FLAC__ASSERT(parameter <= 31);

	if(!FLAC__bitreader_read_unary_unsigned(br, &msbs))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(br, &lsbs, parameter))
		return false;

	uval = (msbs << parameter) | lsbs;
	if(uval & 1)
		*val = -((int)(uval >> 1)) - 1;
	else
		*val = (int)(uval >> 1);

	return true;
}
#endif

FLAC__bool FLAC__bitreader_read_rice_signed_block(FLAC__BitReader *br, int vals[], uint32_t nvals, uint32_t parameter)
#include "deduplication/bitreader_read_rice_signed_block.c"

#ifdef FLAC__BMI2_SUPPORTED
FLAC__SSE_TARGET("bmi2")
FLAC__bool FLAC__bitreader_read_rice_signed_block_bmi2(FLAC__BitReader *br, int vals[], uint32_t nvals, uint32_t parameter)
#include "deduplication/bitreader_read_rice_signed_block.c"
#endif

#if 0
FLAC__bool FLAC__bitreader_read_golomb_signed(FLAC__BitReader *br, int *val, uint32_t parameter)
{
	FLAC__uint32 lsbs = 0, msbs = 0;
	uint32_t bit, uval, k;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	k = FLAC__bitmath_ilog2(parameter);

	if(!FLAC__bitreader_read_unary_unsigned(br, &msbs))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(br, &lsbs, k))
		return false;

	if(parameter == 1u<<k) {

		uval = (msbs << k) | lsbs;
	}
	else {
		uint32_t d = (1 << (k+1)) - parameter;
		if(lsbs >= d) {
			if(!FLAC__bitreader_read_bit(br, &bit))
				return false;
			lsbs <<= 1;
			lsbs |= bit;
			lsbs -= d;
		}

		uval = msbs * parameter + lsbs;
	}

	if(uval & 1)
		*val = -((int)(uval >> 1)) - 1;
	else
		*val = (int)(uval >> 1);

	return true;
}

FLAC__bool FLAC__bitreader_read_golomb_unsigned(FLAC__BitReader *br, uint32_t *val, uint32_t parameter)
{
	FLAC__uint32 lsbs, msbs = 0;
	uint32_t bit, k;

	FLAC__ASSERT(0 != br);
	FLAC__ASSERT(0 != br->buffer);

	k = FLAC__bitmath_ilog2(parameter);

	if(!FLAC__bitreader_read_unary_unsigned(br, &msbs))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(br, &lsbs, k))
		return false;

	if(parameter == 1u<<k) {

		*val = (msbs << k) | lsbs;
	}
	else {
		uint32_t d = (1 << (k+1)) - parameter;
		if(lsbs >= d) {
			if(!FLAC__bitreader_read_bit(br, &bit))
				return false;
			lsbs <<= 1;
			lsbs |= bit;
			lsbs -= d;
		}

		*val = msbs * parameter + lsbs;
	}

	return true;
}
#endif

FLAC__bool FLAC__bitreader_read_utf8_uint32(FLAC__BitReader *br, FLAC__uint32 *val, FLAC__byte *raw, uint32_t *rawlen)
{
	FLAC__uint32 v = 0;
	FLAC__uint32 x;
	uint32_t i;

	if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
		return false;
	if(raw)
		raw[(*rawlen)++] = (FLAC__byte)x;
	if(!(x & 0x80)) {
		v = x;
		i = 0;
	}
	else if(x & 0xC0 && !(x & 0x20)) {
		v = x & 0x1F;
		i = 1;
	}
	else if(x & 0xE0 && !(x & 0x10)) {
		v = x & 0x0F;
		i = 2;
	}
	else if(x & 0xF0 && !(x & 0x08)) {
		v = x & 0x07;
		i = 3;
	}
	else if(x & 0xF8 && !(x & 0x04)) {
		v = x & 0x03;
		i = 4;
	}
	else if(x & 0xFC && !(x & 0x02)) {
		v = x & 0x01;
		i = 5;
	}
	else {
		*val = 0xffffffff;
		return true;
	}
	for( ; i; i--) {
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		if(raw)
			raw[(*rawlen)++] = (FLAC__byte)x;
		if(!(x & 0x80) || (x & 0x40)) {
			*val = 0xffffffff;
			return true;
		}
		v <<= 6;
		v |= (x & 0x3F);
	}
	*val = v;
	return true;
}

FLAC__bool FLAC__bitreader_read_utf8_uint64(FLAC__BitReader *br, FLAC__uint64 *val, FLAC__byte *raw, uint32_t *rawlen)
{
	FLAC__uint64 v = 0;
	FLAC__uint32 x;
	uint32_t i;

	if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
		return false;
	if(raw)
		raw[(*rawlen)++] = (FLAC__byte)x;
	if(!(x & 0x80)) {
		v = x;
		i = 0;
	}
	else if(x & 0xC0 && !(x & 0x20)) {
		v = x & 0x1F;
		i = 1;
	}
	else if(x & 0xE0 && !(x & 0x10)) {
		v = x & 0x0F;
		i = 2;
	}
	else if(x & 0xF0 && !(x & 0x08)) {
		v = x & 0x07;
		i = 3;
	}
	else if(x & 0xF8 && !(x & 0x04)) {
		v = x & 0x03;
		i = 4;
	}
	else if(x & 0xFC && !(x & 0x02)) {
		v = x & 0x01;
		i = 5;
	}
	else if(x & 0xFE && !(x & 0x01)) {
		v = 0;
		i = 6;
	}
	else {
		*val = FLAC__U64L(0xffffffffffffffff);
		return true;
	}
	for( ; i; i--) {
		if(!FLAC__bitreader_read_raw_uint32(br, &x, 8))
			return false;
		if(raw)
			raw[(*rawlen)++] = (FLAC__byte)x;
		if(!(x & 0x80) || (x & 0x40)) {
			*val = FLAC__U64L(0xffffffffffffffff);
			return true;
		}
		v <<= 6;
		v |= (x & 0x3F);
	}
	*val = v;
	return true;
}

extern FLAC__bool FLAC__bitreader_is_consumed_byte_aligned(const FLAC__BitReader *br);
extern uint32_t FLAC__bitreader_bits_left_for_byte_alignment(const FLAC__BitReader *br);
extern uint32_t FLAC__bitreader_get_input_bits_unconsumed(const FLAC__BitReader *br);
extern FLAC__bool FLAC__bitreader_read_uint32_little_endian(FLAC__BitReader *br, FLAC__uint32 *val);
