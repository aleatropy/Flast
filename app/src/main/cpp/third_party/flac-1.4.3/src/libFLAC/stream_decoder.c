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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "share/compat.h"
#include "FLAC/assert.h"
#include "share/alloc.h"
#include "protected/stream_decoder.h"
#include "private/bitreader.h"
#include "private/bitmath.h"
#include "private/cpu.h"
#include "private/crc.h"
#include "private/fixed.h"
#include "private/format.h"
#include "private/lpc.h"
#include "private/md5.h"
#include "private/memory.h"
#include "private/macros.h"

FLAC_API int FLAC_API_SUPPORTS_OGG_FLAC = FLAC__HAS_OGG;

static const FLAC__byte ID3V2_TAG_[3] = { 'I', 'D', '3' };

static void set_defaults_(FLAC__StreamDecoder *decoder);
static FILE *get_binary_stdin_(void);
static FLAC__bool allocate_output_(FLAC__StreamDecoder *decoder, uint32_t size, uint32_t channels, uint32_t bps);
static FLAC__bool has_id_filtered_(FLAC__StreamDecoder *decoder, FLAC__byte *id);
static FLAC__bool find_metadata_(FLAC__StreamDecoder *decoder);
static FLAC__bool read_metadata_(FLAC__StreamDecoder *decoder);
static FLAC__bool read_metadata_streaminfo_(FLAC__StreamDecoder *decoder, FLAC__bool is_last, uint32_t length);
static FLAC__bool read_metadata_seektable_(FLAC__StreamDecoder *decoder, FLAC__bool is_last, uint32_t length);
static FLAC__bool read_metadata_vorbiscomment_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_VorbisComment *obj, uint32_t length);
static FLAC__bool read_metadata_cuesheet_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_CueSheet *obj);
static FLAC__bool read_metadata_picture_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_Picture *obj);
static FLAC__bool skip_id3v2_tag_(FLAC__StreamDecoder *decoder);
static FLAC__bool frame_sync_(FLAC__StreamDecoder *decoder);
static FLAC__bool read_frame_(FLAC__StreamDecoder *decoder, FLAC__bool *got_a_frame, FLAC__bool do_full_decode);
static FLAC__bool read_frame_header_(FLAC__StreamDecoder *decoder);
static FLAC__bool read_subframe_(FLAC__StreamDecoder *decoder, uint32_t channel, uint32_t bps, FLAC__bool do_full_decode);
static FLAC__bool read_subframe_constant_(FLAC__StreamDecoder *decoder, uint32_t channel, uint32_t bps, FLAC__bool do_full_decode);
static FLAC__bool read_subframe_fixed_(FLAC__StreamDecoder *decoder, uint32_t channel, uint32_t bps, const uint32_t order, FLAC__bool do_full_decode);
static FLAC__bool read_subframe_lpc_(FLAC__StreamDecoder *decoder, uint32_t channel, uint32_t bps, const uint32_t order, FLAC__bool do_full_decode);
static FLAC__bool read_subframe_verbatim_(FLAC__StreamDecoder *decoder, uint32_t channel, uint32_t bps, FLAC__bool do_full_decode);
static FLAC__bool read_residual_partitioned_rice_(FLAC__StreamDecoder *decoder, uint32_t predictor_order, uint32_t partition_order, FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents, FLAC__int32 *residual, FLAC__bool is_extended);
static FLAC__bool read_zero_padding_(FLAC__StreamDecoder *decoder);
static void       undo_channel_coding(FLAC__StreamDecoder *decoder);
static FLAC__bool read_callback_(FLAC__byte buffer[], size_t *bytes, void *client_data);
#if FLAC__HAS_OGG
static FLAC__StreamDecoderReadStatus read_callback_ogg_aspect_(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes);
static FLAC__OggDecoderAspectReadStatus read_callback_proxy_(const void *void_decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);
#endif
static FLAC__StreamDecoderWriteStatus write_audio_frame_to_client_(FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[]);
static void send_error_to_client_(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status);
static FLAC__bool seek_to_absolute_sample_(FLAC__StreamDecoder *decoder, FLAC__uint64 stream_length, FLAC__uint64 target_sample);
#if FLAC__HAS_OGG
static FLAC__bool seek_to_absolute_sample_ogg_(FLAC__StreamDecoder *decoder, FLAC__uint64 stream_length, FLAC__uint64 target_sample);
#endif
static FLAC__StreamDecoderReadStatus file_read_callback_(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);
static FLAC__StreamDecoderSeekStatus file_seek_callback_(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data);
static FLAC__StreamDecoderTellStatus file_tell_callback_(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data);
static FLAC__StreamDecoderLengthStatus file_length_callback_(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data);
static FLAC__bool file_eof_callback_(const FLAC__StreamDecoder *decoder, void *client_data);

typedef struct FLAC__StreamDecoderPrivate {
	FLAC__bool is_ogg;
	FLAC__StreamDecoderReadCallback read_callback;
	FLAC__StreamDecoderSeekCallback seek_callback;
	FLAC__StreamDecoderTellCallback tell_callback;
	FLAC__StreamDecoderLengthCallback length_callback;
	FLAC__StreamDecoderEofCallback eof_callback;
	FLAC__StreamDecoderWriteCallback write_callback;
	FLAC__StreamDecoderMetadataCallback metadata_callback;
	FLAC__StreamDecoderErrorCallback error_callback;
	void *client_data;
	FILE *file;
	FLAC__BitReader *input;
	FLAC__int32 *output[FLAC__MAX_CHANNELS];
	FLAC__int32 *residual[FLAC__MAX_CHANNELS];
	FLAC__int64 *side_subframe;
	FLAC__bool side_subframe_in_use;
	FLAC__EntropyCodingMethod_PartitionedRiceContents partitioned_rice_contents[FLAC__MAX_CHANNELS];
	uint32_t output_capacity, output_channels;
	FLAC__uint32 fixed_block_size, next_fixed_block_size;
	FLAC__uint64 samples_decoded;
	FLAC__bool has_stream_info, has_seek_table;
	FLAC__StreamMetadata stream_info;
	FLAC__StreamMetadata seek_table;
	FLAC__bool metadata_filter[128];
	FLAC__byte *metadata_filter_ids;
	size_t metadata_filter_ids_count, metadata_filter_ids_capacity;
	FLAC__Frame frame;
	FLAC__bool cached;
	FLAC__CPUInfo cpuinfo;
	FLAC__byte header_warmup[2];
	FLAC__byte lookahead;

	FLAC__int32 *residual_unaligned[FLAC__MAX_CHANNELS];
	FLAC__bool do_md5_checking;
	FLAC__bool internal_reset_hack;
	FLAC__bool is_seeking;
	FLAC__MD5Context md5context;
	FLAC__byte computed_md5sum[16];

	FLAC__Frame last_frame;
	FLAC__bool last_frame_is_set;
	FLAC__uint64 first_frame_offset;
	FLAC__uint64 last_seen_framesync;
	FLAC__uint64 target_sample;
	uint32_t unparseable_frame_count;
	FLAC__bool got_a_frame;
	FLAC__bool (*local_bitreader_read_rice_signed_block)(FLAC__BitReader *br, int vals[], uint32_t nvals, uint32_t parameter);
} FLAC__StreamDecoderPrivate;

FLAC_API const char * const FLAC__StreamDecoderStateString[] = {
	"FLAC__STREAM_DECODER_SEARCH_FOR_METADATA",
	"FLAC__STREAM_DECODER_READ_METADATA",
	"FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC",
	"FLAC__STREAM_DECODER_READ_FRAME",
	"FLAC__STREAM_DECODER_END_OF_STREAM",
	"FLAC__STREAM_DECODER_OGG_ERROR",
	"FLAC__STREAM_DECODER_SEEK_ERROR",
	"FLAC__STREAM_DECODER_ABORTED",
	"FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR",
	"FLAC__STREAM_DECODER_UNINITIALIZED"
};

FLAC_API const char * const FLAC__StreamDecoderInitStatusString[] = {
	"FLAC__STREAM_DECODER_INIT_STATUS_OK",
	"FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER",
	"FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS",
	"FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR",
	"FLAC__STREAM_DECODER_INIT_STATUS_ERROR_OPENING_FILE",
	"FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED"
};

FLAC_API const char * const FLAC__StreamDecoderReadStatusString[] = {
	"FLAC__STREAM_DECODER_READ_STATUS_CONTINUE",
	"FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM",
	"FLAC__STREAM_DECODER_READ_STATUS_ABORT"
};

FLAC_API const char * const FLAC__StreamDecoderSeekStatusString[] = {
	"FLAC__STREAM_DECODER_SEEK_STATUS_OK",
	"FLAC__STREAM_DECODER_SEEK_STATUS_ERROR",
	"FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED"
};

FLAC_API const char * const FLAC__StreamDecoderTellStatusString[] = {
	"FLAC__STREAM_DECODER_TELL_STATUS_OK",
	"FLAC__STREAM_DECODER_TELL_STATUS_ERROR",
	"FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED"
};

FLAC_API const char * const FLAC__StreamDecoderLengthStatusString[] = {
	"FLAC__STREAM_DECODER_LENGTH_STATUS_OK",
	"FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR",
	"FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED"
};

FLAC_API const char * const FLAC__StreamDecoderWriteStatusString[] = {
	"FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE",
	"FLAC__STREAM_DECODER_WRITE_STATUS_ABORT"
};

FLAC_API const char * const FLAC__StreamDecoderErrorStatusString[] = {
	"FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC",
	"FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER",
	"FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH",
	"FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM",
	"FLAC__STREAM_DECODER_ERROR_STATUS_BAD_METADATA"
};

FLAC_API FLAC__StreamDecoder *FLAC__stream_decoder_new(void)
{
	FLAC__StreamDecoder *decoder;
	uint32_t i;

	FLAC__ASSERT(sizeof(int) >= 4);

	decoder = calloc(1, sizeof(FLAC__StreamDecoder));
	if(decoder == 0) {
		return 0;
	}

	decoder->protected_ = calloc(1, sizeof(FLAC__StreamDecoderProtected));
	if(decoder->protected_ == 0) {
		free(decoder);
		return 0;
	}

	decoder->private_ = calloc(1, sizeof(FLAC__StreamDecoderPrivate));
	if(decoder->private_ == 0) {
		free(decoder->protected_);
		free(decoder);
		return 0;
	}

	decoder->private_->input = FLAC__bitreader_new();
	if(decoder->private_->input == 0) {
		free(decoder->private_);
		free(decoder->protected_);
		free(decoder);
		return 0;
	}

	decoder->private_->metadata_filter_ids_capacity = 16;
	if(0 == (decoder->private_->metadata_filter_ids = malloc((FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8) * decoder->private_->metadata_filter_ids_capacity))) {
		FLAC__bitreader_delete(decoder->private_->input);
		free(decoder->private_);
		free(decoder->protected_);
		free(decoder);
		return 0;
	}

	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		decoder->private_->output[i] = 0;
		decoder->private_->residual_unaligned[i] = decoder->private_->residual[i] = 0;
	}

	decoder->private_->side_subframe = 0;

	decoder->private_->output_capacity = 0;
	decoder->private_->output_channels = 0;
	decoder->private_->has_seek_table = false;

	for(i = 0; i < FLAC__MAX_CHANNELS; i++)
		FLAC__format_entropy_coding_method_partitioned_rice_contents_init(&decoder->private_->partitioned_rice_contents[i]);

	decoder->private_->file = 0;

	set_defaults_(decoder);

	decoder->protected_->state = FLAC__STREAM_DECODER_UNINITIALIZED;

	return decoder;
}

FLAC_API void FLAC__stream_decoder_delete(FLAC__StreamDecoder *decoder)
{
	uint32_t i;

	if (decoder == NULL)
		return ;

	FLAC__ASSERT(0 != decoder->protected_);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->private_->input);

	(void)FLAC__stream_decoder_finish(decoder);

	if(0 != decoder->private_->metadata_filter_ids)
		free(decoder->private_->metadata_filter_ids);

	FLAC__bitreader_delete(decoder->private_->input);

	for(i = 0; i < FLAC__MAX_CHANNELS; i++)
		FLAC__format_entropy_coding_method_partitioned_rice_contents_clear(&decoder->private_->partitioned_rice_contents[i]);

	free(decoder->private_);
	free(decoder->protected_);
	free(decoder);
}

static FLAC__StreamDecoderInitStatus init_stream_internal_(
	FLAC__StreamDecoder *decoder,
	FLAC__StreamDecoderReadCallback read_callback,
	FLAC__StreamDecoderSeekCallback seek_callback,
	FLAC__StreamDecoderTellCallback tell_callback,
	FLAC__StreamDecoderLengthCallback length_callback,
	FLAC__StreamDecoderEofCallback eof_callback,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data,
	FLAC__bool is_ogg
)
{
	FLAC__ASSERT(0 != decoder);

	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED;

	if(FLAC__HAS_OGG == 0 && is_ogg)
		return FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER;

	if(
		0 == read_callback ||
		0 == write_callback ||
		0 == error_callback ||
		(seek_callback && (0 == tell_callback || 0 == length_callback || 0 == eof_callback))
	)
		return FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS;

#if FLAC__HAS_OGG
	decoder->private_->is_ogg = is_ogg;
	if(is_ogg && !FLAC__ogg_decoder_aspect_init(&decoder->protected_->ogg_decoder_aspect))
		return decoder->protected_->initstate = FLAC__STREAM_DECODER_INIT_STATUS_ERROR_OPENING_FILE;
#endif

	FLAC__cpu_info(&decoder->private_->cpuinfo);
	decoder->private_->local_bitreader_read_rice_signed_block = FLAC__bitreader_read_rice_signed_block;

#ifdef FLAC__BMI2_SUPPORTED
	if (decoder->private_->cpuinfo.x86.bmi2) {
		decoder->private_->local_bitreader_read_rice_signed_block = FLAC__bitreader_read_rice_signed_block_bmi2;
	}
#endif

	if(!FLAC__bitreader_init(decoder->private_->input, read_callback_, decoder)) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR;
	}

	decoder->private_->read_callback = read_callback;
	decoder->private_->seek_callback = seek_callback;
	decoder->private_->tell_callback = tell_callback;
	decoder->private_->length_callback = length_callback;
	decoder->private_->eof_callback = eof_callback;
	decoder->private_->write_callback = write_callback;
	decoder->private_->metadata_callback = metadata_callback;
	decoder->private_->error_callback = error_callback;
	decoder->private_->client_data = client_data;
	decoder->private_->fixed_block_size = decoder->private_->next_fixed_block_size = 0;
	decoder->private_->samples_decoded = 0;
	decoder->private_->has_stream_info = false;
	decoder->private_->cached = false;

	decoder->private_->do_md5_checking = decoder->protected_->md5_checking;
	decoder->private_->is_seeking = false;

	decoder->private_->internal_reset_hack = true;
	if(!FLAC__stream_decoder_reset(decoder)) {

		return FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR;
	}

	return FLAC__STREAM_DECODER_INIT_STATUS_OK;
}

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
)
{
	return init_stream_internal_(
		decoder,
		read_callback,
		seek_callback,
		tell_callback,
		length_callback,
		eof_callback,
		write_callback,
		metadata_callback,
		error_callback,
		client_data,
		false
	);
}

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
)
{
	return init_stream_internal_(
		decoder,
		read_callback,
		seek_callback,
		tell_callback,
		length_callback,
		eof_callback,
		write_callback,
		metadata_callback,
		error_callback,
		client_data,
		true
	);
}

