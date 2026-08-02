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

#ifndef FLAC__STREAM_DECODER_H
#define FLAC__STREAM_DECODER_H

#include <stdio.h>
#include "export.h"
#include "format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {

	FLAC__STREAM_DECODER_SEARCH_FOR_METADATA = 0,

	FLAC__STREAM_DECODER_READ_METADATA,

	FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC,

	FLAC__STREAM_DECODER_READ_FRAME,

	FLAC__STREAM_DECODER_END_OF_STREAM,

	FLAC__STREAM_DECODER_OGG_ERROR,

	FLAC__STREAM_DECODER_SEEK_ERROR,

	FLAC__STREAM_DECODER_ABORTED,

	FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR,

	FLAC__STREAM_DECODER_UNINITIALIZED

} FLAC__StreamDecoderState;

extern FLAC_API const char * const FLAC__StreamDecoderStateString[];

typedef enum {

	FLAC__STREAM_DECODER_INIT_STATUS_OK = 0,

	FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER,

	FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS,

	FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR,

	FLAC__STREAM_DECODER_INIT_STATUS_ERROR_OPENING_FILE,

	FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED

} FLAC__StreamDecoderInitStatus;

extern FLAC_API const char * const FLAC__StreamDecoderInitStatusString[];

typedef enum {

	FLAC__STREAM_DECODER_READ_STATUS_CONTINUE,

	FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM,

	FLAC__STREAM_DECODER_READ_STATUS_ABORT

} FLAC__StreamDecoderReadStatus;

extern FLAC_API const char * const FLAC__StreamDecoderReadStatusString[];

typedef enum {

	FLAC__STREAM_DECODER_SEEK_STATUS_OK,

	FLAC__STREAM_DECODER_SEEK_STATUS_ERROR,

	FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED

} FLAC__StreamDecoderSeekStatus;

extern FLAC_API const char * const FLAC__StreamDecoderSeekStatusString[];

typedef enum {

	FLAC__STREAM_DECODER_TELL_STATUS_OK,

	FLAC__STREAM_DECODER_TELL_STATUS_ERROR,

	FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED

} FLAC__StreamDecoderTellStatus;

extern FLAC_API const char * const FLAC__StreamDecoderTellStatusString[];

typedef enum {

	FLAC__STREAM_DECODER_LENGTH_STATUS_OK,

	FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR,

	FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED

} FLAC__StreamDecoderLengthStatus;

extern FLAC_API const char * const FLAC__StreamDecoderLengthStatusString[];

typedef enum {

	FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE,

	FLAC__STREAM_DECODER_WRITE_STATUS_ABORT

} FLAC__StreamDecoderWriteStatus;

extern FLAC_API const char * const FLAC__StreamDecoderWriteStatusString[];

typedef enum {

	FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC,

	FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER,

	FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH,

	FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM,

	FLAC__STREAM_DECODER_ERROR_STATUS_BAD_METADATA

} FLAC__StreamDecoderErrorStatus;

extern FLAC_API const char * const FLAC__StreamDecoderErrorStatusString[];

struct FLAC__StreamDecoderProtected;
struct FLAC__StreamDecoderPrivate;

typedef struct {
	struct FLAC__StreamDecoderProtected *protected_;
	struct FLAC__StreamDecoderPrivate *private_;
} FLAC__StreamDecoder;

typedef FLAC__StreamDecoderReadStatus (*FLAC__StreamDecoderReadCallback)(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);

typedef FLAC__StreamDecoderSeekStatus (*FLAC__StreamDecoderSeekCallback)(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data);

typedef FLAC__StreamDecoderTellStatus (*FLAC__StreamDecoderTellCallback)(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data);

typedef FLAC__StreamDecoderLengthStatus (*FLAC__StreamDecoderLengthCallback)(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data);

typedef FLAC__bool (*FLAC__StreamDecoderEofCallback)(const FLAC__StreamDecoder *decoder, void *client_data);

typedef FLAC__StreamDecoderWriteStatus (*FLAC__StreamDecoderWriteCallback)(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data);

typedef void (*FLAC__StreamDecoderMetadataCallback)(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);

