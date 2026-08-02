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

#ifndef FLAC__FORMAT_H
#define FLAC__FORMAT_H

#include "export.h"
#include "ordinals.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLAC__MAX_METADATA_TYPE_CODE (126u)

#define FLAC__MIN_BLOCK_SIZE (16u)

#define FLAC__MAX_BLOCK_SIZE (65535u)

#define FLAC__SUBSET_MAX_BLOCK_SIZE_48000HZ (4608u)

#define FLAC__MAX_CHANNELS (8u)

#define FLAC__MIN_BITS_PER_SAMPLE (4u)

#define FLAC__MAX_BITS_PER_SAMPLE (32u)

#define FLAC__REFERENCE_CODEC_MAX_BITS_PER_SAMPLE (32u)

#define FLAC__MAX_SAMPLE_RATE (1048575u)

#define FLAC__MAX_LPC_ORDER (32u)

#define FLAC__SUBSET_MAX_LPC_ORDER_48000HZ (12u)

#define FLAC__MIN_QLP_COEFF_PRECISION (5u)

#define FLAC__MAX_QLP_COEFF_PRECISION (15u)

#define FLAC__MAX_FIXED_ORDER (4u)

#define FLAC__MAX_RICE_PARTITION_ORDER (15u)

#define FLAC__SUBSET_MAX_RICE_PARTITION_ORDER (8u)

extern FLAC_API const char *FLAC__VERSION_STRING;

extern FLAC_API const char *FLAC__VENDOR_STRING;

extern FLAC_API const FLAC__byte FLAC__STREAM_SYNC_STRING[4];

extern FLAC_API const uint32_t FLAC__STREAM_SYNC;

extern FLAC_API const uint32_t FLAC__STREAM_SYNC_LEN;

#define FLAC__STREAM_SYNC_LENGTH (4u)

typedef enum {
	FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE = 0,

	FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2 = 1

} FLAC__EntropyCodingMethodType;

extern FLAC_API const char * const FLAC__EntropyCodingMethodTypeString[];

typedef struct {

	uint32_t *parameters;

	uint32_t *raw_bits;

	uint32_t capacity_by_order;

} FLAC__EntropyCodingMethod_PartitionedRiceContents;

typedef struct {

	uint32_t order;

	const FLAC__EntropyCodingMethod_PartitionedRiceContents *contents;

} FLAC__EntropyCodingMethod_PartitionedRice;

extern FLAC_API const uint32_t FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ORDER_LEN;
extern FLAC_API const uint32_t FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_PARAMETER_LEN;
extern FLAC_API const uint32_t FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_PARAMETER_LEN;
extern FLAC_API const uint32_t FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_RAW_LEN;

extern FLAC_API const uint32_t FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE_ESCAPE_PARAMETER;

extern FLAC_API const uint32_t FLAC__ENTROPY_CODING_METHOD_PARTITIONED_RICE2_ESCAPE_PARAMETER;

typedef struct {
	FLAC__EntropyCodingMethodType type;
	union {
		FLAC__EntropyCodingMethod_PartitionedRice partitioned_rice;
	} data;
} FLAC__EntropyCodingMethod;

extern FLAC_API const uint32_t FLAC__ENTROPY_CODING_METHOD_TYPE_LEN;

typedef enum {
	FLAC__SUBFRAME_TYPE_CONSTANT = 0,
	FLAC__SUBFRAME_TYPE_VERBATIM = 1,
	FLAC__SUBFRAME_TYPE_FIXED = 2,
	FLAC__SUBFRAME_TYPE_LPC = 3
} FLAC__SubframeType;

extern FLAC_API const char * const FLAC__SubframeTypeString[];

typedef struct {
	FLAC__int64 value;
} FLAC__Subframe_Constant;

typedef enum {
	FLAC__VERBATIM_SUBFRAME_DATA_TYPE_INT32,
	FLAC__VERBATIM_SUBFRAME_DATA_TYPE_INT64
} FLAC__VerbatimSubframeDataType;

