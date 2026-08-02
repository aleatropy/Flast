cmake_minimum_required(VERSION 3.22.1)
project(flacplayer_native C)

# ---------------------------------------------------------------------
# Size-first native build (spec 3.8.2), applied to EVERY translation
# unit in the project -- not just libFLAC, which was the only thing
# setting -Os before. Everything here is a code-generation/link setting:
# no source behaviour changes, only how much of the result survives into
# the shipped .so.
#
#   -Os                        optimise for size instead of the NDK
#                              Release default of -O3. FLAC decoding is
#                              ~1% of one core; the UI redraws a few
#                              times a second. There is no speed budget
#                              being defended by -O3 here.
#   -ffunction-sections
#   -fdata-sections            put every function/object in its own
#     + -Wl,--gc-sections      section so the linker can drop the ones
#                              nothing calls. libFLAC ships encoder-side
#                              helpers (FLAC__lpc_compute_autocorrelation,
#                              FLAC__fixed_compute_best_predictor*,
#                              FLAC__lpc_compute_residual_*) in the SAME
#                              .o files as the decoder ones, so
#                              "decoder-only source list" alone did not
#                              keep them out of the binary. This does.
#   -fvisibility=hidden        the previous build exported 268 dynamic
#                              symbols, 213 of them FLAC__*, none of
#                              which anything outside this .so can call.
#                              Hiding them shrinks .dynsym/.dynstr/
#                              .gnu.hash/.gnu.version/.rela.plt/.plt AND
#                              is what lets --gc-sections above actually
#                              collect them. The real entry points
#                              (ANativeActivity_onCreate, Java_com_flast_*)
#                              are JNIEXPORT, i.e. explicitly
#                              visibility("default"), so they survive.
#   -fno-unwind-tables
#   -fno-asynchronous-unwind-tables
#                              this is plain C with no exceptions and no
#                              pthread_cancel; .eh_frame + .eh_frame_hdr
#                              were ~13KB of the .so and are only read
#                              when something has already crashed.
#                              Trade-off, stated plainly: native crash
#                              backtraces get less precise. Delete these
#                              two lines to get them back.
#   -fmerge-all-constants      fold identical constant tables/literals.
#   -flto=thin                 measured, not assumed: a further 4,192
#                              bytes off the arm64 .so on top of
#                              everything above, because it lets the
#                              optimiser see across translation units
#                              (and across the libFLAC static library)
#                              before --gc-sections runs. Verified to
#                              still export exactly the four entry
#                              points on all three ABIs.
#   -fomit-frame-pointer is deliberately NOT set: it is what keeps
#                              tombstone backtraces usable now that
#                              .eh_frame is gone.
# ---------------------------------------------------------------------
set(FLAST_SIZE_FLAGS
    -Os
    -ffunction-sections
    -fdata-sections
    -fvisibility=hidden
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -fmerge-all-constants
    -flto=thin
)

# Overrides the Android toolchain's "-O3 -DNDEBUG" default for Release.
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG")

include(third_party/flac-1.4.3/src/libFLAC/CMakeLists_decoder_only.txt)

if(NOT DEFINED ANDROID_NDK)
    if(DEFINED CMAKE_ANDROID_NDK)
        set(ANDROID_NDK ${CMAKE_ANDROID_NDK})
    else()
        message(FATAL_ERROR
            "Could not resolve the NDK path (neither ANDROID_NDK nor "
            "CMAKE_ANDROID_NDK is defined) -- needed to locate "
            "android_native_app_glue.c. Verify this build is coming "
            "from the Android Gradle plugin via externalNativeBuild.")
    endif()
endif()

set(NATIVE_APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(app-glue STATIC
    ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c
)
target_include_directories(app-glue PUBLIC ${NATIVE_APP_GLUE_DIR})
target_compile_options(app-glue PRIVATE ${FLAST_SIZE_FLAGS})

add_library(flacplayer_native SHARED
    native_bridge.c
    flac_stream.c
    playback_controller.c
    music_library.c
    ui_main.c
    ui_render.c
    ui_font_spleen8x16.c
)

target_include_directories(flacplayer_native PRIVATE ${NATIVE_APP_GLUE_DIR})
target_compile_options(flacplayer_native PRIVATE ${FLAST_SIZE_FLAGS})

find_library(log-lib log)
find_library(aaudio-lib aaudio)

find_library(android-lib android)

target_link_libraries(flacplayer_native
    app-glue
    ${log-lib}
    ${aaudio-lib}
    ${android-lib}
    flac_decoder
)

target_link_options(flacplayer_native PRIVATE
    # ANativeActivity_onCreate is only ever resolved by name at runtime
    # by the framework, so nothing in this .so references it -- without
    # -u, --gc-sections below would (correctly, from its point of view)
    # decide it is unreachable and delete the app's entry point.
    "-u" "ANativeActivity_onCreate"
    # Must be on the link line too, not just the compile line: with
    # ThinLTO the .o files hold bitcode, and the linker is what runs the
    # actual codegen.
    "-flto=thin"
    "-Wl,--gc-sections"
    # Identical code folding: merge byte-identical functions.
    "-Wl,--icf=all"
    # Belt and braces on top of -fvisibility=hidden: even if a future
    # libFLAC bump reintroduces FLAC_API visibility attributes, none of
    # its symbols get re-exported from this .so.
    "-Wl,--exclude-libs,libflac_decoder.a"
)
