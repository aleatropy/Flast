/* libFLAC - Free Lossless Audio Codec library
 * Copyright (C) 2012-2023  Xiph.Org Foundation
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

#ifndef FLAC__SHARE__COMPAT_H
#define FLAC__SHARE__COMPAT_H

#include <stddef.h>
#include <stdarg.h>

#if defined _WIN32 && !defined __CYGWIN__

# include <io.h>
#else
# include <unistd.h>
#endif

#if defined _MSC_VER || defined __BORLANDC__ || defined __MINGW32__
#include <sys/types.h>
#define FLAC__off_t __int64
#define FLAC__OFF_T_MAX INT64_MAX
#if !defined __MINGW32__
#define fseeko _fseeki64
#define ftello _ftelli64
#else
#if !defined(HAVE_FSEEKO)
#define fseeko fseeko64
#define ftello ftello64
#endif
#endif
#else
#define FLAC__off_t off_t
#define FLAC__OFF_T_MAX OFF_T_MAX
#endif

#ifdef HAVE_INTTYPES_H
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif

#if defined(_MSC_VER)
#define strtoll _strtoi64
#define strtoull _strtoui64
#endif

#if defined(_MSC_VER) && !defined(__cplusplus)
#define inline __inline
#endif

#if defined __INTEL_COMPILER || (defined _MSC_VER && defined _WIN64)

#define flac_restrict __restrict
#elif defined __GNUC__
#define flac_restrict __restrict__
#else
#define flac_restrict
#endif

#define FLAC__U64L(x) x##ULL

#if defined _MSC_VER || defined __MINGW32__
#define FLAC__STRCASECMP _stricmp
#define FLAC__STRNCASECMP _strnicmp
#elif defined __BORLANDC__
#define FLAC__STRCASECMP stricmp
#define FLAC__STRNCASECMP strnicmp
#else
#define FLAC__STRCASECMP strcasecmp
#define FLAC__STRNCASECMP strncasecmp
#endif

#if defined _MSC_VER || defined __MINGW32__ || defined __EMX__
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

#if defined _MSC_VER || defined __BORLANDC__ || defined __MINGW32__
#if defined __BORLANDC__
#include <utime.h>
#else
#include <sys/utime.h>
#endif
#else
#if defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE >= 200809L)
#include <fcntl.h>
#else
#include <sys/types.h>
#include <utime.h>
#endif
#endif

#if defined _MSC_VER
#  if _MSC_VER >= 1800
#    include <inttypes.h>
#  elif _MSC_VER >= 1600

#    include <stdint.h>
#    define PRIu64 "llu"
#    define PRId64 "lld"
#    define PRIx64 "llx"
#  else
#    include <limits.h>
#    ifndef UINT32_MAX
#      define UINT32_MAX _UI32_MAX
#    endif
#    define PRIu64 "I64u"
#    define PRId64 "I64d"
#    define PRIx64 "I64x"
#  endif
#  if defined(_USING_V110_SDK71_) && !defined(_DLL)
#    pragma message("WARNING: This compile will NOT FUNCTION PROPERLY on Windows XP. See comments in include/share/compat.h for details")
#define FLAC__USE_FILELENGTHI64

#  endif
#endif

#ifdef _WIN32

#include "share/win_utf8_io.h"
#define flac_printf printf_utf8
#define flac_fprintf fprintf_utf8
#define flac_vfprintf vfprintf_utf8
#define flac_fopen fopen_utf8
#define flac_chmod chmod_utf8
#define flac_utime utime_utf8
#define flac_unlink unlink_utf8
#define flac_rename rename_utf8
#define flac_stat stat64_utf8

#else

#define flac_printf printf
#define flac_fprintf fprintf
#define flac_vfprintf vfprintf

#define flac_fopen fopen
#define flac_chmod chmod
#define flac_unlink unlink
#define flac_rename rename
#define flac_stat stat

#if defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE >= 200809L)
#define flac_utime(a, b) utimensat (AT_FDCWD, a, *b, 0)
#else
#define flac_utime utime
#endif
#endif

#ifdef _WIN32
#define flac_stat_s __stat64
#define flac_fstat _fstat64
#else
#define flac_stat_s stat
#define flac_fstat fstat
#endif

#ifdef ANDROID
#include <limits.h>
#endif

#ifndef M_LN2
#define M_LN2 0.69314718055994530942
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef __cplusplus
extern "C" {
#endif
int flac_snprintf(char *str, size_t size, const char *fmt, ...);
int flac_vsnprintf(char *str, size_t size, const char *fmt, va_list va);
#ifdef __cplusplus
};
#endif

#endif
