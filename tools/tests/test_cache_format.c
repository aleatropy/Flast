#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "music_library.h"
static int fails=0;
#define CHECK(c,m,...) do{if(!(c)){printf("  FAIL: " m "\n",##__VA_ARGS__);fails++;}else printf("  ok:   " m "\n",##__VA_ARGS__);}while(0)
static long fsize(const char*p){struct stat s; return stat(p,&s)==0?(long)s.st_size:-1;}
int main(void){
    const char *fd = getenv("FILESDIR");
    char cache[512]; snprintf(cache,sizeof cache,"%s/music_scan_cache.txt",fd);
    ml_init(fd);
    StrList lib;
    if(!ml_scan(&lib)){printf("scan failed\n");return 1;}
    long plain=0; for(int i=0;i<lib.count;i++) plain += strlen(lib.items[i])+1;
    long on_disk = fsize(cache);
    printf("  %d tracks: %ld bytes as plain paths, %ld bytes front-coded (%.0f%% smaller)\n",
           lib.count, plain, on_disk, 100.0*(plain-on_disk)/plain);
    StrList c; CHECK(ml_load_cache(&c), "front-coded cache reloads");
    CHECK(c.count==lib.count, "same count (%d vs %d)", c.count, lib.count);
    int same=(c.count==lib.count); for(int i=0;same&&i<c.count;i++) if(strcmp(c.items[i],lib.items[i])) same=0;
    CHECK(same, "every path round-trips byte-identically");
    ml_str_list_free(&c);

    printf("fail-safe behaviour:\n");
    FILE*f=fopen(cache,"wb"); fputs("/some/old/style/path.flac\n/another.flac\n",f); fclose(f);
    CHECK(!ml_load_cache(&c), "a v1.0 plain-path cache is rejected (triggers rescan)");
    f=fopen(cache,"wb"); fputs("FLASTCACHE2\n0\t/a/b.flac\n999\tzz.flac\n",f); fclose(f);
    CHECK(!ml_load_cache(&c), "impossible shared-prefix count is rejected");
    f=fopen(cache,"wb"); fputs("FLASTCACHE2\nnotanumber\tx.flac\n",f); fclose(f);
    CHECK(!ml_load_cache(&c), "non-numeric prefix field is rejected");
    f=fopen(cache,"wb"); fputs("FLASTCACHE2\n0\t/a/b.flac\nno-tab-here\n",f); fclose(f);
    CHECK(!ml_load_cache(&c), "missing tab separator is rejected");
    f=fopen(cache,"wb"); fputs("FLASTCACHE2\n",f); fclose(f);
    CHECK(!ml_load_cache(&c), "empty (but valid) cache reports empty, not a bogus entry");
    ml_str_list_free(&lib);
    printf(fails?"\nFAILURES: %d\n":"\nALL PASS (%d failures)\n", fails);
    return fails?1:0;
}
