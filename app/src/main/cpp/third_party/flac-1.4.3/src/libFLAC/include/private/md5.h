#ifndef FLAC__PRIVATE__MD5_H
#define FLAC__PRIVATE__MD5_H

#include "FLAC/ordinals.h"

typedef union {
	FLAC__byte *p8;
	FLAC__int16 *p16;
	FLAC__int32 *p32;
} FLAC__multibyte;

typedef struct {
	FLAC__uint32 in[16];
	FLAC__uint32 buf[4];
	FLAC__uint32 bytes[2];
	FLAC__multibyte internal_buf;
	size_t capacity;
} FLAC__MD5Context;

void FLAC__MD5Init(FLAC__MD5Context *context);
void FLAC__MD5Final(FLAC__byte digest[16], FLAC__MD5Context *context);

FLAC__bool FLAC__MD5Accumulate(FLAC__MD5Context *ctx, const FLAC__int32 * const signal[], uint32_t channels, uint32_t samples, uint32_t bytes_per_sample);

#endif
