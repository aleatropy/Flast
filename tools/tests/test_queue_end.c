// Exercises playback_controller.c's real queue logic with the audio layer
// stubbed out. The behaviour under test is what happens when an album or
// playlist runs off its last track: playback stops, the queue rewinds to
// the first track WITHOUT starting it, and the transport controls behave
// sensibly from that resting point.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include "playback_controller.h"

static int fails = 0;
#define CHECK(c,m,...) do{ if(!(c)){printf("  FAIL: " m "\n",##__VA_ARGS__);fails++;} \
                            else printf("  ok:   " m "\n",##__VA_ARGS__);}while(0)

/* ---- stubbed audio layer ---- */
static int g_played_index = -1;
static const char *g_last_path = NULL;
int flast_stream_play(const char *p, bool *bp, int32_t *sr, int32_t *b) {
    g_last_path = p; *bp = true; *sr = 44100; *b = 16; return 0;
}
void flast_stream_stop(void) {}
void flast_stream_pause(void) {}
bool flast_stream_resume(void) { return true; }
double flast_stream_get_position_seconds(void) { return 0.0; }
bool flast_stream_is_bit_perfect_now(void) { return true; }
int flast_stream_bit_perfect_reason(void) { return 0; }

static StrList make_queue(int n) {
    StrList q; memset(&q, 0, sizeof q);
    size_t each = 24;
    q.arena = malloc(each * n);
    q.items = malloc(sizeof(char*) * n);
    for (int i = 0; i < n; i++) {
        q.items[i] = q.arena + i * each;
        snprintf(q.items[i], each, "/t%d.flac", i);
    }
    q.count = n; q.owns_arena = true;
    return q;
}
static int idx(void) { PlaybackSnapshot s; pc_get_snapshot(&s); return s.current_index; }
static bool playing(void) { PlaybackSnapshot s; pc_get_snapshot(&s); return s.is_playing; }
static void settle(void) { usleep(350000); pc_tick(); }

int main(void) {
    (void)g_played_index;
    pc_init();
    StrList q = make_queue(3);
    pc_set_queue(&q, 0);
    pc_play_index(0);
    CHECK(idx()==0 && playing(), "playing track 0 of 3");

    printf("advancing through the queue:\n");
    pc_on_track_finished();  CHECK(idx()==1, "track 0 ends -> track 1");
    pc_on_track_finished();  CHECK(idx()==2, "track 1 ends -> track 2 (last)");

    printf("running off the end:\n");
    pc_on_track_finished();
    CHECK(!playing(), "playback stops");
    CHECK(idx()==0, "rewinds to the FIRST track (was staying on the last)");

    printf("transport from the resting point:\n");
    pc_request_next(); settle();
    CHECK(idx()==0, "NEXT goes to the first track, not the second");
    CHECK(playing(), "and starts playing it");

    // back to the resting point
    pc_on_track_finished(); pc_on_track_finished(); pc_on_track_finished();
    CHECK(idx()==0 && !playing(), "back at the resting point");
    pc_request_prev(); settle();
    CHECK(idx()==2, "PREV goes to the LAST track");

    // resting point again, then press play
    pc_on_track_finished();
    CHECK(idx()==0 && !playing(), "at rest after the last track");
    pc_toggle_play_pause();
    CHECK(idx()==0 && playing(), "PLAY starts the first track");

    printf("normal navigation is unaffected:\n");
    pc_request_next(); settle();
    CHECK(idx()==1, "NEXT from track 0 -> track 1");
    pc_request_next(); settle();
    CHECK(idx()==2, "NEXT from track 1 -> track 2");

    printf("single-track queue:\n");
    StrList q1 = make_queue(1);
    pc_set_queue(&q1, 0);
    pc_play_index(0);
    pc_on_track_finished();
    CHECK(idx()==0 && !playing(), "one track ends -> rests on itself, stopped");

    printf(fails?"\nFAILURES: %d\n":"\nALL PASS (%d failures)\n", fails);
    return fails ? 1 : 0;
}
