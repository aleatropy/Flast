/*
 * Flast — a bit-perfect FLAC player for Android.
 * Copyright (C) 2026 Aleatropy and the Flast contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Bundles libFLAC (decoder only), BSD-3-Clause, (C) Xiph.Org Foundation —
 * see app/src/main/cpp/third_party/flac-1.4.3/COPYING.Xiph
 */

#include <FLAC/stream_decoder.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "playback_controller.h"

#define LOG_TAG "FlastStream"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------
// AAudio MMAP introspection -- resolved at RUNTIME, never linked.
//
// AAudio_getMMapPolicy() and AAudioStream_isMMapUsed() are NOT in
// <aaudio/AAudio.h>. They are platform-internal symbols that happen to
// be listed in the NDK's libaaudio stub, so declaring them `extern` and
// linking against them compiles and works on a stock device -- and
// turns into a HARD FAILURE on any device whose libaaudio.so does not
// export them: the dynamic linker refuses to load libflacplayer_native.so
// at all, System.loadLibrary() throws UnsatisfiedLinkError from the
// FlastNativeActivity static initialiser, and the app dies before
// drawing a single frame. "Won't even open on that phone", with no way
// for the user to tell why.
//
// Google's own Oboe library resolves these two with dlsym() and
// null-checks them for exactly this reason. So does this file now. If
// they are missing the app still plays; it just cannot *verify* MMAP,
// which is a downgrade in reporting, not in function.
// ---------------------------------------------------------------------
typedef int32_t aaudio_policy_t;
#define AAUDIO_POLICY_NEVER ((aaudio_policy_t)1)

typedef aaudio_policy_t (*fn_get_mmap_policy)(void);
typedef bool (*fn_is_mmap_used)(AAudioStream *stream);

static fn_get_mmap_policy g_AAudio_getMMapPolicy = NULL;
static fn_is_mmap_used g_AAudioStream_isMMapUsed = NULL;
static atomic_bool g_mmap_syms_resolved = false;

static void resolve_mmap_symbols(void) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_mmap_syms_resolved, &expected, true)) {
        return;
    }
    // libaaudio.so is already loaded (this .so links against its public
    // API), so this is a refcount bump that hands back the existing
    // handle rather than a fresh load.
    void *handle = dlopen("libaaudio.so", RTLD_NOW);
    if (handle == NULL) {
        LOGW("dlopen(libaaudio.so) failed: %s -- MMAP state cannot be queried", dlerror());
        return;
    }
    g_AAudio_getMMapPolicy = (fn_get_mmap_policy)dlsym(handle, "AAudio_getMMapPolicy");
    g_AAudioStream_isMMapUsed = (fn_is_mmap_used)dlsym(handle, "AAudioStream_isMMapUsed");
    LOGI("MMAP introspection: getMMapPolicy=%s isMMapUsed=%s",
         g_AAudio_getMMapPolicy ? "yes" : "MISSING",
         g_AAudioStream_isMMapUsed ? "yes" : "MISSING");
}

// 300ms of headroom against decode-thread/AAudio-callback scheduling
// jitter is generous -- AAudio EXCLUSIVE calls back in chunks of tens
// of milliseconds, not seconds. Sized to the ACTUAL opened stream's
// sample_rate/channels rather than a worst-case constant, so a typical
// 44.1kHz/16-bit album costs ~105KB of ring buffer.
#define RING_BUFFER_MS 300

// How long the decode thread sleeps when the ring buffer is full (it
// holds RING_BUFFER_MS of audio, so there is no hurry) or the stream is
// paused. This used to be 5ms, which meant ~200 wakeups per second for
// the entire duration of every track -- a pure battery cost, since a
// full buffer has nothing for the thread to do. It also bounds how long
// a stop request takes to be noticed.
#define DECODE_IDLE_SLEEP_MS 15
#define DECODE_PAUSED_SLEEP_MS 50

// How long the AAudio stream may sit running on pure silence after a
// track has drained before it gives up and stops for real. See the
// dead-man's switch in aaudio_callback().
#define KEEP_ALIVE_SILENCE_S 3

typedef struct {
    int32_t *data;
    size_t capacity;

    // Single-producer (decode thread) / single-consumer (AAudio
    // callback) ring. write_pos is owned by the producer, read_pos by
    // the consumer, and `available` is the only shared counter: the
    // producer only ever adds to it, the consumer only ever subtracts.
    // That makes the whole thing lock-free.
    //
    // The mutex this replaced was held by the DECODE thread while it
    // copied a whole FLAC block, and taken by the AAudio callback --
    // a real-time thread blocking on a normal-priority one, which is
    // the textbook setup for priority inversion and audible dropouts.
    size_t write_pos;
    size_t read_pos;
    atomic_size_t available;
} RingBuffer;

// Split out from ring_buffer_init so the track-transition path can ask
// "would the next track need a different ring than the one already
// allocated?" without allocating anything to find out.
static size_t ring_capacity_for(uint32_t sample_rate, uint32_t channels) {
    size_t capacity_samples = ((size_t)sample_rate * RING_BUFFER_MS / 1000) * channels;
    if (capacity_samples < 4096) capacity_samples = 4096;
    return capacity_samples;
}

static bool ring_buffer_init(RingBuffer *rb, uint32_t sample_rate, uint32_t channels) {
    size_t capacity_samples = ((size_t)sample_rate * RING_BUFFER_MS / 1000) * channels;
    // A small floor for absurdly low sample rates, but nothing like the
    // 65536-sample (256KB) floor this used to need. That floor existed
    // only because ring_buffer_write() had to fit an entire decoded FLAC
    // block in one shot; write_callback() now feeds the ring in
    // whatever-fits chunks, so the capacity is free to be exactly the
    // 300ms the design asks for. For 44.1kHz stereo that is 105KB
    // instead of 256KB.
    if (capacity_samples < 4096) capacity_samples = 4096;
    rb->data = (int32_t *)malloc(capacity_samples * sizeof(int32_t));
    if (rb->data == NULL) {
        rb->capacity = 0;
        return false;
    }
    rb->capacity = capacity_samples;
    rb->write_pos = 0;
    rb->read_pos = 0;
    atomic_store(&rb->available, 0);
    return true;
}

static void ring_buffer_destroy(RingBuffer *rb) {
    free(rb->data);
    rb->data = NULL;
    rb->capacity = 0;
    atomic_store(&rb->available, 0);
}

