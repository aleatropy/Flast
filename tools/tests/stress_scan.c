#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include "music_library.h"
int main(void) {
    struct timespec t0, t1;
    ml_init(getenv("FILESDIR"));
    clock_gettime(CLOCK_MONOTONIC, &t0);
    StrList lib;
    if (!ml_scan(&lib)) { printf("scan failed\n"); return 1; }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec-t0.tv_sec)*1000.0 + (t1.tv_nsec-t0.tv_nsec)/1e6;
    struct rusage ru; getrusage(RUSAGE_SELF, &ru);
    size_t text = 0; for (int i=0;i<lib.count;i++) text += strlen(lib.items[i])+1;
    printf("scan   : %d tracks in %.0f ms\n", lib.count, ms);
    printf("memory : arena %zu KB + index %zu KB = %zu KB for the whole library\n",
           text/1024, (size_t)lib.count*sizeof(char*)/1024, (text + (size_t)lib.count*sizeof(char*))/1024);
    printf("peakRSS: %ld KB (process total, includes the harness)\n", ru.ru_maxrss);
    // deepest path reached -> confirms the depth cap engaged, not a crash
    int deepest = 0; const char *dp = "";
    for (int i=0;i<lib.count;i++){int d=0;for(const char*c=lib.items[i];*c;c++)if(*c=='/')d++;if(d>deepest){deepest=d;dp=lib.items[i];}}
    printf("deepest: %d path components -> %s\n", deepest, dp);
    ml_str_list_free(&lib);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    StrList c; ml_load_cache(&c);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("cache  : %d paths reloaded in %.1f ms\n", c.count,
           (t1.tv_sec-t0.tv_sec)*1000.0 + (t1.tv_nsec-t0.tv_nsec)/1e6);
    ml_str_list_free(&c);
    return 0;
}