static FLAC__StreamDecoderInitStatus init_FILE_internal_(
	FLAC__StreamDecoder *decoder,
	FILE *file,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data,
	FLAC__bool is_ogg
)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != file);

	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return decoder->protected_->initstate = FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED;

	if(0 == write_callback || 0 == error_callback)
		return decoder->protected_->initstate = FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS;

	if(file == stdin)
		file = get_binary_stdin_();

	decoder->private_->file = file;

	return init_stream_internal_(
		decoder,
		file_read_callback_,
		decoder->private_->file == stdin? 0: file_seek_callback_,
		decoder->private_->file == stdin? 0: file_tell_callback_,
		decoder->private_->file == stdin? 0: file_length_callback_,
		file_eof_callback_,
		write_callback,
		metadata_callback,
		error_callback,
		client_data,
		is_ogg
	);
}

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_FILE(
	FLAC__StreamDecoder *decoder,
	FILE *file,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
)
{
	return init_FILE_internal_(decoder, file, write_callback, metadata_callback, error_callback, client_data, false);
}

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_ogg_FILE(
	FLAC__StreamDecoder *decoder,
	FILE *file,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
)
{
	return init_FILE_internal_(decoder, file, write_callback, metadata_callback, error_callback, client_data, true);
}

static FLAC__StreamDecoderInitStatus init_file_internal_(
	FLAC__StreamDecoder *decoder,
	const char *filename,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data,
	FLAC__bool is_ogg
)
{
	FILE *file;

	FLAC__ASSERT(0 != decoder);

	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return decoder->protected_->initstate = FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED;

	if(0 == write_callback || 0 == error_callback)
		return decoder->protected_->initstate = FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS;

	file = filename? flac_fopen(filename, "rb") : stdin;

	if(0 == file)
		return FLAC__STREAM_DECODER_INIT_STATUS_ERROR_OPENING_FILE;

	return init_FILE_internal_(decoder, file, write_callback, metadata_callback, error_callback, client_data, is_ogg);
}

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_file(
	FLAC__StreamDecoder *decoder,
	const char *filename,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
)
{
	return init_file_internal_(decoder, filename, write_callback, metadata_callback, error_callback, client_data, false);
}

FLAC_API FLAC__StreamDecoderInitStatus FLAC__stream_decoder_init_ogg_file(
	FLAC__StreamDecoder *decoder,
	const char *filename,
	FLAC__StreamDecoderWriteCallback write_callback,
	FLAC__StreamDecoderMetadataCallback metadata_callback,
	FLAC__StreamDecoderErrorCallback error_callback,
	void *client_data
)
{
	return init_file_internal_(decoder, filename, write_callback, metadata_callback, error_callback, client_data, true);
}

FLAC_API FLAC__bool FLAC__stream_decoder_finish(FLAC__StreamDecoder *decoder)
{
	FLAC__bool md5_failed = false;
	uint32_t i;

	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);

	if(decoder->protected_->state == FLAC__STREAM_DECODER_UNINITIALIZED)
		return true;

	FLAC__MD5Final(decoder->private_->computed_md5sum, &decoder->private_->md5context);

	free(decoder->private_->seek_table.data.seek_table.points);
	decoder->private_->seek_table.data.seek_table.points = 0;
	decoder->private_->has_seek_table = false;

	FLAC__bitreader_free(decoder->private_->input);
	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {

		if(0 != decoder->private_->output[i]) {
			free(decoder->private_->output[i]-4);
			decoder->private_->output[i] = 0;
		}
		if(0 != decoder->private_->residual_unaligned[i]) {
			free(decoder->private_->residual_unaligned[i]);
			decoder->private_->residual_unaligned[i] = decoder->private_->residual[i] = 0;
		}
	}
	if(0 != decoder->private_->side_subframe) {
		free(decoder->private_->side_subframe);
		decoder->private_->side_subframe = 0;
	}
	decoder->private_->output_capacity = 0;
	decoder->private_->output_channels = 0;

#if FLAC__HAS_OGG
	if(decoder->private_->is_ogg)
		FLAC__ogg_decoder_aspect_finish(&decoder->protected_->ogg_decoder_aspect);
#endif

	if(0 != decoder->private_->file) {
		if(decoder->private_->file != stdin)
			fclose(decoder->private_->file);
		decoder->private_->file = 0;
	}

	if(decoder->private_->do_md5_checking) {
		if(memcmp(decoder->private_->stream_info.data.stream_info.md5sum, decoder->private_->computed_md5sum, 16))
			md5_failed = true;
	}
	decoder->private_->is_seeking = false;

	set_defaults_(decoder);

	decoder->protected_->state = FLAC__STREAM_DECODER_UNINITIALIZED;

	return !md5_failed;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_ogg_serial_number(FLAC__StreamDecoder *decoder, long value)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
#if FLAC__HAS_OGG

	FLAC__ogg_decoder_aspect_set_serial_number(&decoder->protected_->ogg_decoder_aspect, value);
	return true;
#else
	(void)value;
	return false;
#endif
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_md5_checking(FLAC__StreamDecoder *decoder, FLAC__bool value)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	decoder->protected_->md5_checking = value;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond(FLAC__StreamDecoder *decoder, FLAC__MetadataType type)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	FLAC__ASSERT((uint32_t)type <= FLAC__MAX_METADATA_TYPE_CODE);

	if((uint32_t)type > FLAC__MAX_METADATA_TYPE_CODE)
		return false;
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	decoder->private_->metadata_filter[type] = true;
	if(type == FLAC__METADATA_TYPE_APPLICATION)
		decoder->private_->metadata_filter_ids_count = 0;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond_application(FLAC__StreamDecoder *decoder, const FLAC__byte id[4])
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	FLAC__ASSERT(0 != id);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;

	if(decoder->private_->metadata_filter[FLAC__METADATA_TYPE_APPLICATION])
		return true;

	FLAC__ASSERT(0 != decoder->private_->metadata_filter_ids);

	if(decoder->private_->metadata_filter_ids_count == decoder->private_->metadata_filter_ids_capacity) {
		if(0 == (decoder->private_->metadata_filter_ids = safe_realloc_mul_2op_(decoder->private_->metadata_filter_ids, decoder->private_->metadata_filter_ids_capacity, 2))) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		decoder->private_->metadata_filter_ids_capacity *= 2;
	}

	memcpy(decoder->private_->metadata_filter_ids + decoder->private_->metadata_filter_ids_count * (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8), id, (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8));
	decoder->private_->metadata_filter_ids_count++;

	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_respond_all(FLAC__StreamDecoder *decoder)
{
	uint32_t i;
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	for(i = 0; i < sizeof(decoder->private_->metadata_filter) / sizeof(decoder->private_->metadata_filter[0]); i++)
		decoder->private_->metadata_filter[i] = true;
	decoder->private_->metadata_filter_ids_count = 0;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore(FLAC__StreamDecoder *decoder, FLAC__MetadataType type)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	FLAC__ASSERT((uint32_t)type <= FLAC__MAX_METADATA_TYPE_CODE);

	if((uint32_t)type > FLAC__MAX_METADATA_TYPE_CODE)
		return false;
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	decoder->private_->metadata_filter[type] = false;
	if(type == FLAC__METADATA_TYPE_APPLICATION)
		decoder->private_->metadata_filter_ids_count = 0;
	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore_application(FLAC__StreamDecoder *decoder, const FLAC__byte id[4])
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	FLAC__ASSERT(0 != id);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;

	if(!decoder->private_->metadata_filter[FLAC__METADATA_TYPE_APPLICATION])
		return true;

	FLAC__ASSERT(0 != decoder->private_->metadata_filter_ids);

	if(decoder->private_->metadata_filter_ids_count == decoder->private_->metadata_filter_ids_capacity) {
		if(0 == (decoder->private_->metadata_filter_ids = safe_realloc_mul_2op_(decoder->private_->metadata_filter_ids, decoder->private_->metadata_filter_ids_capacity, 2))) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		decoder->private_->metadata_filter_ids_capacity *= 2;
	}

	memcpy(decoder->private_->metadata_filter_ids + decoder->private_->metadata_filter_ids_count * (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8), id, (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8));
	decoder->private_->metadata_filter_ids_count++;

	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_set_metadata_ignore_all(FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);
	if(decoder->protected_->state != FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;
	memset(decoder->private_->metadata_filter, 0, sizeof(decoder->private_->metadata_filter));
	decoder->private_->metadata_filter_ids_count = 0;
	return true;
}

FLAC_API FLAC__StreamDecoderState FLAC__stream_decoder_get_state(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->state;
}

FLAC_API const char *FLAC__stream_decoder_get_resolved_state_string(const FLAC__StreamDecoder *decoder)
{
	return FLAC__StreamDecoderStateString[decoder->protected_->state];
}

FLAC_API FLAC__bool FLAC__stream_decoder_get_md5_checking(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->md5_checking;
}

FLAC_API FLAC__uint64 FLAC__stream_decoder_get_total_samples(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->private_->has_stream_info? decoder->private_->stream_info.data.stream_info.total_samples : 0;
}

FLAC_API uint32_t FLAC__stream_decoder_get_channels(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->channels;
}

FLAC_API FLAC__ChannelAssignment FLAC__stream_decoder_get_channel_assignment(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->channel_assignment;
}

FLAC_API uint32_t FLAC__stream_decoder_get_bits_per_sample(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->bits_per_sample;
}

FLAC_API uint32_t FLAC__stream_decoder_get_sample_rate(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->sample_rate;
}

FLAC_API uint32_t FLAC__stream_decoder_get_blocksize(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);
	return decoder->protected_->blocksize;
}

FLAC_API FLAC__bool FLAC__stream_decoder_get_decode_position(const FLAC__StreamDecoder *decoder, FLAC__uint64 *position)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != position);

	if(FLAC__HAS_OGG && decoder->private_->is_ogg)
		return false;

	if(0 == decoder->private_->tell_callback)
		return false;
	if(decoder->private_->tell_callback(decoder, position, decoder->private_->client_data) != FLAC__STREAM_DECODER_TELL_STATUS_OK)
		return false;

	if(!FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input))
		return false;
	FLAC__ASSERT(*position >= FLAC__stream_decoder_get_input_bytes_unconsumed(decoder));
	*position -= FLAC__stream_decoder_get_input_bytes_unconsumed(decoder);
	return true;
}

FLAC_API const void *FLAC__stream_decoder_get_client_data(FLAC__StreamDecoder *decoder)
{
	return decoder->private_->client_data;
}

FLAC_API FLAC__bool FLAC__stream_decoder_flush(FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);

	if(!decoder->private_->internal_reset_hack && decoder->protected_->state == FLAC__STREAM_DECODER_UNINITIALIZED)
		return false;

	decoder->private_->samples_decoded = 0;
	decoder->private_->do_md5_checking = false;
	decoder->private_->last_seen_framesync = 0;
	decoder->private_->last_frame_is_set = false;

#if FLAC__HAS_OGG
	if(decoder->private_->is_ogg)
		FLAC__ogg_decoder_aspect_flush(&decoder->protected_->ogg_decoder_aspect);
#endif

	if(!FLAC__bitreader_clear(decoder->private_->input)) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;

	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_reset(FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);
	FLAC__ASSERT(0 != decoder->protected_);

	if(!FLAC__stream_decoder_flush(decoder)) {

		return false;
	}

#if FLAC__HAS_OGG

	if(decoder->private_->is_ogg)
		FLAC__ogg_decoder_aspect_reset(&decoder->protected_->ogg_decoder_aspect);
#endif

	if(!decoder->private_->internal_reset_hack) {
		if(decoder->private_->file == stdin)
			return false;
		if(decoder->private_->seek_callback && decoder->private_->seek_callback(decoder, 0, decoder->private_->client_data) == FLAC__STREAM_DECODER_SEEK_STATUS_ERROR)
			return false;
	}

	decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_METADATA;

	decoder->private_->has_stream_info = false;

	free(decoder->private_->seek_table.data.seek_table.points);
	decoder->private_->seek_table.data.seek_table.points = 0;
	decoder->private_->has_seek_table = false;

	decoder->private_->do_md5_checking = decoder->protected_->md5_checking;

	decoder->private_->fixed_block_size = decoder->private_->next_fixed_block_size = 0;

	if(!decoder->private_->internal_reset_hack) {

		FLAC__MD5Final(decoder->private_->computed_md5sum, &decoder->private_->md5context);
	}
	else
		decoder->private_->internal_reset_hack = false;
	FLAC__MD5Init(&decoder->private_->md5context);

	decoder->private_->first_frame_offset = 0;
	decoder->private_->unparseable_frame_count = 0;
	decoder->private_->last_seen_framesync = 0;
	decoder->private_->last_frame_is_set = false;

	return true;
}