// Producer side. Copies as much of `samples` as currently fits and
// returns how many it took; the caller loops. Two memcpy()s at most,
// instead of the per-sample `pos = (pos + 1) % capacity` the old
// version did (a division per sample, ~5 million of them per minute of
// stereo audio).
static size_t ring_buffer_write(RingBuffer *rb, const int32_t *samples, size_t count) {
    size_t used = atomic_load_explicit(&rb->available, memory_order_acquire);
    size_t space = rb->capacity - used;
    if (space == 0) return 0;
    if (count > space) count = space;

    size_t first = rb->capacity - rb->write_pos;
    if (first > count) first = count;
    memcpy(rb->data + rb->write_pos, samples, first * sizeof(int32_t));
    if (count > first) {
        memcpy(rb->data, samples + first, (count - first) * sizeof(int32_t));
    }
    rb->write_pos += count;
    if (rb->write_pos >= rb->capacity) rb->write_pos -= rb->capacity;

    atomic_fetch_add_explicit(&rb->available, count, memory_order_release);
    return count;
}

// Consumer side, called from the AAudio real-time callback. Converts on
// the way out to whatever PCM format the stream actually opened with
// (see open_stream_with_fallback) -- the ring always holds samples
// left-aligned in int32, which is exactly what AAUDIO_FORMAT_PCM_I32
// wants and what a >>16 turns back into the original 16-bit sample,
// bit for bit, with no rounding.
static void convert_samples(void *dst, size_t dst_index, const int32_t *src, size_t n,
                            aaudio_format_t fmt) {
    switch (fmt) {
        case AAUDIO_FORMAT_PCM_I16: {
            int16_t *o = (int16_t *)dst + dst_index;
            for (size_t i = 0; i < n; i++) o[i] = (int16_t)(src[i] >> 16);
            break;
        }
        case AAUDIO_FORMAT_PCM_FLOAT: {
            float *o = (float *)dst + dst_index;
            for (size_t i = 0; i < n; i++) o[i] = (float)src[i] * (1.0f / 2147483648.0f);
            break;
        }
        default: {
            memcpy((int32_t *)dst + dst_index, src, n * sizeof(int32_t));
            break;
        }
    }
}

static size_t ring_buffer_read(RingBuffer *rb, void *out, size_t count, aaudio_format_t fmt) {
    size_t used = atomic_load_explicit(&rb->available, memory_order_acquire);
    size_t to_read = count < used ? count : used;
    if (to_read == 0) return 0;

    size_t first = rb->capacity - rb->read_pos;
    if (first > to_read) first = to_read;
    convert_samples(out, 0, rb->data + rb->read_pos, first, fmt);
    if (to_read > first) {
        convert_samples(out, first, rb->data, to_read - first, fmt);
    }
    rb->read_pos += to_read;
    if (rb->read_pos >= rb->capacity) rb->read_pos -= rb->capacity;

    atomic_fetch_sub_explicit(&rb->available, to_read, memory_order_release);
    return to_read;
}

typedef struct {
    FLAC__StreamDecoder *decoder;
    AAudioStream *aaudio_stream;
    RingBuffer ring_buffer;

    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;

    // What the stream actually opened as, which is not necessarily what
    // was asked for -- see open_stream_with_fallback().
    aaudio_format_t stream_format;
    aaudio_sharing_mode_t stream_sharing;

    atomic_bool metadata_ready;
    atomic_bool decoding_finished;
    atomic_bool stop_requested;
    atomic_bool paused;

    pthread_t decode_thread;
    bool decode_thread_running;

    atomic_uint_fast64_t frames_played;

    // How many frames of pure silence the callback has emitted since the
    // ring last ran dry with decoding finished. Only touched by the
    // audio callback, so a plain counter is enough. It exists purely as
    // a dead-man's switch for the keep-the-stream-alive path below: if
    // the track never gets replaced, the stream must not idle forever
    // holding an EXCLUSIVE claim on the DAC.
    int64_t silent_frames;
} FlastStreamState;

static FlastStreamState g_state = {0};

// Set by AAudio when the stream is invalidated -- overwhelmingly because
// the USB DAC was unplugged. Measured on device: without this, the
// retained stream is reused on the next track, AAudioStream_requestStart
// returns AAUDIO_ERROR_INVALID_STATE, and that track silently does not
// play. It recovered on the track after, so exactly one track was lost
// per replug.
static atomic_bool g_stream_disconnected = false;

// Posted once by the decode thread when it stops producing. A counting
// semaphore, deliberately: the pthread_cond_signal() this replaced was
// LOST whenever the decode thread finished before the watcher thread
// reached pthread_cond_wait() -- which is exactly what happens with a
// very short track or a file that fails to decode. When the signal was
// lost the watcher blocked forever and auto-advance to the next track
// silently stopped working for the rest of the process's life. A
// semaphore counts, so an early post is still there when the waiter
// arrives.
static sem_t g_finished_sem;
static pthread_t g_watcher_thread;
static atomic_bool g_watcher_started = false;
static atomic_bool g_watcher_stop = false;

extern void native_bridge_notify_finished(void);

bool flast_stream_is_finished(void);

// The instant the ring last ran dry at the end of a track, i.e. the
// instant the listener stopped hearing music. flast_stream_play() logs
// how long it took to put audio back, which is the only honest way to
// talk about the gap between songs -- every other timestamp in this file
// is either before the tail has finished playing or after the fact.
static atomic_uint_fast64_t g_drain_ns = 0;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void sleep_ms(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void *finished_watcher_func(void *arg) {
    (void)arg;
    while (!atomic_load(&g_watcher_stop)) {
        if (sem_wait(&g_finished_sem) != 0) continue;
        if (atomic_load(&g_watcher_stop)) break;

        // Decoding is done; the tail of the track is still in the ring
        // buffer. Wait for the AAudio callback to drain it. Bounded so
        // a stream that dies mid-drain cannot pin this thread awake.
        //
        // The poll interval is 2ms rather than 20ms because every
        // millisecond spent here is silence the listener hears between
        // songs -- at 20ms this contributed an average of 10ms to the
        // gap for nothing. The extra wakeups only happen while a track
        // is actually draining (a few hundred ms per track), and are
        // noise beside the ~578 wakeups/second the EXCLUSIVE MMAP path
        // already costs during playback. Signalling from the audio
        // callback instead would be better still, but sem_post() on a
        // real-time thread is exactly the kind of syscall that is
        // forbidden there.
        bool drained = false;
        int guard_ms = 0;
        while (!atomic_load(&g_watcher_stop) && guard_ms < 10000) {
            if (flast_stream_is_finished()) {
                drained = true;
                atomic_store(&g_drain_ns, now_ns());
                break;
            }
            sleep_ms(2);
            guard_ms += 2;
        }
        if (atomic_load(&g_watcher_stop)) break;

        // Advance ONLY on a real drain. Hitting the guard means
        // something is not as expected -- the stream was replaced under
        // us, or it died mid-drain -- and the safe response to "I do not
        // know what is going on" is to do nothing, not to skip the
        // user's track.
        if (!drained) {
            LOGW("end-of-track watcher timed out without draining -- not advancing");
            continue;
        }

        native_bridge_notify_finished();
    }
    return NULL;
}

// Threads created here do nothing that needs a megabyte of stack (the
// biggest local left anywhere in this file is a few hundred bytes since
// write_callback()'s 128KB interleave buffer went away). Android's
// default is 1MB of address space per thread; capping it keeps the
// process's virtual footprint honest and its guard pages cheap.
#define FLAST_THREAD_STACK_BYTES (128 * 1024)

static int spawn_thread(pthread_t *out, void *(*fn)(void *), void *arg) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, FLAST_THREAD_STACK_BYTES);
    int rc = pthread_create(out, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    return rc;
}