typedef struct {
	union {
		const FLAC__int32 *int32;
		const FLAC__int64 *int64;
	} data;
	FLAC__VerbatimSubframeDataType data_type;
} FLAC__Subframe_Verbatim;

typedef struct {
	FLAC__EntropyCodingMethod entropy_coding_method;

	uint32_t order;

	FLAC__int64 warmup[FLAC__MAX_FIXED_ORDER];

	const FLAC__int32 *residual;

} FLAC__Subframe_Fixed;

typedef struct {
	FLAC__EntropyCodingMethod entropy_coding_method;

	uint32_t order;

	uint32_t qlp_coeff_precision;

	int quantization_level;

	FLAC__int32 qlp_coeff[FLAC__MAX_LPC_ORDER];

	FLAC__int64 warmup[FLAC__MAX_LPC_ORDER];

	const FLAC__int32 *residual;

} FLAC__Subframe_LPC;

extern FLAC_API const uint32_t FLAC__SUBFRAME_LPC_QLP_COEFF_PRECISION_LEN;
extern FLAC_API const uint32_t FLAC__SUBFRAME_LPC_QLP_SHIFT_LEN;

typedef struct {
	FLAC__SubframeType type;
	union {
		FLAC__Subframe_Constant constant;
		FLAC__Subframe_Fixed fixed;
		FLAC__Subframe_LPC lpc;
		FLAC__Subframe_Verbatim verbatim;
	} data;
	uint32_t wasted_bits;
} FLAC__Subframe;

extern FLAC_API const uint32_t FLAC__SUBFRAME_ZERO_PAD_LEN;
extern FLAC_API const uint32_t FLAC__SUBFRAME_TYPE_LEN;
extern FLAC_API const uint32_t FLAC__SUBFRAME_WASTED_BITS_FLAG_LEN;

extern FLAC_API const uint32_t FLAC__SUBFRAME_TYPE_CONSTANT_BYTE_ALIGNED_MASK;
extern FLAC_API const uint32_t FLAC__SUBFRAME_TYPE_VERBATIM_BYTE_ALIGNED_MASK;
extern FLAC_API const uint32_t FLAC__SUBFRAME_TYPE_FIXED_BYTE_ALIGNED_MASK;
extern FLAC_API const uint32_t FLAC__SUBFRAME_TYPE_LPC_BYTE_ALIGNED_MASK;

typedef enum {
	FLAC__CHANNEL_ASSIGNMENT_INDEPENDENT = 0,
	FLAC__CHANNEL_ASSIGNMENT_LEFT_SIDE = 1,
	FLAC__CHANNEL_ASSIGNMENT_RIGHT_SIDE = 2,
	FLAC__CHANNEL_ASSIGNMENT_MID_SIDE = 3
} FLAC__ChannelAssignment;

extern FLAC_API const char * const FLAC__ChannelAssignmentString[];

typedef enum {
	FLAC__FRAME_NUMBER_TYPE_FRAME_NUMBER,
	FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER
} FLAC__FrameNumberType;

extern FLAC_API const char * const FLAC__FrameNumberTypeString[];

typedef struct {
	uint32_t blocksize;

	uint32_t sample_rate;

	uint32_t channels;

	FLAC__ChannelAssignment channel_assignment;

	uint32_t bits_per_sample;

	FLAC__FrameNumberType number_type;

	union {
		FLAC__uint32 frame_number;
		FLAC__uint64 sample_number;
	} number;

	FLAC__uint8 crc;

} FLAC__FrameHeader;