FLAC_API FLAC__bool FLAC__stream_decoder_process_single(FLAC__StreamDecoder *decoder)
{
	FLAC__bool got_a_frame;
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);

	while(1) {
		switch(decoder->protected_->state) {
			case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA:
				if(!find_metadata_(decoder))
					return false;
				break;
			case FLAC__STREAM_DECODER_READ_METADATA:
				if(!read_metadata_(decoder))
					return false;
				else
					return true;
			case FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC:
				if(!frame_sync_(decoder))
					return true;
				break;
			case FLAC__STREAM_DECODER_READ_FRAME:
				if(!read_frame_(decoder, &got_a_frame, true))
					return false;
				if(got_a_frame)
					return true;
				break;
			case FLAC__STREAM_DECODER_END_OF_STREAM:
			case FLAC__STREAM_DECODER_ABORTED:
				return true;
			default:
				return false;
		}
	}
}

FLAC_API FLAC__bool FLAC__stream_decoder_process_until_end_of_metadata(FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);

	while(1) {
		switch(decoder->protected_->state) {
			case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA:
				if(!find_metadata_(decoder))
					return false;
				break;
			case FLAC__STREAM_DECODER_READ_METADATA:
				if(!read_metadata_(decoder))
					return false;
				break;
			case FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC:
			case FLAC__STREAM_DECODER_READ_FRAME:
			case FLAC__STREAM_DECODER_END_OF_STREAM:
			case FLAC__STREAM_DECODER_ABORTED:
				return true;
			default:
				return false;
		}
	}
}

FLAC_API FLAC__bool FLAC__stream_decoder_process_until_end_of_stream(FLAC__StreamDecoder *decoder)
{
	FLAC__bool dummy;
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);

	while(1) {
		switch(decoder->protected_->state) {
			case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA:
				if(!find_metadata_(decoder))
					return false;
				break;
			case FLAC__STREAM_DECODER_READ_METADATA:
				if(!read_metadata_(decoder))
					return false;
				break;
			case FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC:
				if(!frame_sync_(decoder))
					return true;
				break;
			case FLAC__STREAM_DECODER_READ_FRAME:
				if(!read_frame_(decoder, &dummy, true))
					return false;
				break;
			case FLAC__STREAM_DECODER_END_OF_STREAM:
			case FLAC__STREAM_DECODER_ABORTED:
				return true;
			default:
				return false;
		}
	}
}

FLAC_API FLAC__bool FLAC__stream_decoder_skip_single_frame(FLAC__StreamDecoder *decoder)
{
	FLAC__bool got_a_frame;
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->protected_);

	while(1) {
		switch(decoder->protected_->state) {
			case FLAC__STREAM_DECODER_SEARCH_FOR_METADATA:
			case FLAC__STREAM_DECODER_READ_METADATA:
				return false;
			case FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC:
				if(!frame_sync_(decoder))
					return true;
				break;
			case FLAC__STREAM_DECODER_READ_FRAME:
				if(!read_frame_(decoder, &got_a_frame, false))
					return false;
				if(got_a_frame)
					return true;
				break;
			case FLAC__STREAM_DECODER_END_OF_STREAM:
			case FLAC__STREAM_DECODER_ABORTED:
				return true;
			default:
				return false;
		}
	}
}

FLAC_API FLAC__bool FLAC__stream_decoder_seek_absolute(FLAC__StreamDecoder *decoder, FLAC__uint64 sample)
{
	FLAC__uint64 length;

	FLAC__ASSERT(0 != decoder);

	if(
		decoder->protected_->state != FLAC__STREAM_DECODER_SEARCH_FOR_METADATA &&
		decoder->protected_->state != FLAC__STREAM_DECODER_READ_METADATA &&
		decoder->protected_->state != FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC &&
		decoder->protected_->state != FLAC__STREAM_DECODER_READ_FRAME &&
		decoder->protected_->state != FLAC__STREAM_DECODER_END_OF_STREAM
	)
		return false;

	if(0 == decoder->private_->seek_callback)
		return false;

	FLAC__ASSERT(decoder->private_->seek_callback);
	FLAC__ASSERT(decoder->private_->tell_callback);
	FLAC__ASSERT(decoder->private_->length_callback);
	FLAC__ASSERT(decoder->private_->eof_callback);

	if(FLAC__stream_decoder_get_total_samples(decoder) > 0 && sample >= FLAC__stream_decoder_get_total_samples(decoder))
		return false;

	decoder->private_->is_seeking = true;

	decoder->private_->do_md5_checking = false;

	if(decoder->private_->length_callback(decoder, &length, decoder->private_->client_data) != FLAC__STREAM_DECODER_LENGTH_STATUS_OK) {
		decoder->private_->is_seeking = false;
		return false;
	}

	if(
		decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_METADATA ||
		decoder->protected_->state == FLAC__STREAM_DECODER_READ_METADATA
	) {
		if(!FLAC__stream_decoder_process_until_end_of_metadata(decoder)) {

			decoder->private_->is_seeking = false;
			return false;
		}

		if(FLAC__stream_decoder_get_total_samples(decoder) > 0 && sample >= FLAC__stream_decoder_get_total_samples(decoder)) {
			decoder->private_->is_seeking = false;
			return false;
		}
	}

	{
		const FLAC__bool ok =
#if FLAC__HAS_OGG
			decoder->private_->is_ogg?
			seek_to_absolute_sample_ogg_(decoder, length, sample) :
#endif
			seek_to_absolute_sample_(decoder, length, sample)
		;
		decoder->private_->is_seeking = false;
		return ok;
	}
}

uint32_t FLAC__stream_decoder_get_input_bytes_unconsumed(const FLAC__StreamDecoder *decoder)
{
	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));
	FLAC__ASSERT(!(FLAC__bitreader_get_input_bits_unconsumed(decoder->private_->input) & 7));
	return FLAC__bitreader_get_input_bits_unconsumed(decoder->private_->input) / 8;
}

void set_defaults_(FLAC__StreamDecoder *decoder)
{
	decoder->private_->is_ogg = false;
	decoder->private_->read_callback = 0;
	decoder->private_->seek_callback = 0;
	decoder->private_->tell_callback = 0;
	decoder->private_->length_callback = 0;
	decoder->private_->eof_callback = 0;
	decoder->private_->write_callback = 0;
	decoder->private_->metadata_callback = 0;
	decoder->private_->error_callback = 0;
	decoder->private_->client_data = 0;

	memset(decoder->private_->metadata_filter, 0, sizeof(decoder->private_->metadata_filter));
	decoder->private_->metadata_filter[FLAC__METADATA_TYPE_STREAMINFO] = true;
	decoder->private_->metadata_filter_ids_count = 0;

	decoder->protected_->md5_checking = false;

#if FLAC__HAS_OGG
	FLAC__ogg_decoder_aspect_set_defaults(&decoder->protected_->ogg_decoder_aspect);
#endif
}

FILE *get_binary_stdin_(void)
{

#if defined _MSC_VER || defined __MINGW32__
	_setmode(_fileno(stdin), _O_BINARY);
#elif defined __EMX__
	setmode(fileno(stdin), O_BINARY);
#endif

	return stdin;
}

FLAC__bool allocate_output_(FLAC__StreamDecoder *decoder, uint32_t size, uint32_t channels, uint32_t bps)
{
	uint32_t i;
	FLAC__int32 *tmp;

	if(size <= decoder->private_->output_capacity && channels <= decoder->private_->output_channels &&
	   (bps < 32 || decoder->private_->side_subframe != 0))
		return true;

	for(i = 0; i < FLAC__MAX_CHANNELS; i++) {
		if(0 != decoder->private_->output[i]) {
			free(decoder->private_->output[i]-4);
			decoder->private_->output[i] = 0;
		}
		if(0 != decoder->private_->residual_unaligned[i]) {
			free(decoder->private_->residual_unaligned[i]);
			decoder->private_->residual_unaligned[i] = decoder->private_->residual[i] = 0;
		}
	}

	if(0 != decoder->private_->side_subframe) {
		free(decoder->private_->side_subframe);
		decoder->private_->side_subframe = 0;
	}

	for(i = 0; i < channels; i++) {

		tmp = safe_malloc_muladd2_(sizeof(FLAC__int32), size, 4);
		if(tmp == 0) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		memset(tmp, 0, sizeof(FLAC__int32)*4);
		decoder->private_->output[i] = tmp + 4;

		if(!FLAC__memory_alloc_aligned_int32_array(size, &decoder->private_->residual_unaligned[i], &decoder->private_->residual[i])) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
	}

	if(bps == 32) {
		decoder->private_->side_subframe = safe_malloc_mul_2op_p(sizeof(FLAC__int64), size);
		if(decoder->private_->side_subframe == NULL) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
	}

	decoder->private_->output_capacity = size;
	decoder->private_->output_channels = channels;

	return true;
}

FLAC__bool has_id_filtered_(FLAC__StreamDecoder *decoder, FLAC__byte *id)
{
	size_t i;

	FLAC__ASSERT(0 != decoder);
	FLAC__ASSERT(0 != decoder->private_);

	for(i = 0; i < decoder->private_->metadata_filter_ids_count; i++)
		if(0 == memcmp(decoder->private_->metadata_filter_ids + i * (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8), id, (FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8)))
			return true;

	return false;
}

FLAC__bool find_metadata_(FLAC__StreamDecoder *decoder)
{
	FLAC__uint32 x;
	uint32_t i, id;
	FLAC__bool first = true;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	for(i = id = 0; i < 4; ) {
		if(decoder->private_->cached) {
			x = (FLAC__uint32)decoder->private_->lookahead;
			decoder->private_->cached = false;
		}
		else {
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
				return false;
		}
		if(x == FLAC__STREAM_SYNC_STRING[i]) {
			first = true;
			i++;
			id = 0;
			continue;
		}

		if(id >= 3)
			return false;

		if(x == ID3V2_TAG_[id]) {
			id++;
			i = 0;
			if(id == 3) {
				if(!skip_id3v2_tag_(decoder))
					return false;
			}
			continue;
		}
		id = 0;
		if(x == 0xff) {
			decoder->private_->header_warmup[0] = (FLAC__byte)x;
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
				return false;

			if(x == 0xff) {
				decoder->private_->lookahead = (FLAC__byte)x;
				decoder->private_->cached = true;
			}
			else if(x >> 1 == 0x7c) {
				decoder->private_->header_warmup[1] = (FLAC__byte)x;
				decoder->protected_->state = FLAC__STREAM_DECODER_READ_FRAME;
				return true;
			}
		}
		i = 0;
		if(first) {
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
			first = false;
		}
	}

	decoder->protected_->state = FLAC__STREAM_DECODER_READ_METADATA;
	return true;
}

FLAC__bool read_metadata_(FLAC__StreamDecoder *decoder)
{
	FLAC__bool is_last;
	FLAC__uint32 i, x, type, length;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_IS_LAST_LEN))
		return false;
	is_last = x? true : false;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &type, FLAC__STREAM_METADATA_TYPE_LEN))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &length, FLAC__STREAM_METADATA_LENGTH_LEN))
		return false;

	if(type == FLAC__METADATA_TYPE_STREAMINFO) {
		if(!read_metadata_streaminfo_(decoder, is_last, length))
			return false;

		decoder->private_->has_stream_info = true;
		if(0 == memcmp(decoder->private_->stream_info.data.stream_info.md5sum, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16))
			decoder->private_->do_md5_checking = false;
		if(!decoder->private_->is_seeking && decoder->private_->metadata_filter[FLAC__METADATA_TYPE_STREAMINFO] && decoder->private_->metadata_callback)
			decoder->private_->metadata_callback(decoder, &decoder->private_->stream_info, decoder->private_->client_data);
	}
	else if(type == FLAC__METADATA_TYPE_SEEKTABLE) {

		decoder->private_->has_seek_table = false;

		if(length > 0) {
			if(!read_metadata_seektable_(decoder, is_last, length))
				return false;

			decoder->private_->has_seek_table = true;
			if(!decoder->private_->is_seeking && decoder->private_->metadata_filter[FLAC__METADATA_TYPE_SEEKTABLE] && decoder->private_->metadata_callback)
				decoder->private_->metadata_callback(decoder, &decoder->private_->seek_table, decoder->private_->client_data);
		}
	}
	else {
		FLAC__bool skip_it = !decoder->private_->metadata_filter[type];
		uint32_t real_length = length;
		FLAC__StreamMetadata block;

		memset(&block, 0, sizeof(block));
		block.is_last = is_last;
		block.type = (FLAC__MetadataType)type;
		block.length = length;

		if(type == FLAC__METADATA_TYPE_APPLICATION) {
			if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, block.data.application.id, FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8))
				return false;

			if(real_length < FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8) {
				decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
				return false;
			}

			real_length -= FLAC__STREAM_METADATA_APPLICATION_ID_LEN/8;

			if(decoder->private_->metadata_filter_ids_count > 0 && has_id_filtered_(decoder, block.data.application.id))
				skip_it = !skip_it;
		}

		if(skip_it) {
			if(!FLAC__bitreader_skip_byte_block_aligned_no_crc(decoder->private_->input, real_length))
				return false;
		}
		else {
			FLAC__bool ok = true;
			FLAC__bitreader_set_limit(decoder->private_->input, real_length*8);
			switch(type) {
				case FLAC__METADATA_TYPE_PADDING:

					if(!FLAC__bitreader_skip_byte_block_aligned_no_crc(decoder->private_->input, real_length))
						ok = false;
					break;
				case FLAC__METADATA_TYPE_APPLICATION:

					if(real_length > 0) {
						if(0 == (block.data.application.data = malloc(real_length))) {
							decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
							ok = false;
						}
						else if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, block.data.application.data, real_length))
							ok = false;
					}
					else
						block.data.application.data = 0;
					break;
				case FLAC__METADATA_TYPE_VORBIS_COMMENT:
					if(!read_metadata_vorbiscomment_(decoder, &block.data.vorbis_comment, real_length))
						ok = false;
					break;
				case FLAC__METADATA_TYPE_CUESHEET:
					if(!read_metadata_cuesheet_(decoder, &block.data.cue_sheet))
						ok = false;
					break;
				case FLAC__METADATA_TYPE_PICTURE:
					if(!read_metadata_picture_(decoder, &block.data.picture))
						ok = false;
					break;
				case FLAC__METADATA_TYPE_STREAMINFO:
				case FLAC__METADATA_TYPE_SEEKTABLE:
					FLAC__ASSERT(0);
					break;
				default:
					if(real_length > 0) {
						if(0 == (block.data.unknown.data = malloc(real_length))) {
							decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
							ok = false;
						}
						else if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, block.data.unknown.data, real_length))
							ok = false;
					}
					else
						block.data.unknown.data = 0;
					break;
			}
			if(FLAC__bitreader_limit_remaining(decoder->private_->input) > 0) {

				send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_METADATA);
				if(decoder->protected_->state == FLAC__STREAM_DECODER_READ_METADATA)
					decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
				ok = false;
			}
			FLAC__bitreader_remove_limit(decoder->private_->input);
			if(ok && !decoder->private_->is_seeking && decoder->private_->metadata_callback)
				decoder->private_->metadata_callback(decoder, &block, decoder->private_->client_data);

			switch(type) {
				case FLAC__METADATA_TYPE_PADDING:
					break;
				case FLAC__METADATA_TYPE_APPLICATION:
					if(0 != block.data.application.data)
						free(block.data.application.data);
					break;
				case FLAC__METADATA_TYPE_VORBIS_COMMENT:
					if(0 != block.data.vorbis_comment.vendor_string.entry)
						free(block.data.vorbis_comment.vendor_string.entry);
					if(block.data.vorbis_comment.num_comments > 0)
						for(i = 0; i < block.data.vorbis_comment.num_comments; i++)
							if(0 != block.data.vorbis_comment.comments[i].entry)
								free(block.data.vorbis_comment.comments[i].entry);
					if(0 != block.data.vorbis_comment.comments)
						free(block.data.vorbis_comment.comments);
					break;
				case FLAC__METADATA_TYPE_CUESHEET:
					if(block.data.cue_sheet.num_tracks > 0 && 0 != block.data.cue_sheet.tracks)
						for(i = 0; i < block.data.cue_sheet.num_tracks; i++)
							if(0 != block.data.cue_sheet.tracks[i].indices)
								free(block.data.cue_sheet.tracks[i].indices);
					if(0 != block.data.cue_sheet.tracks)
						free(block.data.cue_sheet.tracks);
					break;
				case FLAC__METADATA_TYPE_PICTURE:
					if(0 != block.data.picture.mime_type)
						free(block.data.picture.mime_type);
					if(0 != block.data.picture.description)
						free(block.data.picture.description);
					if(0 != block.data.picture.data)
						free(block.data.picture.data);
					break;
				case FLAC__METADATA_TYPE_STREAMINFO:
				case FLAC__METADATA_TYPE_SEEKTABLE:
					FLAC__ASSERT(0);
				default:
					if(0 != block.data.unknown.data)
						free(block.data.unknown.data);
					break;
			}

			if(!ok)
				return false;
		}
	}

	if(is_last) {

		if(!FLAC__stream_decoder_get_decode_position(decoder, &decoder->private_->first_frame_offset))
			decoder->private_->first_frame_offset = 0;
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
	}

	return true;
}