static atomic_bool g_finished_sem_ready = false;

static void ensure_watcher_started(void) {
    bool expected = false;
    if (atomic_compare_exchange_strong(&g_watcher_started, &expected, true)) {
        // Its own flag, not g_watcher_started: that one is cleared again
        // if the thread fails to spawn, and sem_init() on an already
        // initialised semaphore is undefined.
        bool sem_expected = false;
        if (atomic_compare_exchange_strong(&g_finished_sem_ready, &sem_expected, true)) {
            sem_init(&g_finished_sem, 0, 0);
        }
        if (spawn_thread(&g_watcher_thread, finished_watcher_func, NULL) != 0) {
            LOGE("could not start the end-of-track watcher thread -- tracks will not auto-advance");
            atomic_store(&g_watcher_started, false);
        }
    }
}

// ---------------------------------------------------------------------
// Warming the next track.
//
// With the stream handed over hot (see aaudio_callback), the entire
// remaining gap between two songs is flast_stream_play() opening the next
// file and reading its header: measured on device at 42ms of a 47ms gap.
// None of that is decoding. It is one cold open on an SD card plus
// reading through the metadata blocks -- 80KB on a typical album track,
// nearly all of it padding libFLAC reads and throws away.
//
// So the fix is to make that read warm instead of cold: as soon as a
// track starts playing, pull the front of the NEXT file through the page
// cache on a throwaway thread. Minutes later, when the track actually
// ends, the same bytes are already in memory and the open costs almost
// nothing.
//
// Why warm the cache instead of pre-opening a decoder, which would be the
// obvious move: libFLAC fixes client_data at FLAC__stream_decoder_init_*
// time, and write_callback needs it to be &g_state. A decoder opened
// against a scratch struct cannot be handed to the real playback path
// afterwards -- every decoded block would be delivered to the wrong
// place. Warming has the same effect on the critical path with none of
// that risk.
//
// It also costs the app no memory at all. The page cache is kernel
// file-backed memory, reclaimable under pressure and not charged to the
// process. If it gets evicted before the track ends, or the read fails,
// or the thread never runs, the next track simply opens cold exactly as
// it does today. There is no correctness path through this code.
#define PREFETCH_WARM_BYTES (256 * 1024)

static atomic_bool g_prefetch_running = false;
static pthread_mutex_t g_prefetch_lock = PTHREAD_MUTEX_INITIALIZER;
static char *g_prefetch_path = NULL;
static FILE *g_prefetch_file = NULL;

static void prefetch_discard_locked(void) {
    if (g_prefetch_file != NULL) {
        fclose(g_prefetch_file);
        g_prefetch_file = NULL;
    }
    free(g_prefetch_path);
    g_prefetch_path = NULL;
}

void flast_stream_prefetch_discard(void) {
    pthread_mutex_lock(&g_prefetch_lock);
    prefetch_discard_locked();
    pthread_mutex_unlock(&g_prefetch_lock);
}

// Hand the caller the already-open handle for this path, if there is one,
// transferring ownership. trylock rather than lock on purpose: if the
// warm-up thread is still working, waiting for it would ADD to the very
// gap this exists to remove. Failing to claim just means opening cold,
// which is the behaviour this whole mechanism is an optimisation over.
static FILE *prefetch_claim(const char *filepath) {
    FILE *fp = NULL;
    if (pthread_mutex_trylock(&g_prefetch_lock) != 0) return NULL;
    if (g_prefetch_file != NULL && g_prefetch_path != NULL &&
        strcmp(g_prefetch_path, filepath) == 0) {
        fp = g_prefetch_file;
        g_prefetch_file = NULL;
        free(g_prefetch_path);
        g_prefetch_path = NULL;
    }
    pthread_mutex_unlock(&g_prefetch_lock);
    return fp;
}

static void *prefetch_thread_func(void *arg) {
    char *path = (char *)arg;

    // Open it AND pull the front of it through the page cache. The open
    // is the expensive half -- measured at 36ms of a 48ms gap on SD-card
    // storage, against 5ms for the header read itself -- so the handle is
    // kept rather than the bytes alone.
    FILE *fp = fopen(path, "rb");
    if (fp != NULL) {
        char buf[8192];
        size_t done = 0;
        while (done < PREFETCH_WARM_BYTES) {
            size_t n = fread(buf, 1, sizeof buf, fp);
            if (n == 0) break;
            done += n;
        }
        // libFLAC starts reading wherever the handle is left.
        if (fseek(fp, 0, SEEK_SET) != 0) {
            fclose(fp);
            fp = NULL;
        }
    }

    pthread_mutex_lock(&g_prefetch_lock);
    prefetch_discard_locked();
    if (fp != NULL) {
        g_prefetch_file = fp;
        g_prefetch_path = path;
        path = NULL;
    }
    pthread_mutex_unlock(&g_prefetch_lock);

    free(path);
    atomic_store(&g_prefetch_running, false);
    return NULL;
}

void flast_stream_prefetch(const char *filepath) {
    if (filepath == NULL) return;
    // One at a time. A skipped warm-up costs 42ms on one transition;
    // piling up threads against a slow card would cost more than it saves.
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_prefetch_running, &expected, true)) return;

    char *copy = strdup(filepath);
    if (copy == NULL) {
        atomic_store(&g_prefetch_running, false);
        return;
    }
    pthread_t t;
    if (spawn_thread(&t, prefetch_thread_func, copy) != 0) {
        free(copy);
        atomic_store(&g_prefetch_running, false);
        return;
    }
    pthread_detach(t);
}

static void metadata_callback(const FLAC__StreamDecoder *decoder,
                                const FLAC__StreamMetadata *metadata,
                                void *client_data) {
    (void)decoder;
    FlastStreamState *state = (FlastStreamState *)client_data;

    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
        state->sample_rate = metadata->data.stream_info.sample_rate;
        state->channels = metadata->data.stream_info.channels;
        state->bits_per_sample = metadata->data.stream_info.bits_per_sample;
        atomic_store(&state->metadata_ready, true);

        LOGI("STREAMINFO: %u Hz, %u canales, %u bits",
             state->sample_rate, state->channels, state->bits_per_sample);
    }
}