extern FLAC_API const uint32_t FLAC__FRAME_HEADER_SYNC;
extern FLAC_API const uint32_t FLAC__FRAME_HEADER_SYNC_LEN;
extern FLAC_API const uint32_t FLAC__FRAME_HEADER_RESERVED_LEN;
extern FLAC_API const uint32_t FLAC__FRAME_HEADER_BLOCKING_STRATEGY_LEN;
extern FLAC_API const uint32_t FLAC__FRAME_HEADER_BLOCK_SIZE_LEN;
extern FLAC_API const uint32_t FLAC__FRAME_HEADER_SAMPLE_RATE_LEN;
extern FLAC_API const uint32_t FLAC__FRAME_HEADER_CHANNEL_ASSIGNMENT_LEN;
extern FLAC_API const uint32_t FLAC__FRAME_HEADER_BITS_PER_SAMPLE_LEN;
extern FLAC_API const uint32_t FLAC__FRAME_HEADER_ZERO_PAD_LEN;
extern FLAC_API const uint32_t FLAC__FRAME_HEADER_CRC_LEN;

typedef struct {
	FLAC__uint16 crc;

} FLAC__FrameFooter;

extern FLAC_API const uint32_t FLAC__FRAME_FOOTER_CRC_LEN;

typedef struct {
	FLAC__FrameHeader header;
	FLAC__Subframe subframes[FLAC__MAX_CHANNELS];
	FLAC__FrameFooter footer;
} FLAC__Frame;

typedef enum {

	FLAC__METADATA_TYPE_STREAMINFO = 0,

	FLAC__METADATA_TYPE_PADDING = 1,

	FLAC__METADATA_TYPE_APPLICATION = 2,

	FLAC__METADATA_TYPE_SEEKTABLE = 3,

	FLAC__METADATA_TYPE_VORBIS_COMMENT = 4,

	FLAC__METADATA_TYPE_CUESHEET = 5,

	FLAC__METADATA_TYPE_PICTURE = 6,

	FLAC__METADATA_TYPE_UNDEFINED = 7,

	FLAC__MAX_METADATA_TYPE = FLAC__MAX_METADATA_TYPE_CODE,

} FLAC__MetadataType;

extern FLAC_API const char * const FLAC__MetadataTypeString[];

typedef struct {
	uint32_t min_blocksize, max_blocksize;
	uint32_t min_framesize, max_framesize;
	uint32_t sample_rate;
	uint32_t channels;
	uint32_t bits_per_sample;
	FLAC__uint64 total_samples;
	FLAC__byte md5sum[16];
} FLAC__StreamMetadata_StreamInfo;

extern FLAC_API const uint32_t FLAC__STREAM_METADATA_STREAMINFO_MIN_BLOCK_SIZE_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_STREAMINFO_MAX_BLOCK_SIZE_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_STREAMINFO_MIN_FRAME_SIZE_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_STREAMINFO_MAX_FRAME_SIZE_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_STREAMINFO_SAMPLE_RATE_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_STREAMINFO_CHANNELS_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_STREAMINFO_BITS_PER_SAMPLE_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_STREAMINFO_TOTAL_SAMPLES_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_STREAMINFO_MD5SUM_LEN;

#define FLAC__STREAM_METADATA_STREAMINFO_LENGTH (34u)

typedef struct {
	int dummy;

} FLAC__StreamMetadata_Padding;

typedef struct {
	FLAC__byte id[4];
	FLAC__byte *data;
} FLAC__StreamMetadata_Application;

extern FLAC_API const uint32_t FLAC__STREAM_METADATA_APPLICATION_ID_LEN;

typedef struct {
	FLAC__uint64 sample_number;

	FLAC__uint64 stream_offset;

	uint32_t frame_samples;

} FLAC__StreamMetadata_SeekPoint;

extern FLAC_API const uint32_t FLAC__STREAM_METADATA_SEEKPOINT_SAMPLE_NUMBER_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_SEEKPOINT_STREAM_OFFSET_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_SEEKPOINT_FRAME_SAMPLES_LEN;

#define FLAC__STREAM_METADATA_SEEKPOINT_LENGTH (18u)

extern FLAC_API const FLAC__uint64 FLAC__STREAM_METADATA_SEEKPOINT_PLACEHOLDER;

