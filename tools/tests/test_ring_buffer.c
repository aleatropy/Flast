#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
typedef int32_t aaudio_format_t;
#define AAUDIO_FORMAT_PCM_I16  1
#define AAUDIO_FORMAT_PCM_FLOAT 2
#define AAUDIO_FORMAT_PCM_I32  4
#define RING_BUFFER_MS 300
#include "ring_extract.inc"
static int fails=0;
#define CHECK(c,m,...) do{if(!(c)){printf("  FAIL: " m "\n",##__VA_ARGS__);fails++;}else printf("  ok:   " m "\n",##__VA_ARGS__);}while(0)

#define TOTAL 4000000   /* ~45s of 44.1k stereo, streamed through the ring */
static RingBuffer rb;
static atomic_bool producer_done = false;

static void *producer(void *a){
    (void)a; int32_t chunk[512];
    size_t sent=0;
    while (sent < TOTAL) {
        size_t n = TOTAL - sent; if (n > 512) n = 512;
        for (size_t i=0;i<n;i++) chunk[i] = (int32_t)((sent+i) * 2654435761u);
        size_t w=0;
        while (w<n) { size_t t=ring_buffer_write(&rb, chunk+w, n-w); if(!t){struct timespec ts={0,200000};nanosleep(&ts,NULL);} w+=t; }
        sent += n;
    }
    atomic_store(&producer_done,true); return NULL;
}

int main(void){
    printf("sizing:\n");
    CHECK(ring_buffer_init(&rb,44100,2), "init 44.1k stereo");
    CHECK(rb.capacity==26460, "300ms of 44.1k stereo = 26460 samples (got %zu)", rb.capacity);
    printf("        -> %zu KB ring (the old fixed 65536-sample floor was %zu KB)\n",
           rb.capacity*4/1024, (size_t)65536*4/1024);
    ring_buffer_destroy(&rb);
    CHECK(ring_buffer_init(&rb,8000,1) && rb.capacity>=4096, "8kHz mono clamped to the floor (%zu)", rb.capacity);
    ring_buffer_destroy(&rb);

    printf("conversion (bit-exactness):\n");
    ring_buffer_init(&rb,44100,2);
    int32_t src16[4]; int16_t orig[4]={-32768,-1,0,32767};
    for(int i=0;i<4;i++) src16[i]=(int32_t)((uint32_t)orig[i]<<16);   /* what write_callback does for 16-bit */
    ring_buffer_write(&rb,src16,4);
    int16_t out16[4]; ring_buffer_read(&rb,out16,4,AAUDIO_FORMAT_PCM_I16);
    CHECK(!memcmp(out16,orig,sizeof orig), "16-bit source survives <<16 then >>16 exactly");
    int32_t src32[3]={INT32_MIN,0,INT32_MAX}; int32_t out32[3];
    ring_buffer_write(&rb,src32,3); ring_buffer_read(&rb,out32,3,AAUDIO_FORMAT_PCM_I32);
    CHECK(!memcmp(out32,src32,sizeof src32), "I32 passthrough is byte-identical");
    float outf[3]; ring_buffer_write(&rb,src32,3); ring_buffer_read(&rb,outf,3,AAUDIO_FORMAT_PCM_FLOAT);
    CHECK(outf[0]==-1.0f && outf[1]==0.0f && outf[2]>0.99999f && outf[2]<=1.0f, "FLOAT maps full scale onto [-1,1] (%.9f)", (double)outf[2]);
    ring_buffer_destroy(&rb);

    printf("threaded stream (SPSC, no lock):\n");
    ring_buffer_init(&rb,44100,2);
    pthread_t t; pthread_create(&t,NULL,producer,NULL);
    int32_t out[977];               /* deliberately not a divisor of anything */
    size_t got=0, mismatches=0, wraps=0;
    while (got < TOTAL) {
        size_t n = ring_buffer_read(&rb,out,977,AAUDIO_FORMAT_PCM_I32);
        if (n==0){ if(atomic_load(&producer_done) && atomic_load(&rb.available)==0) break;
                   struct timespec ts={0,200000}; nanosleep(&ts,NULL); continue; }
        for (size_t i=0;i<n;i++) if (out[i] != (int32_t)((got+i)*2654435761u)) mismatches++;
        got += n; wraps++;
    }
    pthread_join(t,NULL);
    CHECK(got==TOTAL, "consumed every sample: %zu of %d", got, TOTAL);
    CHECK(mismatches==0, "%zu samples, 0 corrupted, over %zu read calls", got, wraps);
    CHECK(atomic_load(&rb.available)==0, "ring drains to empty");
    ring_buffer_destroy(&rb);
    printf(fails?"\nFAILURES: %d\n":"\nALL PASS (%d failures)\n", fails);
    return fails?1:0;
}