FLAC__bool read_metadata_streaminfo_(FLAC__StreamDecoder *decoder, FLAC__bool is_last, uint32_t length)
{
	FLAC__uint32 x;
	uint32_t bits, used_bits = 0;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	decoder->private_->stream_info.type = FLAC__METADATA_TYPE_STREAMINFO;
	decoder->private_->stream_info.is_last = is_last;
	decoder->private_->stream_info.length = length;

	bits = FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, bits))
		return false;
	decoder->private_->stream_info.data.stream_info.min_blocksize = x;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN))
		return false;
	decoder->private_->stream_info.data.stream_info.max_blocksize = x;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN))
		return false;
	decoder->private_->stream_info.data.stream_info.min_framesize = x;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN))
		return false;
	decoder->private_->stream_info.data.stream_info.max_framesize = x;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN))
		return false;
	decoder->private_->stream_info.data.stream_info.sample_rate = x;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN))
		return false;
	decoder->private_->stream_info.data.stream_info.channels = x+1;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN;
	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN))
		return false;
	decoder->private_->stream_info.data.stream_info.bits_per_sample = x+1;
	used_bits += bits;

	bits = FLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN;
	if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &decoder->private_->stream_info.data.stream_info.total_samples, FLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN))
		return false;
	used_bits += bits;

	if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, decoder->private_->stream_info.data.stream_info.md5sum, 16))
		return false;
	used_bits += 16*8;

	FLAC__ASSERT(used_bits % 8 == 0);
	if (length < (used_bits / 8))
		return false;
	length -= (used_bits / 8);
	if(!FLAC__bitreader_skip_byte_block_aligned_no_crc(decoder->private_->input, length))
		return false;

	return true;
}

FLAC__bool read_metadata_seektable_(FLAC__StreamDecoder *decoder, FLAC__bool is_last, uint32_t length)
{
	FLAC__uint32 i, x;
	FLAC__uint64 xx;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	decoder->private_->seek_table.type = FLAC__METADATA_TYPE_SEEKTABLE;
	decoder->private_->seek_table.is_last = is_last;
	decoder->private_->seek_table.length = length;

	if(length % FLAC__STREAM_METADATA_SEEKPOINT_LENGTH) {
		FLAC__bitreader_limit_invalidate(decoder->private_->input);
		return false;
	}

	decoder->private_->seek_table.data.seek_table.num_points = length / FLAC__STREAM_METADATA_SEEKPOINT_LENGTH;

	if(0 == (decoder->private_->seek_table.data.seek_table.points = safe_realloc_mul_2op_(decoder->private_->seek_table.data.seek_table.points, decoder->private_->seek_table.data.seek_table.num_points, sizeof(FLAC__StreamMetadata_SeekPoint)))) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	for(i = 0; i < decoder->private_->seek_table.data.seek_table.num_points; i++) {
		if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &xx, FLAC__STREAM_METADATA_SEEKPOINT_SAMPLE_NUMBER_LEN))
			return false;
		decoder->private_->seek_table.data.seek_table.points[i].sample_number = xx;

		if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &xx, FLAC__STREAM_METADATA_SEEKPOINT_STREAM_OFFSET_LEN))
			return false;
		decoder->private_->seek_table.data.seek_table.points[i].stream_offset = xx;

		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_SEEKPOINT_FRAME_SAMPLES_LEN))
			return false;
		decoder->private_->seek_table.data.seek_table.points[i].frame_samples = x;
	}
	length -= (decoder->private_->seek_table.data.seek_table.num_points * FLAC__STREAM_METADATA_SEEKPOINT_LENGTH);

	FLAC__ASSERT(length == 0);

	return true;
}

FLAC__bool read_metadata_vorbiscomment_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_VorbisComment *obj, uint32_t length)
{
	FLAC__uint32 i;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	if (length >= 8) {
		length -= 8;
		FLAC__ASSERT(FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN == 32);
		if (!FLAC__bitreader_read_uint32_little_endian(decoder->private_->input, &obj->vendor_string.length))
			return false;
		if (length < obj->vendor_string.length) {
			obj->vendor_string.length = 0;
			obj->vendor_string.entry = 0;
			goto skip;
		}
		else
			length -= obj->vendor_string.length;
		if (0 == (obj->vendor_string.entry = safe_malloc_add_2op_(obj->vendor_string.length, 1))) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		if (!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, obj->vendor_string.entry, obj->vendor_string.length))
			return false;
		obj->vendor_string.entry[obj->vendor_string.length] = '\0';

		FLAC__ASSERT(FLAC__STREAM_METADATA_VORBIS_COMMENT_NUM_COMMENTS_LEN == 32);
		if (!FLAC__bitreader_read_uint32_little_endian(decoder->private_->input, &obj->num_comments))
			return false;

		if (obj->num_comments > 100000) {

			obj->num_comments = 0;
			return false;
		}
		if (obj->num_comments > 0) {
			if (0 == (obj->comments = safe_malloc_mul_2op_p(obj->num_comments, sizeof(FLAC__StreamMetadata_VorbisComment_Entry)))) {
				obj->num_comments = 0;
				decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
				return false;
			}
			for (i = 0; i < obj->num_comments; i++) {

				obj->comments[i].length = 0;
				obj->comments[i].entry = 0;

				FLAC__ASSERT(FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN == 32);
				if (length < 4) {
					obj->num_comments = i;
					goto skip;
				}
				else
					length -= 4;
				if (!FLAC__bitreader_read_uint32_little_endian(decoder->private_->input, &obj->comments[i].length)) {
					obj->num_comments = i;
					return false;
				}
				if (length < obj->comments[i].length) {
					obj->num_comments = i;
					FLAC__bitreader_limit_invalidate(decoder->private_->input);
					return false;
				}
				else
					length -= obj->comments[i].length;
				if (0 == (obj->comments[i].entry = safe_malloc_add_2op_(obj->comments[i].length, 1))) {
					decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
					obj->num_comments = i;
					return false;
				}
				memset (obj->comments[i].entry, 0, obj->comments[i].length) ;
				if (!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, obj->comments[i].entry, obj->comments[i].length)) {

					free (obj->comments[i].entry) ;
					obj->comments[i].entry = NULL ;
					obj->num_comments = i;
					goto skip;
				}
				obj->comments[i].entry[obj->comments[i].length] = '\0';
			}
		}
	}
	else {
		FLAC__bitreader_limit_invalidate(decoder->private_->input);
		return false;
	}

  skip:
	if (length > 0) {

		if(obj->num_comments < 1) {
			free(obj->comments);
			obj->comments = NULL;
		}
		FLAC__bitreader_limit_invalidate(decoder->private_->input);
		return false;
	}

	return true;
}

FLAC__bool read_metadata_cuesheet_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_CueSheet *obj)
{
	FLAC__uint32 i, j, x;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	memset(obj, 0, sizeof(FLAC__StreamMetadata_CueSheet));

	FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN % 8 == 0);
	if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, (FLAC__byte*)obj->media_catalog_number, FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN/8))
		return false;

	if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &obj->lead_in, FLAC__STREAM_METADATA_CUESHEET_LEAD_IN_LEN))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN))
		return false;
	obj->is_cd = x? true : false;

	if(!FLAC__bitreader_skip_bits_no_crc(decoder->private_->input, FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_NUM_TRACKS_LEN))
		return false;
	obj->num_tracks = x;

	if(obj->num_tracks > 0) {
		if(0 == (obj->tracks = safe_calloc_(obj->num_tracks, sizeof(FLAC__StreamMetadata_CueSheet_Track)))) {
			decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
			return false;
		}
		for(i = 0; i < obj->num_tracks; i++) {
			FLAC__StreamMetadata_CueSheet_Track *track = &obj->tracks[i];
			if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &track->offset, FLAC__STREAM_METADATA_CUESHEET_TRACK_OFFSET_LEN))
				return false;

			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_TRACK_NUMBER_LEN))
				return false;
			track->number = (FLAC__byte)x;

			FLAC__ASSERT(FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN % 8 == 0);
			if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, (FLAC__byte*)track->isrc, FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN/8))
				return false;

			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN))
				return false;
			track->type = x;

			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN))
				return false;
			track->pre_emphasis = x;

			if(!FLAC__bitreader_skip_bits_no_crc(decoder->private_->input, FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN))
				return false;

			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_TRACK_NUM_INDICES_LEN))
				return false;
			track->num_indices = (FLAC__byte)x;

			if(track->num_indices > 0) {
				if(0 == (track->indices = safe_calloc_(track->num_indices, sizeof(FLAC__StreamMetadata_CueSheet_Index)))) {
					decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
					return false;
				}
				for(j = 0; j < track->num_indices; j++) {
					FLAC__StreamMetadata_CueSheet_Index *indx = &track->indices[j];
					if(!FLAC__bitreader_read_raw_uint64(decoder->private_->input, &indx->offset, FLAC__STREAM_METADATA_CUESHEET_INDEX_OFFSET_LEN))
						return false;

					if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_CUESHEET_INDEX_NUMBER_LEN))
						return false;
					indx->number = (FLAC__byte)x;

					if(!FLAC__bitreader_skip_bits_no_crc(decoder->private_->input, FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN))
						return false;
				}
			}
		}
	}
	else {
		FLAC__bitreader_limit_invalidate(decoder->private_->input);
		return false;
	}

	return true;
}

FLAC__bool read_metadata_picture_(FLAC__StreamDecoder *decoder, FLAC__StreamMetadata_Picture *obj)
{
	FLAC__uint32 x;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_PICTURE_TYPE_LEN))
		return false;
	if(x < FLAC__STREAM_METADATA_PICTURE_TYPE_UNDEFINED)
		obj->type = x;
	else
		obj->type = FLAC__STREAM_METADATA_PICTURE_TYPE_OTHER;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN))
		return false;
	if(FLAC__bitreader_limit_remaining(decoder->private_->input) < x){
		FLAC__bitreader_limit_invalidate(decoder->private_->input);
		return false;
	}
	if(0 == (obj->mime_type = safe_malloc_add_2op_(x, 1))) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	if(x > 0) {
		if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, (FLAC__byte*)obj->mime_type, x))
			return false;
	}
	obj->mime_type[x] = '\0';

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN))
		return false;
	if(FLAC__bitreader_limit_remaining(decoder->private_->input) < x){
		FLAC__bitreader_limit_invalidate(decoder->private_->input);
		return false;
	}
	if(0 == (obj->description = safe_malloc_add_2op_(x, 1))) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	if(x > 0) {
		if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, obj->description, x))
			return false;
	}
	obj->description[x] = '\0';

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &obj->width, FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &obj->height, FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &obj->depth, FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &obj->colors, FLAC__STREAM_METADATA_PICTURE_COLORS_LEN))
		return false;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &(obj->data_length), FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN))
		return false;
	if(FLAC__bitreader_limit_remaining(decoder->private_->input) < obj->data_length){
		FLAC__bitreader_limit_invalidate(decoder->private_->input);
		return false;
	}
	if(0 == (obj->data = safe_malloc_(obj->data_length))) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}
	if(obj->data_length > 0) {
		if(!FLAC__bitreader_read_byte_block_aligned_no_crc(decoder->private_->input, obj->data, obj->data_length))
			return false;
	}

	return true;
}