typedef void (*FLAC__StreamDecoderErrorCallback)(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

FLAC_API FLAC__StreamDecoder *FLAC__stream_decoder_new(void);

FLAC_API void FLAC__stream_decoder_delete(FLAC__StreamDecoder *decoder);

FLAC_API FLAC__bool FLAC__stream_decoder_set_ogg_serial_number(FLAC__StreamDecoder *decoder, long serial_number);

FLAC_API FLAC__bool FLAC__stream_decoder_set_md5_checking(FLAC__StreamDecoder *decoder, FLAC__bool value);

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond(FLAC__StreamDecoder *decoder, FLAC__MetadataType type);

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond_application(FLAC__StreamDecoder *decoder, const FLAC__byte id[4]);

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond_all(FLAC__StreamDecoder *decoder);

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore(FLAC__StreamDecoder *decoder, FLAC__MetadataType type);

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore_application(FLAC__StreamDecoder *decoder, const FLAC__byte id[4]);

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore_all(FLAC__StreamDecoder *decoder);

FLAC_API FLAC__StreamDecoderState FLAC__stream_decoder_get_state(const FLAC__StreamDecoder *decoder);

FLAC_API const char *FLAC__stream_decoder_get_resolved_state_string(const FLAC__StreamDecoder *decoder);

FLAC_API FLAC__bool FLAC__stream_decoder_get_md5_checking(const FLAC__StreamDecoder *decoder);

FLAC_API FLAC__uint64 FLAC__stream_decoder_get_total_samples(const FLAC__StreamDecoder *decoder);

FLAC_API uint32_t FLAC__stream_decoder_get_channels(const FLAC__StreamDecoder *decoder);

FLAC_API FLAC__ChannelAssignment FLAC__stream_decoder_get_channel_assignment(const FLAC__StreamDecoder *decoder);

FLAC_API uint32_t FLAC__stream_decoder_get_bits_per_sample(const FLAC__StreamDecoder *decoder);

FLAC_API uint32_t FLAC__stream_decoder_get_sample_rate(const FLAC__StreamDecoder *decoder);

FLAC_API uint32_t FLAC__stream_decoder_get_blocksize(const FLAC__StreamDecoder *decoder);

FLAC_API FLAC__bool FLAC__stream_decoder_get_decode_position(const FLAC__StreamDecoder *decoder, FLAC__uint64 *position);

FLAC_API const void *FLAC__stream_decoder_get_client_data(FLAC__StreamDecoder *decoder);

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_stream(
	FLAC__StreamDecoder *decoder,
	FLAC__StreamDecoderReadCallback read_callback,
	FLAC__StreamDecoderSeekCallback seek_callback,
	FLAC__StreamDecoderTellCallback tell_callback,
	FLAC__StreamDecoderLengthCallback length_callback,
	FLAC__StreamDecoderEofCallback eof_callback,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_ogg_stream(
	FLAC__StreamDecoder *decoder,
	FLAC__StreamDecoderReadCallback read_callback,
	FLAC__StreamDecoderSeekCallback seek_callback,
	FLAC__StreamDecoderTellCallback tell_callback,
	FLAC__StreamDecoderLengthCallback length_callback,
	FLAC__StreamDecoderEofCallback eof_callback,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_FILE(
	FLAC__StreamDecoder *decoder,
	FILE *file,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_ogg_FILE(
	FLAC__StreamDecoder *decoder,
	FILE *file,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_file(
	FLAC__StreamDecoder *decoder,
	const char *filename,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_ogg_file(
	FLAC__StreamDecoder *decoder,
	const char *filename,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
);

FLAC_API FLAC__bool FLAC__stream_decoder_finish(FLAC__StreamDecoder *decoder);

FLAC_API FLAC__bool FLAC__stream_decoder_flush(FLAC__StreamDecoder *decoder);

FLAC_API FLAC__bool FLAC__stream_decoder_reset(FLAC__StreamDecoder *decoder);

FLAC_API FLAC__bool FLAC__stream_decoder_process_single(FLAC__StreamDecoder *decoder);

FLAC_API FLAC__bool FLAC__stream_decoder_process_until_end_of_metadata(FLAC__StreamDecoder *decoder);

FLAC_API FLAC__bool FLAC__stream_decoder_process_until_end_of_stream(FLAC__StreamDecoder *decoder);

FLAC_API FLAC__bool FLAC__stream_decoder_skip_single_frame(FLAC__StreamDecoder *decoder);

FLAC_API FLAC__bool FLAC__stream_decoder_seek_absolute(FLAC__StreamDecoder *decoder, FLAC__uint64 sample);

#ifdef __cplusplus
}
#endif

#endif