typedef struct {
	uint32_t num_points;
	FLAC__StreamMetadata_SeekPoint *points;
} FLAC__StreamMetadata_SeekTable;

typedef struct {
	FLAC__uint32 length;
	FLAC__byte *entry;
} FLAC__StreamMetadata_VorbisComment_Entry;

extern FLAC_API const uint32_t FLAC__STREAM_METADATA_VORBIS_COMMENT_ENTRY_LENGTH_LEN;

typedef struct {
	FLAC__StreamMetadata_VorbisComment_Entry vendor_string;
	FLAC__uint32 num_comments;
	FLAC__StreamMetadata_VorbisComment_Entry *comments;
} FLAC__StreamMetadata_VorbisComment;

extern FLAC_API const uint32_t FLAC__STREAM_METADATA_VORBIS_COMMENT_NUM_COMMENTS_LEN;

typedef struct {
	FLAC__uint64 offset;

	FLAC__byte number;

} FLAC__StreamMetadata_CueSheet_Index;

extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_INDEX_OFFSET_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_INDEX_NUMBER_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_INDEX_RESERVED_LEN;

typedef struct {
	FLAC__uint64 offset;

	FLAC__byte number;

	char isrc[13];

	uint32_t type:1;

	uint32_t pre_emphasis:1;

	FLAC__byte num_indices;

	FLAC__StreamMetadata_CueSheet_Index *indices;

} FLAC__StreamMetadata_CueSheet_Track;

extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_TRACK_OFFSET_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_TRACK_NUMBER_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_TRACK_ISRC_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_TRACK_TYPE_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_TRACK_PRE_EMPHASIS_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_TRACK_RESERVED_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_TRACK_NUM_INDICES_LEN;

typedef struct {
	char media_catalog_number[129];

	FLAC__uint64 lead_in;

	FLAC__bool is_cd;

	uint32_t num_tracks;

	FLAC__StreamMetadata_CueSheet_Track *tracks;

} FLAC__StreamMetadata_CueSheet;

extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_MEDIA_CATALOG_NUMBER_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_LEAD_IN_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_IS_CD_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_RESERVED_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_CUESHEET_NUM_TRACKS_LEN;

typedef enum {
	FLAC__STREAM_METADATA_PICTURE_TYPE_OTHER = 0,
	FLAC__STREAM_METADATA_PICTURE_TYPE_FILE_ICON_STANDARD = 1,
	FLAC__STREAM_METADATA_PICTURE_TYPE_FILE_ICON = 2,
	FLAC__STREAM_METADATA_PICTURE_TYPE_FRONT_COVER = 3,
	FLAC__STREAM_METADATA_PICTURE_TYPE_BACK_COVER = 4,
	FLAC__STREAM_METADATA_PICTURE_TYPE_LEAFLET_PAGE = 5,
	FLAC__STREAM_METADATA_PICTURE_TYPE_MEDIA = 6,
	FLAC__STREAM_METADATA_PICTURE_TYPE_LEAD_ARTIST = 7,
	FLAC__STREAM_METADATA_PICTURE_TYPE_ARTIST = 8,
	FLAC__STREAM_METADATA_PICTURE_TYPE_CONDUCTOR = 9,
	FLAC__STREAM_METADATA_PICTURE_TYPE_BAND = 10,
	FLAC__STREAM_METADATA_PICTURE_TYPE_COMPOSER = 11,
	FLAC__STREAM_METADATA_PICTURE_TYPE_LYRICIST = 12,
	FLAC__STREAM_METADATA_PICTURE_TYPE_RECORDING_LOCATION = 13,
	FLAC__STREAM_METADATA_PICTURE_TYPE_DURING_RECORDING = 14,
	FLAC__STREAM_METADATA_PICTURE_TYPE_DURING_PERFORMANCE = 15,
	FLAC__STREAM_METADATA_PICTURE_TYPE_VIDEO_SCREEN_CAPTURE = 16,
	FLAC__STREAM_METADATA_PICTURE_TYPE_FISH = 17,
	FLAC__STREAM_METADATA_PICTURE_TYPE_ILLUSTRATION = 18,
	FLAC__STREAM_METADATA_PICTURE_TYPE_BAND_LOGOTYPE = 19,
	FLAC__STREAM_METADATA_PICTURE_TYPE_PUBLISHER_LOGOTYPE = 20,
	FLAC__STREAM_METADATA_PICTURE_TYPE_UNDEFINED
} FLAC__StreamMetadata_Picture_Type;