FLAC__bool skip_id3v2_tag_(FLAC__StreamDecoder *decoder)
{
	FLAC__uint32 x;
	uint32_t i, skip;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 24))
		return false;

	skip = 0;
	for(i = 0; i < 4; i++) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
			return false;
		skip <<= 7;
		skip |= (x & 0x7f);
	}

	if(!FLAC__bitreader_skip_byte_block_aligned_no_crc(decoder->private_->input, skip))
		return false;
	return true;
}

FLAC__bool frame_sync_(FLAC__StreamDecoder *decoder)
{
	FLAC__uint32 x;
	FLAC__bool first = true;

	if(!FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input)) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__bitreader_bits_left_for_byte_alignment(decoder->private_->input)))
			return false;
	}

	while(1) {
		if(decoder->private_->cached) {
			x = (FLAC__uint32)decoder->private_->lookahead;
			decoder->private_->cached = false;
		}
		else {
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
				return false;
		}
		if(x == 0xff) {
			decoder->private_->header_warmup[0] = (FLAC__byte)x;
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
				return false;

			if(x == 0xff) {
				decoder->private_->lookahead = (FLAC__byte)x;
				decoder->private_->cached = true;
			}
			else if(x >> 1 == 0x7c) {
				decoder->private_->header_warmup[1] = (FLAC__byte)x;
				decoder->protected_->state = FLAC__STREAM_DECODER_READ_FRAME;

				FLAC__bitreader_set_framesync_location(decoder->private_->input);
				if(!FLAC__stream_decoder_get_decode_position(decoder, &decoder->private_->last_seen_framesync))
					decoder->private_->last_seen_framesync = 0;
				return true;
			}
		}
		if(first) {
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
			first = false;
		}
	}

	return true;
}

FLAC__bool read_frame_(FLAC__StreamDecoder *decoder, FLAC__bool *got_a_frame, FLAC__bool do_full_decode)
{
	uint32_t channel;
	uint32_t i;
	uint32_t frame_crc;
	FLAC__uint32 x;

	*got_a_frame = false;
	decoder->private_->side_subframe_in_use = false;

	frame_crc = 0;
	frame_crc = FLAC__CRC16_UPDATE(decoder->private_->header_warmup[0], frame_crc);
	frame_crc = FLAC__CRC16_UPDATE(decoder->private_->header_warmup[1], frame_crc);
	FLAC__bitreader_reset_read_crc16(decoder->private_->input, (FLAC__uint16)frame_crc);

	if(!read_frame_header_(decoder))
		return false;
	if(decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC)
		return true;
	if(!allocate_output_(decoder, decoder->private_->frame.header.blocksize, decoder->private_->frame.header.channels, decoder->private_->frame.header.bits_per_sample))
		return false;
	for(channel = 0; channel < decoder->private_->frame.header.channels; channel++) {

		uint32_t bps = decoder->private_->frame.header.bits_per_sample;
		switch(decoder->private_->frame.header.channel_assignment) {
			case FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT:

				break;
			case FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE:
				FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
				if(channel == 1)
					bps++;
				break;
			case FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE:
				FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
				if(channel == 0)
					bps++;
				break;
			case FLAC__CHANNEL_ASSIGNMENT_MID_SIDE:
				FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
				if(channel == 1)
					bps++;
				break;
			default:
				FLAC__ASSERT(0);
		}

		if(!read_subframe_(decoder, channel, bps, do_full_decode)){

			if(decoder->protected_->state == FLAC__STREAM_DECODER_END_OF_STREAM)
				break;
			else
				return false;
		}
		if(decoder->protected_->state != FLAC__STREAM_DECODER_READ_FRAME)
			break;
	}

	if(decoder->protected_->state != FLAC__STREAM_DECODER_END_OF_STREAM)
		if(!read_zero_padding_(decoder))
			return false;

	if(decoder->protected_->state == FLAC__STREAM_DECODER_READ_FRAME) {
		frame_crc = FLAC__bitreader_get_read_crc16(decoder->private_->input);
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, FLAC__FRAME_FOOTER_CRC_LEN)) {

			if(decoder->protected_->state != FLAC__STREAM_DECODER_END_OF_STREAM)
				return false;
		}
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
	}
	if(decoder->protected_->state == FLAC__STREAM_DECODER_READ_FRAME && frame_crc == x) {
#endif
		if(do_full_decode) {

			undo_channel_coding(decoder);

			for(channel = 0; channel < decoder->private_->frame.header.channels; channel++) {
				for(i = 0; i < decoder->private_->frame.header.blocksize; i++) {
					int shift_bits = 32 - decoder->private_->frame.header.bits_per_sample;

					if((decoder->private_->output[channel][i] < (INT32_MIN >> shift_bits)) ||
					   (decoder->private_->output[channel][i] > (INT32_MAX >> shift_bits))) {

						send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH);
						decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
						break;
					}
				}
			}
		}
	}
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
	else if (decoder->protected_->state == FLAC__STREAM_DECODER_READ_FRAME) {

		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
	}
#endif

	if(decoder->private_->last_frame_is_set && decoder->protected_->state == FLAC__STREAM_DECODER_READ_FRAME && !decoder->private_->is_seeking && do_full_decode) {
		FLAC__ASSERT(decoder->private_->frame.header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);
		FLAC__ASSERT(decoder->private_->last_frame.header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);
		if(decoder->private_->last_frame.header.number.sample_number + decoder->private_->last_frame.header.blocksize < decoder->private_->frame.header.number.sample_number) {
			uint32_t padding_samples_needed = decoder->private_->frame.header.number.sample_number - (decoder->private_->last_frame.header.number.sample_number + decoder->private_->last_frame.header.blocksize);

			if(decoder->private_->last_frame.header.sample_rate == decoder->private_->frame.header.sample_rate &&
			   decoder->private_->last_frame.header.channels == decoder->private_->frame.header.channels &&
			   decoder->private_->last_frame.header.bits_per_sample == decoder->private_->frame.header.bits_per_sample &&
			   decoder->private_->last_frame.header.blocksize >= 16) {
				FLAC__Frame empty_frame;
				FLAC__int32 * empty_buffer[FLAC__MAX_CHANNELS] = {NULL};
				empty_frame.header = decoder->private_->last_frame.header;
				empty_frame.footer.crc = 0;
				for(i = 0; i < empty_frame.header.channels; i++) {
					empty_buffer[i] = safe_calloc_(empty_frame.header.blocksize, sizeof(FLAC__int32));
					if(empty_buffer[i] == NULL) {
						for(i = 0; i < empty_frame.header.channels; i++)
							if(empty_buffer[i] != NULL)
								free(empty_buffer[i]);
						decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
						return false;
					}
				}

				if(padding_samples_needed > (5*empty_frame.header.sample_rate))
					padding_samples_needed = 5*empty_frame.header.sample_rate;
				if(padding_samples_needed > (50*empty_frame.header.blocksize))
					padding_samples_needed = 50*empty_frame.header.blocksize;
				while(padding_samples_needed){
					empty_frame.header.number.sample_number += empty_frame.header.blocksize;
					if(padding_samples_needed < empty_frame.header.blocksize)
						empty_frame.header.blocksize = padding_samples_needed;
					padding_samples_needed -= empty_frame.header.blocksize;
					decoder->protected_->blocksize = empty_frame.header.blocksize;

					FLAC__ASSERT(empty_frame.header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);
					decoder->private_->samples_decoded = empty_frame.header.number.sample_number + empty_frame.header.blocksize;

					for(channel = 0; channel < empty_frame.header.channels; channel++) {
						empty_frame.subframes[channel].type = FLAC__SUBFRAME_TYPE_CONSTANT;
						empty_frame.subframes[channel].data.constant.value = 0;
						empty_frame.subframes[channel].wasted_bits = 0;
					}

					if(write_audio_frame_to_client_(decoder, &empty_frame, (const FLAC__int32 * const *)empty_buffer) != FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE) {
						decoder->protected_->state = FLAC__STREAM_DECODER_ABORTED;
						for(i = 0; i < empty_frame.header.channels; i++)
							if(empty_buffer[i] != NULL)
								free(empty_buffer[i]);
						return false;
					}
				}
				for(i = 0; i < empty_frame.header.channels; i++)
					if(empty_buffer[i] != NULL)
						free(empty_buffer[i]);

			}
		}
	}

	if(decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC || decoder->protected_->state == FLAC__STREAM_DECODER_END_OF_STREAM) {

		if(!FLAC__bitreader_rewind_to_after_last_seen_framesync(decoder->private_->input)){
#ifndef NDEBUG
			fprintf(stderr, "Rewinding, seeking necessary\n");
#endif
			if(decoder->private_->seek_callback && decoder->private_->last_seen_framesync){

#ifndef NDEBUG
				FLAC__uint64 current_decode_position;
				if(FLAC__stream_decoder_get_decode_position(decoder, &current_decode_position))
					fprintf(stderr, "Bitreader was %" PRIu64 " bytes short\n", current_decode_position-decoder->private_->last_seen_framesync);
#endif
				if(decoder->private_->seek_callback(decoder, decoder->private_->last_seen_framesync, decoder->private_->client_data) == FLAC__STREAM_DECODER_SEEK_STATUS_ERROR) {
					decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
					return false;
				}
				if(!FLAC__bitreader_clear(decoder->private_->input)) {
					decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
					return false;
				}
			}
		}
#ifndef NDEBUG
		else{
			fprintf(stderr, "Rewinding, seeking not necessary\n");
		}
#endif
	}
	else {
		*got_a_frame = true;

		if(decoder->private_->next_fixed_block_size)
			decoder->private_->fixed_block_size = decoder->private_->next_fixed_block_size;

		decoder->protected_->channels = decoder->private_->frame.header.channels;
		decoder->protected_->channel_assignment = decoder->private_->frame.header.channel_assignment;
		decoder->protected_->bits_per_sample = decoder->private_->frame.header.bits_per_sample;
		decoder->protected_->sample_rate = decoder->private_->frame.header.sample_rate;
		decoder->protected_->blocksize = decoder->private_->frame.header.blocksize;

		FLAC__ASSERT(decoder->private_->frame.header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);
		decoder->private_->samples_decoded = decoder->private_->frame.header.number.sample_number + decoder->private_->frame.header.blocksize;

		if(do_full_decode) {
			if(write_audio_frame_to_client_(decoder, &decoder->private_->frame, (const FLAC__int32 * const *)decoder->private_->output) != FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE) {
				decoder->protected_->state = FLAC__STREAM_DECODER_ABORTED;
				return false;
			}
		}
	}

	decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
	return true;
}

FLAC__bool read_frame_header_(FLAC__StreamDecoder *decoder)
{
	FLAC__uint32 x;
	FLAC__uint64 xx;
	uint32_t i, blocksize_hint = 0, sample_rate_hint = 0;
	FLAC__byte crc8, raw_header[16];
	uint32_t raw_header_len;
	FLAC__bool is_unparseable = false;

	FLAC__ASSERT(FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input));

	raw_header[0] = decoder->private_->header_warmup[0];
	raw_header[1] = decoder->private_->header_warmup[1];
	raw_header_len = 2;

	if(raw_header[1] & 0x02)
		is_unparseable = true;

	for(i = 0; i < 2; i++) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
			return false;
		if(x == 0xff) {

			decoder->private_->lookahead = (FLAC__byte)x;
			decoder->private_->cached = true;
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}
		raw_header[raw_header_len++] = (FLAC__byte)x;
	}

	switch(x = raw_header[2] >> 4) {
		case 0:
			is_unparseable = true;
			break;
		case 1:
			decoder->private_->frame.header.blocksize = 192;
			break;
		case 2:
		case 3:
		case 4:
		case 5:
			decoder->private_->frame.header.blocksize = 576 << (x-2);
			break;
		case 6:
		case 7:
			blocksize_hint = x;
			break;
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:
			decoder->private_->frame.header.blocksize = 256 << (x-8);
			break;
		default:
			FLAC__ASSERT(0);
			break;
	}

	switch(x = raw_header[2] & 0x0f) {
		case 0:
			if(decoder->private_->has_stream_info)
				decoder->private_->frame.header.sample_rate = decoder->private_->stream_info.data.stream_info.sample_rate;
			else
				is_unparseable = true;
			break;
		case 1:
			decoder->private_->frame.header.sample_rate = 88200;
			break;
		case 2:
			decoder->private_->frame.header.sample_rate = 176400;
			break;
		case 3:
			decoder->private_->frame.header.sample_rate = 192000;
			break;
		case 4:
			decoder->private_->frame.header.sample_rate = 8000;
			break;
		case 5:
			decoder->private_->frame.header.sample_rate = 16000;
			break;
		case 6:
			decoder->private_->frame.header.sample_rate = 22050;
			break;
		case 7:
			decoder->private_->frame.header.sample_rate = 24000;
			break;
		case 8:
			decoder->private_->frame.header.sample_rate = 32000;
			break;
		case 9:
			decoder->private_->frame.header.sample_rate = 44100;
			break;
		case 10:
			decoder->private_->frame.header.sample_rate = 48000;
			break;
		case 11:
			decoder->private_->frame.header.sample_rate = 96000;
			break;
		case 12:
		case 13:
		case 14:
			sample_rate_hint = x;
			break;
		case 15:
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		default:
			FLAC__ASSERT(0);
	}

	x = (uint32_t)(raw_header[3] >> 4);
	if(x & 8) {
		decoder->private_->frame.header.channels = 2;
		switch(x & 7) {
			case 0:
				decoder->private_->frame.header.channel_assignment = FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE;
				break;
			case 1:
				decoder->private_->frame.header.channel_assignment = FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE;
				break;
			case 2:
				decoder->private_->frame.header.channel_assignment = FLAC__CHANNEL_ASSIGNMENT_MID_SIDE;
				break;
			default:
				is_unparseable = true;
				break;
		}
	}
	else {
		decoder->private_->frame.header.channels = (uint32_t)x + 1;
		decoder->private_->frame.header.channel_assignment = FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT;
	}

	switch(x = (uint32_t)(raw_header[3] & 0x0e) >> 1) {
		case 0:
			if(decoder->private_->has_stream_info)
				decoder->private_->frame.header.bits_per_sample = decoder->private_->stream_info.data.stream_info.bits_per_sample;
			else
				is_unparseable = true;
			break;
		case 1:
			decoder->private_->frame.header.bits_per_sample = 8;
			break;
		case 2:
			decoder->private_->frame.header.bits_per_sample = 12;
			break;
		case 3:
			is_unparseable = true;
			break;
		case 4:
			decoder->private_->frame.header.bits_per_sample = 16;
			break;
		case 5:
			decoder->private_->frame.header.bits_per_sample = 20;
			break;
		case 6:
			decoder->private_->frame.header.bits_per_sample = 24;
			break;
		case 7:
			decoder->private_->frame.header.bits_per_sample = 32;
			break;
		default:
			FLAC__ASSERT(0);
			break;
	}