static FLAC__StreamDecoderWriteStatus write_callback(
    const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame,
    const FLAC__int32 *const buffer[], void *client_data) {
    (void)decoder;
    FlastStreamState *state = (FlastStreamState *)client_data;

    uint32_t block_size = frame->header.blocksize;
    uint32_t channels = frame->header.channels;
    uint32_t shift = state->bits_per_sample < 32 ? (32 - state->bits_per_sample) : 0;

    // The 128KB `int32_t interleaved[32768]` local that used to live
    // here is gone. It was 128KB of *stack* -- touched, therefore
    // resident, therefore real RSS -- on a thread that exists for the
    // entire duration of every track, and it also forced the ring
    // buffer to be at least that big (see ring_buffer_init). Frames are
    // now interleaved a chunk at a time straight into the ring, so the
    // working set here is 2KB and the ring is sized by the design's
    // 300ms rule instead of by this array.
    int32_t chunk[512];

    // A frame whose channel count disagrees with STREAMINFO would be
    // interleaved at one width and consumed by the AAudio callback at
    // another, i.e. channel-swapped noise for the rest of the track.
    // Corrupt/unusual files exist; refuse rather than play garbage.
    if (channels == 0 || channels != state->channels) {
        LOGE("frame declares %u channels but STREAMINFO said %u -- aborting decode",
             channels, state->channels);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    const size_t chunk_frames = sizeof(chunk) / sizeof(chunk[0]) / channels;
    if (chunk_frames == 0) {
        LOGE("%u channels is more than the interleave chunk can hold", channels);
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    uint32_t frame_index = 0;
    while (frame_index < block_size) {
        if (atomic_load(&state->stop_requested)) {
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }

        uint32_t n = block_size - frame_index;
        if (n > chunk_frames) n = (uint32_t)chunk_frames;

        size_t produced = 0;
        for (uint32_t i = 0; i < n; i++) {
            for (uint32_t ch = 0; ch < channels; ch++) {
                // Cast through unsigned: left-shifting a negative
                // int32_t is undefined behaviour in C, and at -Os with
                // LTO the compiler is entitled to act on that.
                uint32_t s = (uint32_t)buffer[ch][frame_index + i];
                chunk[produced++] = (int32_t)(s << shift);
            }
        }

        size_t written = 0;
        while (written < produced) {
            if (atomic_load(&state->stop_requested)) {
                return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            }
            if (atomic_load(&state->paused)) {
                sleep_ms(DECODE_PAUSED_SLEEP_MS);
                continue;
            }
            size_t took = ring_buffer_write(&state->ring_buffer, chunk + written, produced - written);
            if (took == 0) {
                // Ring is full, i.e. there are RING_BUFFER_MS of audio
                // already queued ahead of the speaker. Nothing useful to
                // do but wait for the callback to consume some.
                sleep_ms(DECODE_IDLE_SLEEP_MS);
                continue;
            }
            written += took;
        }

        frame_index += n;
    }

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void error_callback(const FLAC__StreamDecoder *decoder,
                             FLAC__StreamDecoderErrorStatus status,
                             void *client_data) {
    (void)decoder;
    (void)client_data;
    LOGE("Error de decodificacion FLAC: %s",
         FLAC__StreamDecoderErrorStatusString[status]);
}

static void *decode_thread_func(void *arg) {
    FlastStreamState *state = (FlastStreamState *)arg;

    bool ok = FLAC__stream_decoder_process_until_end_of_stream(state->decoder);
    bool aborted = atomic_load(&state->stop_requested);

    atomic_store(&state->decoding_finished, true);

    // Post ONLY for a decode that reached the real end of the stream.
    //
    // An aborted decode is the normal path for "user pressed next" or
    // "the queue moved on", and posting for it caused a track to advance
    // by itself roughly ten seconds later, forever. The stale post woke
    // the watcher against the track that had just STARTED; that track
    // was obviously not drained, the watcher's guard timer expired, and
    // the guard used to notify anyway -- which advanced the track, which
    // aborted another decode, which posted again. Measured on device at
    // a ~10.5 s cadence before this check existed.
    if (!aborted && atomic_load(&g_finished_sem_ready)) {
        sem_post(&g_finished_sem);
    }

    if (!ok && !aborted) {
        LOGW("Decodificacion termino con error");
    }
    LOGI("Thread de decodificacion terminado (%s)", aborted ? "abortado" : "fin de stream");
    return NULL;
}

static aaudio_data_callback_result_t aaudio_callback(
    AAudioStream *stream, void *userData, void *audioData, int32_t numFrames) {
    (void)stream;
    FlastStreamState *state = (FlastStreamState *)userData;

    size_t samples_needed = (size_t)numFrames * state->channels;
    size_t samples_read = ring_buffer_read(&state->ring_buffer, audioData, samples_needed,
                                           state->stream_format);

    if (samples_read > 0) {
        // Only real audio advances the clock. Silence emitted between
        // tracks (see below) must not, or the elapsed time would run on
        // through the gap and the next track would start out of sync
        // with its own position display.
        atomic_fetch_add(&state->frames_played, (uint64_t)numFrames);
        state->silent_frames = 0;
    }

    if (samples_read < samples_needed) {
        size_t bytes_per_sample =
            state->stream_format == AAUDIO_FORMAT_PCM_I16 ? sizeof(int16_t) : sizeof(int32_t);
        memset((char *)audioData + samples_read * bytes_per_sample, 0,
               (samples_needed - samples_read) * bytes_per_sample);

        if (atomic_load(&state->decoding_finished) && samples_read == 0) {
            // The track is over and the ring is dry. This used to return
            // STOP, which meant every single track transition paid for a
            // full AAudio stop and a fresh requestStart -- two HAL round
            // trips inside the silent gap between songs, for a stream
            // whose parameters had not changed at all.
            //
            // Keeping the stream RUNNING and feeding it silence lets
            // flast_stream_play() hand the next track to the same live
            // stream and the same ring buffer. The gap becomes only the
            // time to open the next file, not open-plus-renegotiate.
            //
            // The dead-man's switch: if nothing replaces the track within
            // a few seconds, something upstream went wrong (the watcher
            // hit its guard and declined to advance, the queue ended
            // without a stop reaching us) and idling forever would pin an
            // EXCLUSIVE claim on the DAC and keep waking the CPU
            // hundreds of times a second. Stop for real at that point.
            state->silent_frames += numFrames;
            int32_t rate = state->sample_rate > 0 ? (int32_t)state->sample_rate : 48000;
            if (state->silent_frames > (int64_t)rate * KEEP_ALIVE_SILENCE_S) {
                return AAUDIO_CALLBACK_RESULT_STOP;
            }
        }
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// Runs on AAudio's own thread. Does nothing but set a flag: no locks, no
// allocation, no logging in anything AAudio calls back into.
static void aaudio_error_callback(AAudioStream *stream, void *userData, aaudio_result_t error) {
    (void)stream;
    (void)userData;
    (void)error;
    atomic_store(&g_stream_disconnected, true);
}

static void close_stream(void) {
    if (g_state.aaudio_stream != NULL) {
        AAudioStream_requestStop(g_state.aaudio_stream);
        AAudioStream_close(g_state.aaudio_stream);
        g_state.aaudio_stream = NULL;
    }
}

static void stop_decode_thread(void) {
    if (g_state.decode_thread_running) {
        atomic_store(&g_state.stop_requested, true);
        pthread_join(g_state.decode_thread, NULL);
        g_state.decode_thread_running = false;
    }
}

static void release_decoder(void) {
    if (g_state.decoder != NULL) {
        FLAC__stream_decoder_delete(g_state.decoder);
        g_state.decoder = NULL;
    }
}

// Bring the output to a state where the ring buffer is provably not
// being read any more, then free it. The wait is the whole point: the
// AAudio callback runs on a thread this code does not own, so "asked it
// to stop" is not the same as "it has stopped", and freeing rb->data in
// between is a use-after-free on the real-time thread.
//
// The stream itself is left OPEN -- only stopped. Whether it is also
// closed and renegotiated is a separate question, answered later once
// the next file's format is known.
static void quiesce_stream_and_ring(void) {
    if (g_state.aaudio_stream != NULL) {
        AAudioStream_requestStop(g_state.aaudio_stream);
        aaudio_stream_state_t settled = AAUDIO_STREAM_STATE_UNKNOWN;
        AAudioStream_waitForStateChange(g_state.aaudio_stream,
                                        AAUDIO_STREAM_STATE_STOPPING, &settled,
                                        500LL * 1000000LL);
    }
    if (g_state.ring_buffer.data != NULL) {
        ring_buffer_destroy(&g_state.ring_buffer);
    }
}

static void flast_stream_stop_internal(void) {
    stop_decode_thread();
    close_stream();
    release_decoder();
    if (g_state.ring_buffer.data != NULL) {
        ring_buffer_destroy(&g_state.ring_buffer);
    }
}

// ---------------------------------------------------------------------
// Opening the output stream.
//
// The old code had exactly one way to play audio: AAudio EXCLUSIVE with
// PCM_I32, and if the device said no -- or if AAudio_getMMapPolicy()
// came back NEVER -- it returned an error and played NOTHING. On any
// handset whose HAL does not do MMAP/EXCLUSIVE (which is a large share
// of them; spec section 5 documents exactly this fragmentation) the app
// was silent, permanently, with no explanation.
//
// The specification never asked for that. Section 3.6 spells out the
// opposite: "Degradado a SHARED (mixer de Android en uso)" and "el
// dispositivo/HAL no lo soporta en absoluto" are both listed as normal
// states that show `BIT-PERFECT: NO` -- states of the INDICATOR, not
// reasons to refuse to play. This ladder implements that: try hardest
// for bit-perfect, fall back as far as needed, and tell the truth about
// which rung it landed on.
// ---------------------------------------------------------------------
typedef struct {
    aaudio_sharing_mode_t sharing;
    aaudio_format_t format;
    aaudio_performance_mode_t performance;
} StreamAttempt;

static const StreamAttempt k_attempts[] = {
    // Bit-perfect rungs. LOW_LATENCY is what makes a HAL hand out an
    // MMAP/EXCLUSIVE path at all, so it stays on for these two.
    //
    // The I16 rung is skipped for files deeper than 16 bits (see the
    // guard in open_stream_with_fallback): feeding a 24-bit file through
    // a 16-bit stream means convert_samples() drops the low 8 bits, and
    // an EXCLUSIVE stream that is quietly truncating is exactly the kind
    // of thing this project must not report as BIT-PERFECT: YES. Such a
    // file goes to the SHARED rungs instead, where the HAL converts
    // properly and the indicator honestly says NO.
    {AAUDIO_SHARING_MODE_EXCLUSIVE, AAUDIO_FORMAT_PCM_I32, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY},
    {AAUDIO_SHARING_MODE_EXCLUSIVE, AAUDIO_FORMAT_PCM_I16, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY},
    // Fallback rungs: bit-perfect is already lost here, so ask for
    // PERFORMANCE_MODE_NONE instead -- bigger buffers, fewer callbacks,
    // measurably less battery for the same audio.
    {AAUDIO_SHARING_MODE_SHARED, AAUDIO_FORMAT_PCM_I32, AAUDIO_PERFORMANCE_MODE_NONE},
    {AAUDIO_SHARING_MODE_SHARED, AAUDIO_FORMAT_PCM_FLOAT, AAUDIO_PERFORMANCE_MODE_NONE},
    {AAUDIO_SHARING_MODE_SHARED, AAUDIO_FORMAT_PCM_I16, AAUDIO_PERFORMANCE_MODE_NONE},
};

static const char *sharing_name(aaudio_sharing_mode_t m) {
    return m == AAUDIO_SHARING_MODE_EXCLUSIVE ? "EXCLUSIVE" : "SHARED";
}

static bool open_stream_with_fallback(void) {
    resolve_mmap_symbols();

    bool skip_exclusive = false;
    if (g_AAudio_getMMapPolicy != NULL && g_AAudio_getMMapPolicy() == AAUDIO_POLICY_NEVER) {
        // Not an error, just information: this device's HAL will never
        // grant an MMAP path, so the two EXCLUSIVE rungs would only
        // waste two open attempts before landing on SHARED anyway.
        LOGI("AAudio MMAP policy is NEVER on this device -- going straight to SHARED");
        skip_exclusive = true;
    }

    for (size_t i = 0; i < sizeof(k_attempts) / sizeof(k_attempts[0]); i++) {
        const StreamAttempt *a = &k_attempts[i];
        if (skip_exclusive && a->sharing == AAUDIO_SHARING_MODE_EXCLUSIVE) continue;
        if (a->sharing == AAUDIO_SHARING_MODE_EXCLUSIVE &&
            a->format == AAUDIO_FORMAT_PCM_I16 && g_state.bits_per_sample > 16) {
            continue; // would silently truncate; see the note on k_attempts
        }

        AAudioStreamBuilder *builder = NULL;
        if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == NULL) {
            LOGE("AAudio_createStreamBuilder failed");
            return false;
        }

        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setSharingMode(builder, a->sharing);
        AAudioStreamBuilder_setPerformanceMode(builder, a->performance);
        AAudioStreamBuilder_setSampleRate(builder, (int32_t)g_state.sample_rate);
        AAudioStreamBuilder_setChannelCount(builder, (int32_t)g_state.channels);
        AAudioStreamBuilder_setFormat(builder, a->format);
        AAudioStreamBuilder_setDataCallback(builder, aaudio_callback, &g_state);
        AAudioStreamBuilder_setErrorCallback(builder, aaudio_error_callback, &g_state);

        AAudioStream *stream = NULL;
        aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &stream);
        AAudioStreamBuilder_delete(builder);

        if (result != AAUDIO_OK || stream == NULL) {
            LOGI("AAudio open %s/fmt%d rejected (%s) -- trying next configuration",
                 sharing_name(a->sharing), (int)a->format, AAudio_convertResultToText(result));
            continue;
        }

        // A stream that opened is not automatically a stream we can
        // use. In EXCLUSIVE mode the HAL's own rate/channel layout wins
        // over what was requested, and playing 44.1kHz content through
        // a 48kHz stream is not "slightly wrong", it is the whole album
        // at the wrong pitch. Verify before committing.
        int32_t got_rate = AAudioStream_getSampleRate(stream);
        int32_t got_channels = AAudioStream_getChannelCount(stream);
        if (got_rate != (int32_t)g_state.sample_rate || got_channels != (int32_t)g_state.channels) {
            LOGW("AAudio %s gave %d Hz/%d ch for a %u Hz/%u ch file -- rejecting, trying next",
                 sharing_name(a->sharing), got_rate, got_channels,
                 g_state.sample_rate, g_state.channels);
            AAudioStream_close(stream);
            continue;
        }

        g_state.aaudio_stream = stream;
        atomic_store(&g_stream_disconnected, false);
        g_state.stream_sharing = AAudioStream_getSharingMode(stream);
        g_state.stream_format = AAudioStream_getFormat(stream);
        LOGI("AAudio open OK: %s, format %d, %d Hz, %d ch, burst=%d frames, "
             "bufCap=%d frames, framesPerCallback=%d",
             sharing_name(g_state.stream_sharing), (int)g_state.stream_format,
             got_rate, got_channels,
             AAudioStream_getFramesPerBurst(stream),
             AAudioStream_getBufferCapacityInFrames(stream),
             AAudioStream_getFramesPerDataCallback(stream));
        return true;
    }

    LOGE("Every AAudio configuration was rejected for %u Hz/%u ch",
         g_state.sample_rate, g_state.channels);
    return false;
}

static bool needs_stream_reopen(uint32_t new_sample_rate, uint32_t new_channels,
                                  int32_t current_sample_rate, int32_t current_channels) {
    if (current_sample_rate < 0) {
        return true;
    }
    return (int32_t)new_sample_rate != current_sample_rate ||
           (int32_t)new_channels != current_channels;
}

typedef enum {
    FLAST_OK = 0,
    FLAST_ERROR_FILE_OPEN = 1,
    FLAST_ERROR_AAUDIO_OPEN = 2,
} FlastResult;

FlastResult flast_stream_play(const char *filepath, bool *out_is_bit_perfect,
                                int32_t *out_sample_rate, int32_t *out_bits) {

    int32_t previous_sample_rate = -1;
    int32_t previous_channels = -1;

    // Order matters and is not obvious: BOTH ends of the ring buffer
    // have to be quiesced before it can be freed. The decode thread is
    // the writer, the AAudio callback is the reader, and the callback
    // runs on a thread this code does not own. Freeing rb->data while
    // AAudio is still calling back is a use-after-free on the audio
    // thread.
    //
    // Stopping the decode thread is unconditional -- it is the writer,
    // and nothing below may touch the ring while it still runs. But
    // stopping the STREAM and freeing the ring are deliberately deferred
    // until the new file's sample rate and channel count are known,
    // because when they match what is already open (an album, i.e. the
    // overwhelmingly common case) neither has to happen at all. That is
    // what removes two HAL round trips from the gap between songs.
    stop_decode_thread();
    if (g_state.aaudio_stream != NULL) {
        previous_sample_rate = AAudioStream_getSampleRate(g_state.aaudio_stream);
        previous_channels = AAudioStream_getChannelCount(g_state.aaudio_stream);
    }
    // The decoder is only ever touched by the decode thread, which is now
    // joined, so this is safe here regardless of what the stream is doing.
    release_decoder();

    // Is the stream still live and playing out silence, with a ring that
    // has genuinely run dry? Only then can both be handed to the next
    // track untouched. A ring with samples still in it means this is a
    // manual skip, not a track ending -- reusing it there would play the
    // tail of the track the user just skipped away from.
    bool stream_alive = false;
    if (g_state.aaudio_stream != NULL &&
        g_state.ring_buffer.data != NULL &&
        atomic_load(&g_state.ring_buffer.available) == 0 &&
        !atomic_load(&g_stream_disconnected) &&
        AAudioStream_getState(g_state.aaudio_stream) == AAUDIO_STREAM_STATE_STARTED) {
        stream_alive = true;
    }

    if (!stream_alive) {
        quiesce_stream_and_ring();
    }

    // Discard any end-of-track posts left over from the track that just
    // ended or was interrupted, so the watcher cannot act on this track
    // because of the previous one.
    if (atomic_load(&g_finished_sem_ready)) {
        while (sem_trywait(&g_finished_sem) == 0) { /* drain */ }
    }

    atomic_store(&g_state.metadata_ready, false);
    atomic_store(&g_state.decoding_finished, false);
    atomic_store(&g_state.stop_requested, false);
    atomic_store(&g_state.paused, false);
    atomic_store(&g_state.frames_played, 0);

    ensure_watcher_started();

    g_state.decoder = FLAC__stream_decoder_new();
    if (g_state.decoder == NULL) {
        return FLAST_ERROR_FILE_OPEN;
    }

    // A handle warmed during the previous track, if the next-track guess
    // was right. init_FILE takes ownership exactly as init_file does --
    // FLAC__stream_decoder_finish() closes it either way -- so there is
    // no separate lifetime to track here.
    FILE *warm = prefetch_claim(filepath);
    uint64_t t_open0 = now_ns();
    FLAC__StreamDecoderInitStatus init_status =
        warm != NULL
            ? FLAC__stream_decoder_init_FILE(g_state.decoder, warm, write_callback,
                                             metadata_callback, error_callback, &g_state)
            : FLAC__stream_decoder_init_file(g_state.decoder, filepath, write_callback,
                                             metadata_callback, error_callback, &g_state);
    uint64_t t_open1 = now_ns();

    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        LOGE("No se pudo inicializar decoder para: %s", filepath);
        release_decoder();
        return FLAST_ERROR_FILE_OPEN;
    }

    if (!FLAC__stream_decoder_process_until_end_of_metadata(g_state.decoder)) {
        LOGE("Fallo leyendo metadata de: %s", filepath);
        release_decoder();
        return FLAST_ERROR_FILE_OPEN;
    }
    uint64_t t_meta = now_ns();

    if (!atomic_load(&g_state.metadata_ready) ||
        g_state.sample_rate == 0 || g_state.channels == 0) {
        LOGE("El archivo no tiene STREAMINFO valido: %s", filepath);
        release_decoder();
        return FLAST_ERROR_FILE_OPEN;
    }

    bool reopen = needs_stream_reopen(g_state.sample_rate, g_state.channels,
                                        previous_sample_rate, previous_channels);

    // A stream whose device has gone away can still be reported with the
    // right sample rate and channel count, so the parameter comparison
    // above happily decides to reuse it -- and then requestStart() fails.
    // Both the error callback and an explicit state check are consulted,
    // because the callback is not guaranteed to have run yet by the time
    // the user taps the next track.
    if (!reopen && g_state.aaudio_stream != NULL) {
        if (atomic_load(&g_stream_disconnected) ||
            AAudioStream_getState(g_state.aaudio_stream) == AAUDIO_STREAM_STATE_DISCONNECTED) {
            LOGI("previous stream was disconnected (DAC unplugged?) -- reopening");
            reopen = true;
        }
    }

    // A ring that is still allocated at this point belongs to a stream
    // that was left running on silence. It can only be handed to the next
    // track if it is exactly the size that track needs -- same rate, same
    // channel count. Anything else and both stream and ring go.
    if (stream_alive &&
        (reopen ||
         g_state.ring_buffer.capacity !=
             ring_capacity_for(g_state.sample_rate, g_state.channels))) {
        quiesce_stream_and_ring();
        stream_alive = false;
    }

    if (reopen) {
        LOGI("Parametros distintos al stream anterior -- reabriendo AAudio "
             "(%d Hz -> %u Hz, %d ch -> %u ch)",
             previous_sample_rate, g_state.sample_rate,
             previous_channels, g_state.channels);
        close_stream();
        if (!open_stream_with_fallback()) {
            release_decoder();
            return FLAST_ERROR_AAUDIO_OPEN;
        }
    } else if (stream_alive) {
        LOGI("Mismos parametros -- stream y ring reutilizados EN CALIENTE (sin stop/start)");
    } else {
        LOGI("Mismos parametros que el stream anterior -- reutilizando AAudioStream");
    }

    if (!stream_alive &&
        !ring_buffer_init(&g_state.ring_buffer, g_state.sample_rate, g_state.channels)) {
        LOGE("Sin memoria para el ring buffer de %u Hz/%u ch", g_state.sample_rate, g_state.channels);
        release_decoder();
        return FLAST_ERROR_FILE_OPEN;
    }

    if (spawn_thread(&g_state.decode_thread, decode_thread_func, &g_state) != 0) {
        LOGE("No se pudo crear el thread de decodificacion");
        // Previously this path leaked the decoder and the ring buffer
        // it had just allocated, on every failure.
        ring_buffer_destroy(&g_state.ring_buffer);
        release_decoder();
        return FLAST_ERROR_FILE_OPEN;
    }
    g_state.decode_thread_running = true;

    // Pre-roll: wait for the decoder to put something in the ring before
    // starting the stream, so playback does not open with a burst of
    // silence. This used to be a flat 50ms sleep, which is both too long
    // on fast storage and too short on slow storage; waiting for the
    // actual condition (or bailing out at 400ms) is strictly better on
    // both.
    //
    // How much to wait for depends on whether the stream is already
    // running. On a cold start the ring is the only thing standing
    // between requestStart() and an underrun, so a quarter of it is
    // worth waiting for. On a hot hand-over the stream is already live
    // and emitting silence, so every millisecond spent here is silence
    // the listener hears: wait for one eighth, which is still ~37ms of
    // audio at 44.1kHz, and poll at 2ms so the exit is not rounded up.
    uint64_t t_preroll0 = now_ns();
    size_t preroll_divisor = stream_alive ? 8 : 4;
    for (int waited_ms = 0; waited_ms < 400; waited_ms += 2) {
        size_t queued = atomic_load(&g_state.ring_buffer.available);
        if (queued * preroll_divisor >= g_state.ring_buffer.capacity) break;
        if (atomic_load(&g_state.decoding_finished)) break;
        sleep_ms(2);
    }

    // A stream that was never stopped is already started; asking again is
    // at best a no-op and at worst an INVALID_STATE that the recovery
    // path below would answer by needlessly renegotiating the stream.
    uint64_t t_start1 = now_ns();
    aaudio_result_t start = stream_alive
                                ? AAUDIO_OK
                                : AAudioStream_requestStart(g_state.aaudio_stream);
    if (start != AAUDIO_OK && !reopen) {
        // The stream we reused turned out to be unusable after all. Rather
        // than drop this track, close it and negotiate a fresh one once.
        LOGW("requestStart on the reused stream failed (%s) -- reopening from scratch",
             AAudio_convertResultToText(start));
        close_stream();
        if (open_stream_with_fallback()) {
            start = AAudioStream_requestStart(g_state.aaudio_stream);
        }
    }
    if (start != AAUDIO_OK) {
        LOGE("AAudioStream_requestStart fallo: %s", AAudio_convertResultToText(start));
        stop_decode_thread();
        close_stream();
        release_decoder();
        ring_buffer_destroy(&g_state.ring_buffer);
        return FLAST_ERROR_AAUDIO_OPEN;
    }

    // Bit-perfect only when the DEVICE granted an exclusive path. When
    // AAudioStream_isMMapUsed() is resolvable it is the authority; when
    // the platform does not export it, an AAudio-granted EXCLUSIVE
    // stream is the strongest evidence available and is what gets
    // reported, which the log records so the claim is auditable.
    bool exclusive = (g_state.stream_sharing == AAUDIO_SHARING_MODE_EXCLUSIVE);
    bool mmap_ok = true;
    if (g_AAudioStream_isMMapUsed != NULL) {
        mmap_ok = g_AAudioStream_isMMapUsed(g_state.aaudio_stream);
    } else {
        LOGW("AAudioStream_isMMapUsed unavailable -- reporting bit-perfect from "
             "the granted sharing mode alone");
    }
    // Third condition, and it is not redundant: the stream format has to
    // be able to carry every bit the file has. PCM_I32 always can;
    // PCM_I16 only for <=16-bit content. Without this an exclusive
    // 16-bit stream playing a 24-bit file would claim BIT-PERFECT: YES
    // while convert_samples() threw away the bottom 8 bits of every
    // sample.
    bool format_lossless = (g_state.stream_format == AAUDIO_FORMAT_PCM_I32) ||
                           (g_state.stream_format == AAUDIO_FORMAT_PCM_I16 &&
                            g_state.bits_per_sample <= 16);
    *out_is_bit_perfect = exclusive && mmap_ok && format_lossless;
    LOGI("bit-perfect=%d (exclusive=%d mmap=%d format_lossless=%d, %u-bit source)",
         (int)*out_is_bit_perfect, (int)exclusive, (int)mmap_ok, (int)format_lossless,
         g_state.bits_per_sample);
    *out_sample_rate = AAudioStream_getSampleRate(g_state.aaudio_stream);
    *out_bits = (int32_t)g_state.bits_per_sample;

    // How much silence the listener actually heard, measured from the
    // moment the previous track's last sample left the ring. Only
    // meaningful when this play() followed a track ending; a fresh start
    // or a manual skip has no gap to report.
    uint64_t drained_at = atomic_exchange(&g_drain_ns, 0);
    if (drained_at != 0) {
        // Kept in the shipped build on purpose: this is the only number
        // that says what a listener actually hears between two songs, and
        // it is entirely at the mercy of hardware this project cannot
        // test on -- storage speed and the HAL. A gap report from a
        // stranger's phone is worth more than any measurement taken here.
        LOGI("GAP entre pistas: %llu ms (%s, open %s) "
             "[teardown=%.1f open=%.1f meta=%.1f preroll=%.1f start=%.1f]",
             (unsigned long long)((now_ns() - drained_at) / 1000000ULL),
             stream_alive ? "hand-over en caliente" : "stream reiniciado",
             warm ? "precalentado" : "en frio",
             (double)(t_open0 - drained_at) / 1e6,
             (double)(t_open1 - t_open0) / 1e6,
             (double)(t_meta - t_open1) / 1e6,
             (double)(t_start1 - t_preroll0) / 1e6,
             (double)(now_ns() - t_start1) / 1e6);
    }

    return FLAST_OK;
}

void flast_stream_stop(void) {
    flast_stream_stop_internal();
    // Nothing is coming next, so the warmed handle for a track that will
    // not be played is just an open file descriptor doing nothing.
    flast_stream_prefetch_discard();
    g_state.sample_rate = 0;
    g_state.channels = 0;
}

// Swaps ONLY the output stream. The decoder, its position in the file,
// the ring buffer and frames_played all survive -- which is what lets
// playback resume from the same timestamp after the audio device changed
// underneath it. No seeking is involved, and no libFLAC seek code is
// pulled into the binary, because the decoder never stopped.
static bool reopen_output_only(void) {
    if (g_state.sample_rate == 0 || g_state.channels == 0) return false;
    close_stream();
    return open_stream_with_fallback();
}

static bool output_is_dead(void) {
    if (g_state.aaudio_stream == NULL) return true;
    if (atomic_load(&g_stream_disconnected)) return true;
    aaudio_stream_state_t st = AAudioStream_getState(g_state.aaudio_stream);
    return st == AAUDIO_STREAM_STATE_DISCONNECTED ||
           st == AAUDIO_STREAM_STATE_CLOSING ||
           st == AAUDIO_STREAM_STATE_CLOSED;
}

void flast_stream_pause(void) {
    // Set this first and unconditionally: it is what actually stops the
    // decode thread feeding the ring, and it must take effect even if the
    // stream below is already dead.
    atomic_store(&g_state.paused, true);
    if (g_state.aaudio_stream == NULL) return;
    AAudioStream_requestPause(g_state.aaudio_stream);
}

// Returns false if playback could NOT be resumed, so the caller can leave
// the UI showing PLAY instead of flipping to PAUSE over silence.
//
// This used to be three lines that called AAudioStream_requestStart() and
// threw the result away. Plugging in a USB DAC changes the audio device,
// which kills the open stream -- so requestStart failed, nothing played,
// and the button label toggled anyway. From the outside the transport
// controls were simply dead until you picked a track from a list.
bool flast_stream_resume(void) {
    if (output_is_dead() && !reopen_output_only()) {
        return false;
    }

    atomic_store(&g_state.paused, false);
    aaudio_result_t r = AAudioStream_requestStart(g_state.aaudio_stream);
    if (r != AAUDIO_OK) {
        // The stream looked alive but would not start. One fresh attempt.
        LOGW("resume: requestStart failed (%s) -- reopening the output",
             AAudio_convertResultToText(r));
        if (reopen_output_only()) {
            r = AAudioStream_requestStart(g_state.aaudio_stream);
        }
    }
    if (r != AAUDIO_OK) {
        LOGE("resume: could not restart playback (%s)", AAudio_convertResultToText(r));
        atomic_store(&g_state.paused, true);
        return false;
    }
    return true;
}

// Bit-perfect is a LIVE property, not a fact recorded when the track
// opened. Another app taking the audio path, a device change, or a
// disconnect can all end it mid-track, and an indicator that keeps
// showing YES through any of that is exactly the dishonesty this project
// exists not to commit. Every condition is re-checked on demand.
// Returns WHICH condition failed, not merely that one did. The indicator
// is only useful if it can say why, and "the HAL refused exclusive mode"
// is a very different thing to tell a user than "your 5.1 file was folded
// into stereo" -- the first is their phone's fault and unfixable, the
// second is a property of the file they chose.
//
// Ordered most-specific first: a channel or rate conversion is the real
// explanation even though it also happens to force SHARED mode, so it is
// reported ahead of the generic exclusive-mode failure.
int flast_stream_bit_perfect_reason(void) {
    if (output_is_dead()) return BP_REASON_NOT_PLAYING;

    if ((uint32_t)AAudioStream_getChannelCount(g_state.aaudio_stream) != g_state.channels) {
        return BP_REASON_CHANNELS_CONVERTED;
    }
    if ((uint32_t)AAudioStream_getSampleRate(g_state.aaudio_stream) != g_state.sample_rate) {
        return BP_REASON_RATE_CONVERTED;
    }

    aaudio_format_t f = AAudioStream_getFormat(g_state.aaudio_stream);
    bool format_carries_everything =
        (f == AAUDIO_FORMAT_PCM_I32) ||
        (f == AAUDIO_FORMAT_PCM_I16 && g_state.bits_per_sample <= 16);
    if (!format_carries_everything) return BP_REASON_FORMAT_TOO_SMALL;

    if (AAudioStream_getSharingMode(g_state.aaudio_stream) != AAUDIO_SHARING_MODE_EXCLUSIVE) {
        return BP_REASON_NOT_EXCLUSIVE;
    }
    if (g_AAudioStream_isMMapUsed != NULL &&
        !g_AAudioStream_isMMapUsed(g_state.aaudio_stream)) {
        return BP_REASON_NOT_EXCLUSIVE;
    }
    return BP_REASON_OK;
}

bool flast_stream_is_bit_perfect_now(void) {
    return flast_stream_bit_perfect_reason() == BP_REASON_OK;
}

double flast_stream_get_position_seconds(void) {
    if (g_state.sample_rate == 0) {
        return 0.0;
    }
    uint64_t frames = atomic_load(&g_state.frames_played);
    return (double)frames / (double)g_state.sample_rate;
}

bool flast_stream_is_finished(void) {
    if (g_state.aaudio_stream == NULL) return false;
    if (!atomic_load(&g_state.decoding_finished)) return false;
    return atomic_load(&g_state.ring_buffer.available) == 0;
}