extern FLAC_API const char * const FLAC__StreamMetadata_Picture_TypeString[];

typedef struct {
	FLAC__StreamMetadata_Picture_Type type;

	char *mime_type;

	FLAC__byte *description;

	FLAC__uint32 width;

	FLAC__uint32 height;

	FLAC__uint32 depth;

	FLAC__uint32 colors;

	FLAC__uint32 data_length;

	FLAC__byte *data;

} FLAC__StreamMetadata_Picture;

extern FLAC_API const uint32_t FLAC__STREAM_METADATA_PICTURE_TYPE_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_PICTURE_MIME_TYPE_LENGTH_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_PICTURE_DESCRIPTION_LENGTH_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_PICTURE_WIDTH_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_PICTURE_HEIGHT_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_PICTURE_DEPTH_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_PICTURE_COLORS_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_PICTURE_DATA_LENGTH_LEN;

typedef struct {
	FLAC__byte *data;
} FLAC__StreamMetadata_Unknown;

typedef struct FLAC__StreamMetadata {
	FLAC__MetadataType type;

	FLAC__bool is_last;

	uint32_t length;

	union {
		FLAC__StreamMetadata_StreamInfo stream_info;
		FLAC__StreamMetadata_Padding padding;
		FLAC__StreamMetadata_Application application;
		FLAC__StreamMetadata_SeekTable seek_table;
		FLAC__StreamMetadata_VorbisComment vorbis_comment;
		FLAC__StreamMetadata_CueSheet cue_sheet;
		FLAC__StreamMetadata_Picture picture;
		FLAC__StreamMetadata_Unknown unknown;
	} data;

} FLAC__StreamMetadata;

extern FLAC_API const uint32_t FLAC__STREAM_METADATA_IS_LAST_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_TYPE_LEN;
extern FLAC_API const uint32_t FLAC__STREAM_METADATA_LENGTH_LEN;

#define FLAC__STREAM_METADATA_HEADER_LENGTH (4u)

FLAC_API FLAC__bool FLAC__format_sample_rate_is_valid(uint32_t sample_rate);

FLAC_API FLAC__bool FLAC__format_blocksize_is_subset(uint32_t blocksize, uint32_t sample_rate);

FLAC_API FLAC__bool FLAC__format_sample_rate_is_subset(uint32_t sample_rate);

FLAC_API FLAC__bool FLAC__format_vorbiscomment_entry_name_is_legal(const char *name);

FLAC_API FLAC__bool FLAC__format_vorbiscomment_entry_value_is_legal(const FLAC__byte *value, uint32_t length);

FLAC_API FLAC__bool FLAC__format_vorbiscomment_entry_is_legal(const FLAC__byte *entry, uint32_t length);

FLAC_API FLAC__bool FLAC__format_seektable_is_legal(const FLAC__StreamMetadata_SeekTable *seek_table);

FLAC_API uint32_t FLAC__format_seektable_sort(FLAC__StreamMetadata_SeekTable *seek_table);

FLAC_API FLAC__bool FLAC__format_cuesheet_is_legal(const FLAC__StreamMetadata_CueSheet *cue_sheet, FLAC__bool check_cd_da_subset, const char **violation);

FLAC_API FLAC__bool FLAC__format_picture_is_legal(const FLAC__StreamMetadata_Picture *picture, const char **violation);

#ifdef __cplusplus
}
#endif

#endif