#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION

	if(raw_header[3] & 0x01)
		is_unparseable = true;
#endif

	if(
		raw_header[1] & 0x01 ||

		(decoder->private_->has_stream_info && decoder->private_->stream_info.data.stream_info.min_blocksize != decoder->private_->stream_info.data.stream_info.max_blocksize)
	) {
		if(!FLAC__bitreader_read_utf8_uint64(decoder->private_->input, &xx, raw_header, &raw_header_len))
			return false;
		if(xx == FLAC__U64L(0xffffffffffffffff)) {
			decoder->private_->lookahead = raw_header[raw_header_len-1];
			decoder->private_->cached = true;
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}
		decoder->private_->frame.header.number_type = FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER;
		decoder->private_->frame.header.number.sample_number = xx;
	}
	else {
		if(!FLAC__bitreader_read_utf8_uint32(decoder->private_->input, &x, raw_header, &raw_header_len))
			return false;
		if(x == 0xffffffff) {
			decoder->private_->lookahead = raw_header[raw_header_len-1];
			decoder->private_->cached = true;
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}
		decoder->private_->frame.header.number_type = FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER;
		decoder->private_->frame.header.number.frame_number = x;
	}

	if(blocksize_hint) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
			return false;
		raw_header[raw_header_len++] = (FLAC__byte)x;
		if(blocksize_hint == 7) {
			FLAC__uint32 _x;
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &_x, 8))
				return false;
			raw_header[raw_header_len++] = (FLAC__byte)_x;
			x = (x << 8) | _x;
		}
		decoder->private_->frame.header.blocksize = x+1;
		if(decoder->private_->frame.header.blocksize > 65535) {
			decoder->private_->lookahead = raw_header[raw_header_len-1];
			decoder->private_->cached = true;
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}

	}

	if(sample_rate_hint) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
			return false;
		raw_header[raw_header_len++] = (FLAC__byte)x;
		if(sample_rate_hint != 12) {
			FLAC__uint32 _x;
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &_x, 8))
				return false;
			raw_header[raw_header_len++] = (FLAC__byte)_x;
			x = (x << 8) | _x;
		}
		if(sample_rate_hint == 12)
			decoder->private_->frame.header.sample_rate = x*1000;
		else if(sample_rate_hint == 13)
			decoder->private_->frame.header.sample_rate = x;
		else
			decoder->private_->frame.header.sample_rate = x*10;
	}

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
		return false;
	crc8 = (FLAC__byte)x;

#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
	if(FLAC__crc8(raw_header, raw_header_len) != crc8) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}
#endif

	decoder->private_->next_fixed_block_size = 0;
	if(decoder->private_->frame.header.number_type == FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER) {
		x = decoder->private_->frame.header.number.frame_number;
		decoder->private_->frame.header.number_type = FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER;
		if(decoder->private_->fixed_block_size)
			decoder->private_->frame.header.number.sample_number = (FLAC__uint64)decoder->private_->fixed_block_size * (FLAC__uint64)x;
		else if(decoder->private_->has_stream_info) {
			if(decoder->private_->stream_info.data.stream_info.min_blocksize == decoder->private_->stream_info.data.stream_info.max_blocksize) {
				decoder->private_->frame.header.number.sample_number = (FLAC__uint64)decoder->private_->stream_info.data.stream_info.min_blocksize * (FLAC__uint64)x;
				decoder->private_->next_fixed_block_size = decoder->private_->stream_info.data.stream_info.max_blocksize;
			}
			else
				is_unparseable = true;
		}
		else if(x == 0) {
			decoder->private_->frame.header.number.sample_number = 0;
			decoder->private_->next_fixed_block_size = decoder->private_->frame.header.blocksize;
		}
		else {

			decoder->private_->frame.header.number.sample_number = (FLAC__uint64)decoder->private_->frame.header.blocksize * (FLAC__uint64)x;
		}
	}

	if(is_unparseable) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}

	return true;
}

FLAC__bool read_subframe_(FLAC__StreamDecoder *decoder, uint32_t channel, uint32_t bps, FLAC__bool do_full_decode)
{
	FLAC__uint32 x;
	FLAC__bool wasted_bits;
	uint32_t i;

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &x, 8))
		return false;

	wasted_bits = (x & 1);
	x &= 0xfe;

	if(wasted_bits) {
		uint32_t u;
		if(!FLAC__bitreader_read_unary_unsigned(decoder->private_->input, &u))
			return false;
		decoder->private_->frame.subframes[channel].wasted_bits = u+1;
		if (decoder->private_->frame.subframes[channel].wasted_bits >= bps) {
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}
		bps -= decoder->private_->frame.subframes[channel].wasted_bits;
	}
	else
		decoder->private_->frame.subframes[channel].wasted_bits = 0;

	if(x & 0x80) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}
	else if(x == 0) {
		if(!read_subframe_constant_(decoder, channel, bps, do_full_decode))
			return false;
	}
	else if(x == 2) {
		if(!read_subframe_verbatim_(decoder, channel, bps, do_full_decode))
			return false;
	}
	else if(x < 16) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}
	else if(x <= 24) {
		uint32_t predictor_order = (x>>1)&7;
		if(decoder->private_->frame.header.blocksize <= predictor_order){
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}
		if(!read_subframe_fixed_(decoder, channel, bps, predictor_order, do_full_decode))
			return false;
		if(decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC)
			return true;
	}
	else if(x < 64) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}
	else {
		uint32_t predictor_order = ((x>>1)&31)+1;
		if(decoder->private_->frame.header.blocksize <= predictor_order){
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
		}
		if(!read_subframe_lpc_(decoder, channel, bps, predictor_order, do_full_decode))
			return false;
		if(decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC)
			return true;
	}

	if(wasted_bits && do_full_decode) {
		x = decoder->private_->frame.subframes[channel].wasted_bits;
		if((bps + x) < 33) {
			for(i = 0; i < decoder->private_->frame.header.blocksize; i++) {
				uint32_t val = decoder->private_->output[channel][i];
				decoder->private_->output[channel][i] = (val << x);
			}
		}
		else {

			FLAC__ASSERT(!decoder->private_->side_subframe_in_use);
			decoder->private_->side_subframe_in_use = true;
			for(i = 0; i < decoder->private_->frame.header.blocksize; i++) {
				uint64_t val = decoder->private_->output[channel][i];
				decoder->private_->side_subframe[i] = (val << x);
			}
		}
	}

	return true;
}

FLAC__bool read_subframe_constant_(FLAC__StreamDecoder *decoder, uint32_t channel, uint32_t bps, FLAC__bool do_full_decode)
{
	FLAC__Subframe_Constant *subframe = &decoder->private_->frame.subframes[channel].data.constant;
	FLAC__int64 x;
	uint32_t i;

	decoder->private_->frame.subframes[channel].type = FLAC__SUBFRAME_TYPE_CONSTANT;

	if(!FLAC__bitreader_read_raw_int64(decoder->private_->input, &x, bps))
		return false;

	subframe->value = x;

	if(do_full_decode) {
		if(bps <= 32) {
			FLAC__int32 *output = decoder->private_->output[channel];
			for(i = 0; i < decoder->private_->frame.header.blocksize; i++)
				output[i] = x;
		} else {
			FLAC__int64 *output = decoder->private_->side_subframe;
			decoder->private_->side_subframe_in_use = true;
			for(i = 0; i < decoder->private_->frame.header.blocksize; i++)
				output[i] = x;
		}
	}

	return true;
}

FLAC__bool read_subframe_fixed_(FLAC__StreamDecoder *decoder, uint32_t channel, uint32_t bps, const uint32_t order, FLAC__bool do_full_decode)
{
	FLAC__Subframe_Fixed *subframe = &decoder->private_->frame.subframes[channel].data.fixed;
	FLAC__int64 i64;
	FLAC__uint32 u32;
	uint32_t u;

	decoder->private_->frame.subframes[channel].type = FLAC__SUBFRAME_TYPE_FIXED;

	subframe->residual = decoder->private_->residual[channel];
	subframe->order = order;

	for(u = 0; u < order; u++) {
		if(!FLAC__bitreader_read_raw_int64(decoder->private_->input, &i64, bps))
			return false;
		subframe->warmup[u] = i64;
	}

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &u32, FLAC__ENTROPY_CODING_METHOD_TYPE_LEN))
		return false;
	subframe->entropy_coding_method.type = (FLAC__EntropyCodingMethodType)u32;
	switch(subframe->entropy_coding_method.type) {
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE:
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2:
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &u32, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN))
				return false;
			if((decoder->private_->frame.header.blocksize >> u32 < order) ||
			   (decoder->private_->frame.header.blocksize % (1 << u32) > 0)) {
				send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
				decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
				return true;
			}
			subframe->entropy_coding_method.data.partitioned_rice.order = u32;
			subframe->entropy_coding_method.data.partitioned_rice.contents = &decoder->private_->partitioned_rice_contents[channel];
			break;
		default:
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
	}

	switch(subframe->entropy_coding_method.type) {
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE:
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2:
			if(!read_residual_partitioned_rice_(decoder, order, subframe->entropy_coding_method.data.partitioned_rice.order, &decoder->private_->partitioned_rice_contents[channel], decoder->private_->residual[channel], subframe->entropy_coding_method.type == FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2))
				return false;
			break;
		default:
			FLAC__ASSERT(0);
	}

	if(do_full_decode) {
		if(bps < 33){
			uint32_t i;
			for(i = 0; i < order; i++)
				decoder->private_->output[channel][i] = subframe->warmup[i];
			if(bps+order <= 32)
				FLAC__fixed_restore_signal(decoder->private_->residual[channel], decoder->private_->frame.header.blocksize-order, order, decoder->private_->output[channel]+order);
			else
				FLAC__fixed_restore_signal_wide(decoder->private_->residual[channel], decoder->private_->frame.header.blocksize-order, order, decoder->private_->output[channel]+order);
		}
		else {
			decoder->private_->side_subframe_in_use = true;
			memcpy(decoder->private_->side_subframe, subframe->warmup, sizeof(FLAC__int64) * order);
			FLAC__fixed_restore_signal_wide_33bit(decoder->private_->residual[channel], decoder->private_->frame.header.blocksize-order, order, decoder->private_->side_subframe+order);
		}
	}

	return true;
}

FLAC__bool read_subframe_lpc_(FLAC__StreamDecoder *decoder, uint32_t channel, uint32_t bps, const uint32_t order, FLAC__bool do_full_decode)
{
	FLAC__Subframe_LPC *subframe = &decoder->private_->frame.subframes[channel].data.lpc;
	FLAC__int32 i32;
	FLAC__int64 i64;
	FLAC__uint32 u32;
	uint32_t u;

	decoder->private_->frame.subframes[channel].type = FLAC__SUBFRAME_TYPE_LPC;

	subframe->residual = decoder->private_->residual[channel];
	subframe->order = order;

	for(u = 0; u < order; u++) {
		if(!FLAC__bitreader_read_raw_int64(decoder->private_->input, &i64, bps))
			return false;
		subframe->warmup[u] = i64;
	}

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &u32, FLAC__SUBFRAME_LPC_QLP_COEFF_PRECISION_LEN))
		return false;
	if(u32 == (1u << FLAC__SUBFRAME_LPC_QLP_COEFF_PRECISION_LEN) - 1) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}
	subframe->qlp_coeff_precision = u32+1;

	if(!FLAC__bitreader_read_raw_int32(decoder->private_->input, &i32, FLAC__SUBFRAME_LPC_QLP_SHIFT_LEN))
		return false;
	if(i32 < 0) {
		send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
		decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		return true;
	}
	subframe->quantization_level = i32;

	for(u = 0; u < order; u++) {
		if(!FLAC__bitreader_read_raw_int32(decoder->private_->input, &i32, subframe->qlp_coeff_precision))
			return false;
		subframe->qlp_coeff[u] = i32;
	}

	if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &u32, FLAC__ENTROPY_CODING_METHOD_TYPE_LEN))
		return false;
	subframe->entropy_coding_method.type = (FLAC__EntropyCodingMethodType)u32;
	switch(subframe->entropy_coding_method.type) {
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE:
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2:
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &u32, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN))
				return false;
			if((decoder->private_->frame.header.blocksize >> u32 < order) ||
			   (decoder->private_->frame.header.blocksize % (1 << u32) > 0)) {
				send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
				decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
				return true;
			}
			subframe->entropy_coding_method.data.partitioned_rice.order = u32;
			subframe->entropy_coding_method.data.partitioned_rice.contents = &decoder->private_->partitioned_rice_contents[channel];
			break;
		default:
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
			return true;
	}

	switch(subframe->entropy_coding_method.type) {
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE:
		case FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2:
			if(!read_residual_partitioned_rice_(decoder, order, subframe->entropy_coding_method.data.partitioned_rice.order, &decoder->private_->partitioned_rice_contents[channel], decoder->private_->residual[channel], subframe->entropy_coding_method.type == FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2))
				return false;
			break;
		default:
			FLAC__ASSERT(0);
	}

	if(do_full_decode) {
		if(bps <= 32) {
			uint32_t i;
			for(i = 0; i < order; i++)
				decoder->private_->output[channel][i] = subframe->warmup[i];
			if(FLAC__lpc_max_residual_bps(bps, subframe->qlp_coeff, order, subframe->quantization_level) <= 32 &&
			   FLAC__lpc_max_prediction_before_shift_bps(bps, subframe->qlp_coeff, order) <= 32)
				FLAC__lpc_restore_signal(decoder->private_->residual[channel], decoder->private_->frame.header.blocksize-order, subframe->qlp_coeff, order, subframe->quantization_level, decoder->private_->output[channel]+order);
			else
				FLAC__lpc_restore_signal_wide(decoder->private_->residual[channel], decoder->private_->frame.header.blocksize-order, subframe->qlp_coeff, order, subframe->quantization_level, decoder->private_->output[channel]+order);
		}
		else {
			decoder->private_->side_subframe_in_use = true;
			memcpy(decoder->private_->side_subframe, subframe->warmup, sizeof(FLAC__int64) * order);
			FLAC__lpc_restore_signal_wide_33bit(decoder->private_->residual[channel], decoder->private_->frame.header.blocksize-order, subframe->qlp_coeff, order, subframe->quantization_level, decoder->private_->side_subframe+order);
		}
	}

	return true;
}

