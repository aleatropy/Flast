#ifndef FLAC_CONFIG_H
#define FLAC_CONFIG_H

/* Hand-written replacement for libFLAC's autotools/CMake-generated
 * config.h, trimmed to exactly what a decoder-only Android build needs.
 *
 * The CPU/SIMD block below used to hardcode `FLAC__CPU_ARM64 1` +
 * `FLAC__HAS_NEONINTRIN 1`, which silently made this header arm64-only:
 * on armeabi-v7a or x86_64 it would drag <arm_neon.h> and the A64
 * intrinsics into a build that cannot compile them. It now derives the
 * macros from the compiler's own target predefines, so the SAME header
 * builds every ABI the app ships (see `splits.abi` in build.gradle.kts)
 * with NEON on arm64 and the portable C path everywhere else. */

/* Every Android ABI (arm, arm64, x86, x86_64, riscv64) is little-endian. */
#define CPU_IS_BIG_ENDIAN 0
#define WORDS_BIGENDIAN 0

#if defined(__aarch64__)
  #define FLAC__CPU_ARM64 1
  #define FLAC__HAS_NEONINTRIN 1
  #define FLAC__HAS_A64NEONINTRIN 1
#else
  /* armeabi-v7a / x86_64: portable C. FLAC decoding of a 44.1kHz stereo
   * stream costs a low single-digit percentage of one core even without
   * SIMD, and the x86 intrinsic files are deliberately not part of the
   * decoder-only source list (see CMakeLists_decoder_only.txt). */
  #define FLAC__HAS_NEONINTRIN 0
  #define FLAC__HAS_A64NEONINTRIN 0
#endif

#define ENABLE_64_BIT_WORDS 0

#define OGG_FOUND 0
#define FLAC__HAS_OGG 0

#define FLAC__HAS_X86INTRIN 0

#define FLAC__SYS_LINUX 1

#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_MEMORY_H 1

#define HAVE_FSEEKO 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_LROUND 1
#define HAVE_BSWAP16 1
#define HAVE_BSWAP32 1
#define HAVE_TYPEOF 1

#define PACKAGE_VERSION "1.4.3"

#endif