FLAC__bool read_subframe_verbatim_(FLAC__StreamDecoder *decoder, uint32_t channel, uint32_t bps, FLAC__bool do_full_decode)
{
	FLAC__Subframe_Verbatim *subframe = &decoder->private_->frame.subframes[channel].data.verbatim;
	uint32_t i;

	decoder->private_->frame.subframes[channel].type = FLAC__SUBFRAME_TYPE_VERBATIM;

	if(bps < 33) {
		FLAC__int32 x, *residual = decoder->private_->residual[channel];

		subframe->data_type = FLAC__VERBATIM_SUBFRAME_DATA_TYPE_INT32;
		subframe->data.int32 = residual;

		for(i = 0; i < decoder->private_->frame.header.blocksize; i++) {
			if(!FLAC__bitreader_read_raw_int32(decoder->private_->input, &x, bps))
				return false;
			residual[i] = x;
		}

		if(do_full_decode)
			memcpy(decoder->private_->output[channel], subframe->data.int32, sizeof(FLAC__int32) * decoder->private_->frame.header.blocksize);
	}
	else {
		FLAC__int64 x, *side = decoder->private_->side_subframe;

		subframe->data_type = FLAC__VERBATIM_SUBFRAME_DATA_TYPE_INT64;
		subframe->data.int64 = side;
		decoder->private_->side_subframe_in_use = true;

		for(i = 0; i < decoder->private_->frame.header.blocksize; i++) {
			if(!FLAC__bitreader_read_raw_int64(decoder->private_->input, &x, bps))
				return false;
			side[i] = x;
		}
	}

	return true;
}

FLAC__bool read_residual_partitioned_rice_(FLAC__StreamDecoder *decoder, uint32_t predictor_order, uint32_t partition_order, FLAC__EntropyCodingMethod_PartitionedRiceContents *partitioned_rice_contents, FLAC__int32 *residual, FLAC__bool is_extended)
{
	FLAC__uint32 rice_parameter;
	int i;
	uint32_t partition, sample, u;
	const uint32_t partitions = 1u << partition_order;
	const uint32_t partition_samples = decoder->private_->frame.header.blocksize >> partition_order;
	const uint32_t plen = is_extended? FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_PARAMETER_LEN : FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN;
	const uint32_t pesc = is_extended? FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_ESCAPE_PARAMETER : FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER;

	FLAC__ASSERT(partition_order > 0? partition_samples >= predictor_order : decoder->private_->frame.header.blocksize >= predictor_order);

	if(!FLAC__format_entropy_coding_method_partitioned_rice_contents_ensure_size(partitioned_rice_contents, flac_max(6u, partition_order))) {
		decoder->protected_->state = FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR;
		return false;
	}

	sample = 0;
	for(partition = 0; partition < partitions; partition++) {
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &rice_parameter, plen))
			return false;
		partitioned_rice_contents->parameters[partition] = rice_parameter;
		if(rice_parameter < pesc) {
			partitioned_rice_contents->raw_bits[partition] = 0;
			u = (partition == 0) ? partition_samples - predictor_order : partition_samples;
			if(!decoder->private_->local_bitreader_read_rice_signed_block(decoder->private_->input, residual + sample, u, rice_parameter)){
				if(decoder->protected_->state == FLAC__STREAM_DECODER_READ_FRAME) {

					send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
					decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
					return true;
				}
				else
					return false;
			}
			sample += u;
		}
		else {
			if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &rice_parameter, FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_RAW_LEN))
				return false;
			partitioned_rice_contents->raw_bits[partition] = rice_parameter;
			if(rice_parameter == 0) {
				for(u = (partition == 0)? predictor_order : 0; u < partition_samples; u++, sample++)
					residual[sample] = 0;
			}
			else{
				for(u = (partition == 0)? predictor_order : 0; u < partition_samples; u++, sample++) {
					if(!FLAC__bitreader_read_raw_int32(decoder->private_->input, &i, rice_parameter))
						return false;
					residual[sample] = i;
				}
			}
		}
	}

	return true;
}

FLAC__bool read_zero_padding_(FLAC__StreamDecoder *decoder)
{
	if(!FLAC__bitreader_is_consumed_byte_aligned(decoder->private_->input)) {
		FLAC__uint32 zero = 0;
		if(!FLAC__bitreader_read_raw_uint32(decoder->private_->input, &zero, FLAC__bitreader_bits_left_for_byte_alignment(decoder->private_->input)))
			return false;
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
		if(zero != 0) {
			send_error_to_client_(decoder, FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC);
			decoder->protected_->state = FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC;
		}
#endif
	}
	return true;
}

FLAC__bool read_callback_(FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	FLAC__StreamDecoder *decoder = (FLAC__StreamDecoder *)client_data;

	if(
#if FLAC__HAS_OGG

		!decoder->private_->is_ogg &&
#endif
		decoder->private_->eof_callback && decoder->private_->eof_callback(decoder, decoder->private_->client_data)
	) {
		*bytes = 0;
		decoder->protected_->state = FLAC__STREAM_DECODER_END_OF_STREAM;
		return false;
	}
	else if(*bytes > 0) {

		if(decoder->private_->is_seeking && decoder->private_->unparseable_frame_count > 20) {
			decoder->protected_->state = FLAC__STREAM_DECODER_ABORTED;
			return false;
		}
		else {
			const FLAC__StreamDecoderReadStatus status =
#if FLAC__HAS_OGG
				decoder->private_->is_ogg?
				read_callback_ogg_aspect_(decoder, buffer, bytes) :
#endif
				decoder->private_->read_callback(decoder, buffer, bytes, decoder->private_->client_data)
			;
			if(status == FLAC__STREAM_DECODER_READ_STATUS_ABORT) {
				decoder->protected_->state = FLAC__STREAM_DECODER_ABORTED;
				return false;
			}
			else if(*bytes == 0) {
				if(
					status == FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM ||
					(
#if FLAC__HAS_OGG

						!decoder->private_->is_ogg &&
#endif
						decoder->private_->eof_callback && decoder->private_->eof_callback(decoder, decoder->private_->client_data)
					)
				) {
					decoder->protected_->state = FLAC__STREAM_DECODER_END_OF_STREAM;
					return false;
				}
				else
					return true;
			}
			else
				return true;
		}
	}
	else {

		decoder->protected_->state = FLAC__STREAM_DECODER_ABORTED;
		return false;
	}

}

#if defined(FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION) && !defined(FUZZING_BUILD_MODE_FLAC_SANITIZE_SIGNED_INTEGER_OVERFLOW)

__attribute__((no_sanitize("signed-integer-overflow")))
#endif
void undo_channel_coding(FLAC__StreamDecoder *decoder) {
	uint32_t i;
	switch(decoder->private_->frame.header.channel_assignment) {
	case FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT:

		break;
	case FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE:
		FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
		FLAC__ASSERT(decoder->private_->side_subframe_in_use !=  (decoder->private_->frame.header.bits_per_sample < 32));
		for(i = 0; i < decoder->private_->frame.header.blocksize; i++)
			if(decoder->private_->side_subframe_in_use)
				decoder->private_->output[1][i] = decoder->private_->output[0][i] - decoder->private_->side_subframe[i];
			else
				decoder->private_->output[1][i] = decoder->private_->output[0][i] - decoder->private_->output[1][i];
		break;
	case FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE:
		FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
		FLAC__ASSERT(decoder->private_->side_subframe_in_use !=  (decoder->private_->frame.header.bits_per_sample < 32));
		for(i = 0; i < decoder->private_->frame.header.blocksize; i++)
			if(decoder->private_->side_subframe_in_use)
				decoder->private_->output[0][i] = decoder->private_->output[1][i] + decoder->private_->side_subframe[i];
			else
				decoder->private_->output[0][i] += decoder->private_->output[1][i];
		break;
	case FLAC__CHANNEL_ASSIGNMENT_MID_SIDE:
		FLAC__ASSERT(decoder->private_->frame.header.channels == 2);
		FLAC__ASSERT(decoder->private_->side_subframe_in_use !=  (decoder->private_->frame.header.bits_per_sample < 32));
		for(i = 0; i < decoder->private_->frame.header.blocksize; i++) {
			if(!decoder->private_->side_subframe_in_use){
				FLAC__int32 mid, side;
				mid = decoder->private_->output[0][i];
				side = decoder->private_->output[1][i];
				mid = ((uint32_t) mid) << 1;
				mid |= (side & 1);
				decoder->private_->output[0][i] = (mid + side) >> 1;
				decoder->private_->output[1][i] = (mid - side) >> 1;
			}
			else {
				FLAC__int64 mid;
				mid = ((uint64_t)decoder->private_->output[0][i]) << 1;
				mid |= (decoder->private_->side_subframe[i] & 1);
				decoder->private_->output[0][i] = (mid + decoder->private_->side_subframe[i]) >> 1;
				decoder->private_->output[1][i] = (mid - decoder->private_->side_subframe[i]) >> 1;
			}
		}
		break;
	default:
		FLAC__ASSERT(0);
		break;
	}
}

#if FLAC__HAS_OGG
FLAC__StreamDecoderReadStatus read_callback_ogg_aspect_(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes)
{
	switch(FLAC__ogg_decoder_aspect_read_callback_wrapper(&decoder->protected_->ogg_decoder_aspect, buffer, bytes, read_callback_proxy_, decoder, decoder->private_->client_data)) {
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_OK:
			return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;

		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_LOST_SYNC:
			return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_END_OF_STREAM:
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_NOT_FLAC:
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_UNSUPPORTED_MAPPING_VERSION:
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_ABORT:
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_ERROR:
		case FLAC__OGG_DECODER_ASPECT_READ_STATUS_MEMORY_ALLOCATION_ERROR:
			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
		default:
			FLAC__ASSERT(0);

			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	}
}

FLAC__OggDecoderAspectReadStatus read_callback_proxy_(const void *void_decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	FLAC__StreamDecoder *decoder = (FLAC__StreamDecoder*)void_decoder;

	switch(decoder->private_->read_callback(decoder, buffer, bytes, client_data)) {
		case FLAC__STREAM_DECODER_READ_STATUS_CONTINUE:
			return FLAC__OGG_DECODER_ASPECT_READ_STATUS_OK;
		case FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM:
			return FLAC__OGG_DECODER_ASPECT_READ_STATUS_END_OF_STREAM;
		case FLAC__STREAM_DECODER_READ_STATUS_ABORT:
			return FLAC__OGG_DECODER_ASPECT_READ_STATUS_ABORT;
		default:

			FLAC__ASSERT(0);
			return FLAC__OGG_DECODER_ASPECT_READ_STATUS_ABORT;
	}
}
#endif

FLAC__StreamDecoderWriteStatus write_audio_frame_to_client_(FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[])
{
	decoder->private_->last_frame = *frame;
	decoder->private_->last_frame_is_set = true;
	if(decoder->private_->is_seeking) {
		FLAC__uint64 this_frame_sample = frame->header.number.sample_number;
		FLAC__uint64 next_frame_sample = this_frame_sample + (FLAC__uint64)frame->header.blocksize;
		FLAC__uint64 target_sample = decoder->private_->target_sample;

		FLAC__ASSERT(frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);

#if FLAC__HAS_OGG
		decoder->private_->got_a_frame = true;
#endif
		if(this_frame_sample <= target_sample && target_sample < next_frame_sample) {
			uint32_t delta = (uint32_t)(target_sample - this_frame_sample);

			decoder->private_->is_seeking = false;

			if(delta > 0) {
				uint32_t channel;
				const FLAC__int32 *newbuffer[FLAC__MAX_CHANNELS];
				for(channel = 0; channel < frame->header.channels; channel++) {
					newbuffer[channel] = buffer[channel] + delta;
					decoder->private_->last_frame.subframes[channel].type = FLAC__SUBFRAME_TYPE_VERBATIM;
					decoder->private_->last_frame.subframes[channel].data.verbatim.data_type = FLAC__VERBATIM_SUBFRAME_DATA_TYPE_INT32;
					decoder->private_->last_frame.subframes[channel].data.verbatim.data.int32 = newbuffer[channel];
				}
				decoder->private_->last_frame.header.blocksize -= delta;
				decoder->private_->last_frame.header.number.sample_number += (FLAC__uint64)delta;

				return decoder->private_->write_callback(decoder, &decoder->private_->last_frame, newbuffer, decoder->private_->client_data);
			}
			else {

				return decoder->private_->write_callback(decoder, frame, buffer, decoder->private_->client_data);
			}
		}
		else {
			return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
		}
	}
	else {

		if(!decoder->private_->has_stream_info)
			decoder->private_->do_md5_checking = false;
		if(decoder->private_->do_md5_checking) {
			if(!FLAC__MD5Accumulate(&decoder->private_->md5context, buffer, frame->header.channels, frame->header.blocksize, (frame->header.bits_per_sample+7) / 8))
				return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
		}
		return decoder->private_->write_callback(decoder, frame, buffer, decoder->private_->client_data);
	}
}

void send_error_to_client_(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status)
{
	if(!decoder->private_->is_seeking)
		decoder->private_->error_callback(decoder, status, decoder->private_->client_data);
	else if(status == FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM)
		decoder->private_->unparseable_frame_count++;
}

FLAC__bool seek_to_absolute_sample_(FLAC__StreamDecoder *decoder, FLAC__uint64 stream_length, FLAC__uint64 target_sample)
{
	FLAC__uint64 first_frame_offset = decoder->private_->first_frame_offset, lower_bound, upper_bound, lower_bound_sample, upper_bound_sample, this_frame_sample;
	FLAC__int64 pos = -1;
	int i;
	uint32_t approx_bytes_per_frame;
	FLAC__bool first_seek = true, seek_from_lower_bound = false;
	const FLAC__uint64 total_samples = FLAC__stream_decoder_get_total_samples(decoder);
	const uint32_t min_blocksize = decoder->private_->stream_info.data.stream_info.min_blocksize;
	const uint32_t max_blocksize = decoder->private_->stream_info.data.stream_info.max_blocksize;
	const uint32_t max_framesize = decoder->private_->stream_info.data.stream_info.max_framesize;
	const uint32_t min_framesize = decoder->private_->stream_info.data.stream_info.min_framesize;

	uint32_t channels = FLAC__stream_decoder_get_channels(decoder);
	uint32_t bps = FLAC__stream_decoder_get_bits_per_sample(decoder);
	const FLAC__StreamMetadata_SeekTable *seek_table = decoder->private_->has_seek_table? &decoder->private_->seek_table.data.seek_table : 0;

	if(channels == 0)
		channels = decoder->private_->stream_info.data.stream_info.channels;
	if(bps == 0)
		bps = decoder->private_->stream_info.data.stream_info.bits_per_sample;

	if(max_framesize > 0)
		approx_bytes_per_frame = (max_framesize + min_framesize) / 2 + 1;

	else if(min_blocksize == max_blocksize && min_blocksize > 0) {

		approx_bytes_per_frame = min_blocksize * channels * bps/8 + 64;
	}
	else
		approx_bytes_per_frame = 4096 * channels * bps/8 + 64;

	lower_bound = first_frame_offset;
	lower_bound_sample = 0;
	upper_bound = stream_length;
	upper_bound_sample = total_samples > 0 ? total_samples : target_sample ;

	if(decoder->protected_->state == FLAC__STREAM_DECODER_SEARCH_FOR_FRAME_SYNC &&
	   decoder->private_->samples_decoded != 0) {
		if(target_sample < decoder->private_->samples_decoded) {
			if(FLAC__stream_decoder_get_decode_position(decoder, &upper_bound))
				upper_bound_sample = decoder->private_->samples_decoded;
		} else {
			if(FLAC__stream_decoder_get_decode_position(decoder, &lower_bound))
				lower_bound_sample = decoder->private_->samples_decoded;
		}
	}

	if(seek_table) {
		FLAC__uint64 new_lower_bound = lower_bound;
		FLAC__uint64 new_upper_bound = upper_bound;
		FLAC__uint64 new_lower_bound_sample = lower_bound_sample;
		FLAC__uint64 new_upper_bound_sample = upper_bound_sample;

		for(i = (int)seek_table->num_points - 1; i >= 0; i--) {
			if(
				seek_table->points[i].sample_number != FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER &&
				seek_table->points[i].frame_samples > 0 &&
				(total_samples <= 0 || seek_table->points[i].sample_number < total_samples) &&
				seek_table->points[i].sample_number <= target_sample
			)
				break;
		}
		if(i >= 0) {
			new_lower_bound = first_frame_offset + seek_table->points[i].stream_offset;
			new_lower_bound_sample = seek_table->points[i].sample_number;
		}

		for(i = 0; i < (int)seek_table->num_points; i++) {
			if(
				seek_table->points[i].sample_number != FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER &&
				seek_table->points[i].frame_samples > 0 &&
				(total_samples <= 0 || seek_table->points[i].sample_number < total_samples) &&
				seek_table->points[i].sample_number > target_sample
			)
				break;
		}
		if(i < (int)seek_table->num_points) {
			new_upper_bound = first_frame_offset + seek_table->points[i].stream_offset;
			new_upper_bound_sample = seek_table->points[i].sample_number;
		}

		if(new_upper_bound >= new_lower_bound) {
			lower_bound = new_lower_bound;
			upper_bound = new_upper_bound;
			lower_bound_sample = new_lower_bound_sample;
			upper_bound_sample = new_upper_bound_sample;
		}
	}

	FLAC__ASSERT(upper_bound_sample >= lower_bound_sample);

	if(upper_bound_sample == lower_bound_sample)
		upper_bound_sample++;

	decoder->private_->target_sample = target_sample;
	while(1) {

		if(decoder->protected_->state == FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR ||
		   decoder->protected_->state == FLAC__STREAM_DECODER_ABORTED)
			return false;

		if (lower_bound_sample >= upper_bound_sample ||
		    lower_bound > upper_bound ||
		    upper_bound >= INT64_MAX) {
			decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
			return false;
		}
		if(seek_from_lower_bound) {
			pos = lower_bound;
		}
		else {
#ifndef FLAC__INTEGER_ONLY_LIBRARY
			pos = (FLAC__int64)lower_bound + (FLAC__int64)((double)(target_sample - lower_bound_sample) / (double)(upper_bound_sample - lower_bound_sample) * (double)(upper_bound - lower_bound)) - approx_bytes_per_frame;
#else

			if(upper_bound - lower_bound < 0xffffffff)
				pos = (FLAC__int64)lower_bound + (FLAC__int64)(((target_sample - lower_bound_sample) * (upper_bound - lower_bound)) / (upper_bound_sample - lower_bound_sample)) - approx_bytes_per_frame;
			else {
			        FLAC__uint64 ratio = (1<<16) / (upper_bound_sample - lower_bound_sample);
				pos = (FLAC__int64)lower_bound + (FLAC__int64)((((target_sample - lower_bound_sample)>>8) * ((upper_bound - lower_bound)>>8) * ratio)) - approx_bytes_per_frame;
			}
#endif
		}
		if(pos >= (FLAC__int64)upper_bound)
			pos = (FLAC__int64)upper_bound - 1;
		if(pos < (FLAC__int64)lower_bound)
			pos = (FLAC__int64)lower_bound;
		if(decoder->private_->seek_callback(decoder, (FLAC__uint64)pos, decoder->private_->client_data) != FLAC__STREAM_DECODER_SEEK_STATUS_OK) {
			decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
			return false;
		}
		if(!FLAC__stream_decoder_flush(decoder)) {

			return false;
		}

		decoder->private_->unparseable_frame_count = 0;
		if(!FLAC__stream_decoder_process_single(decoder) || decoder->protected_->state == FLAC__STREAM_DECODER_ABORTED || 0 == decoder->private_->samples_decoded) {

			if(decoder->protected_->state != FLAC__STREAM_DECODER_ABORTED && decoder->private_->eof_callback(decoder, decoder->private_->client_data) && !seek_from_lower_bound){

				seek_from_lower_bound = true;
				continue;
			}
			else {
				decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
				return false;
			}
		}
		seek_from_lower_bound = false;

		if(!decoder->private_->is_seeking)
			break;

		FLAC__ASSERT(decoder->private_->last_frame.header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);
		this_frame_sample = decoder->private_->last_frame.header.number.sample_number;

		if(this_frame_sample + decoder->private_->last_frame.header.blocksize >= upper_bound_sample && !first_seek) {
			if (pos == (FLAC__int64)lower_bound) {

				decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
				return false;
			}

			approx_bytes_per_frame = approx_bytes_per_frame? approx_bytes_per_frame * 2 : 16;
			continue;
		}

		first_seek = false;

		if (this_frame_sample < lower_bound_sample) {
			decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
			return false;
		}

		if(target_sample < this_frame_sample) {
			upper_bound_sample = this_frame_sample + decoder->private_->last_frame.header.blocksize;

			if(!FLAC__stream_decoder_get_decode_position(decoder, &upper_bound)) {
				decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
				return false;
			}
			approx_bytes_per_frame = (uint32_t)(2 * (upper_bound - pos) / 3 + 16);
		}
		else {
			lower_bound_sample = this_frame_sample + decoder->private_->last_frame.header.blocksize;
			if(!FLAC__stream_decoder_get_decode_position(decoder, &lower_bound)) {
				decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
				return false;
			}
			approx_bytes_per_frame = (uint32_t)(2 * (lower_bound - pos) / 3 + 16);
		}
	}

	return true;
}

#if FLAC__HAS_OGG
FLAC__bool seek_to_absolute_sample_ogg_(FLAC__StreamDecoder *decoder, FLAC__uint64 stream_length, FLAC__uint64 target_sample)
{
	FLAC__uint64 left_pos = 0, right_pos = stream_length;
	FLAC__uint64 left_sample = 0, right_sample = FLAC__stream_decoder_get_total_samples(decoder);
	FLAC__uint64 this_frame_sample = (FLAC__uint64)0 - 1;
	FLAC__uint64 pos = 0;
	FLAC__bool did_a_seek;
	uint32_t iteration = 0;

	uint32_t BINARY_SEARCH_AFTER_ITERATION = 2;

	static const FLAC__uint64 LINEAR_SEARCH_WITHIN_SAMPLES = FLAC__MAX_BLOCK_SIZE * 2;

	if(right_sample == 0) {
		right_sample = (FLAC__uint64)(-1);
		BINARY_SEARCH_AFTER_ITERATION = 0;
	}

	decoder->private_->target_sample = target_sample;
	for( ; ; iteration++) {

		if(right_pos <= left_pos || right_pos - left_pos < 9) {

			decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
			return false;
		}
		if (iteration == 0 || this_frame_sample > target_sample || target_sample - this_frame_sample > LINEAR_SEARCH_WITHIN_SAMPLES) {
			if (iteration >= BINARY_SEARCH_AFTER_ITERATION) {
				pos = (right_pos + left_pos) / 2;
			}
			else {
#ifndef FLAC__INTEGER_ONLY_LIBRARY
				pos = (FLAC__uint64)((double)(target_sample - left_sample) / (double)(right_sample - left_sample) * (double)(right_pos - left_pos));
#else

				if ((target_sample-left_sample <= 0xffffffff) && (right_pos-left_pos <= 0xffffffff))
					pos = (FLAC__int64)(((target_sample-left_sample) * (right_pos-left_pos)) / (right_sample-left_sample));
				else
					pos = (FLAC__int64)((((target_sample-left_sample)>>8) * ((right_pos-left_pos)>>8)) / ((right_sample-left_sample)>>16));
#endif

			}

			if(decoder->private_->seek_callback((FLAC__StreamDecoder*)decoder, (FLAC__uint64)pos, decoder->private_->client_data) != FLAC__STREAM_DECODER_SEEK_STATUS_OK) {
				decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
				return false;
			}
			if(!FLAC__stream_decoder_flush(decoder)) {

				return false;
			}
			did_a_seek = true;
		}
		else
			did_a_seek = false;

		decoder->private_->got_a_frame = false;
		if(!FLAC__stream_decoder_process_single(decoder) ||
		   decoder->protected_->state == FLAC__STREAM_DECODER_ABORTED) {
			decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
			return false;
		}
		if(!decoder->private_->got_a_frame) {
			if(did_a_seek) {

				right_pos = pos;
				BINARY_SEARCH_AFTER_ITERATION = 0;
			}
			else {

				decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
				return false;
			}
		}

		else if(!decoder->private_->is_seeking) {
			break;
		}
		else {
			this_frame_sample = decoder->private_->last_frame.header.number.sample_number;
			FLAC__ASSERT(decoder->private_->last_frame.header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER);

			if (did_a_seek) {
				if (this_frame_sample <= target_sample) {

					FLAC__ASSERT(this_frame_sample != target_sample);

					left_sample = this_frame_sample;

					if (left_pos == pos) {
						decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
						return false;
					}
					left_pos = pos;
				}
				else {
					right_sample = this_frame_sample;

					if (right_pos == pos) {
						decoder->protected_->state = FLAC__STREAM_DECODER_SEEK_ERROR;
						return false;
					}
					right_pos = pos;
				}
			}
		}
	}

	return true;
}
#endif

FLAC__StreamDecoderReadStatus file_read_callback_(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	(void)client_data;

	if(*bytes > 0) {
		*bytes = fread(buffer, sizeof(FLAC__byte), *bytes, decoder->private_->file);
		if(ferror(decoder->private_->file))
			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
		else if(*bytes == 0)
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		else
			return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
	}
	else
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
}

FLAC__StreamDecoderSeekStatus file_seek_callback_(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data)
{
	(void)client_data;

	if(decoder->private_->file == stdin)
		return FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED;
	else if(fseeko(decoder->private_->file, (FLAC__off_t)absolute_byte_offset, SEEK_SET) < 0)
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
	else
		return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

FLAC__StreamDecoderTellStatus file_tell_callback_(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
{
	FLAC__off_t pos;
	(void)client_data;

	if(decoder->private_->file == stdin)
		return FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED;
	else if((pos = ftello(decoder->private_->file)) < 0)
		return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
	else {
		*absolute_byte_offset = (FLAC__uint64)pos;
		return FLAC__STREAM_DECODER_TELL_STATUS_OK;
	}
}

FLAC__StreamDecoderLengthStatus file_length_callback_(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data)
{
	struct flac_stat_s filestats;
	(void)client_data;

	if(decoder->private_->file == stdin)
		return FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED;

#ifndef FLAC__USE_FILELENGTHI64
	if(flac_fstat(fileno(decoder->private_->file), &filestats) != 0)
#else
	filestats.st_size = _filelengthi64(fileno(decoder->private_->file));
	if(filestats.st_size < 0)
#endif
		return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
	else {
		*stream_length = (FLAC__uint64)filestats.st_size;
		return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
	}
}

FLAC__bool file_eof_callback_(const FLAC__StreamDecoder *decoder, void *client_data)
{
	(void)client_data;

	return feof(decoder->private_->file)? true : false;
}
