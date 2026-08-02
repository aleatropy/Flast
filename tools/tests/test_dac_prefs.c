// DAC volume answers moved from Kotlin to C. The on-disk format must stay
// byte-compatible with what the Kotlin implementation wrote, or every user
// who already answered the question would be asked again.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "music_library.h"
static int fails=0;
#define CHECK(c,m,...) do{if(!(c)){printf("  FAIL: " m "\n",##__VA_ARGS__);fails++;}else printf("  ok:   " m "\n",##__VA_ARGS__);}while(0)
int main(void){
    const char *dir = getenv("FILESDIR");
    ml_init(dir);
    char path[512]; snprintf(path,sizeof path,"%s/dac_volume_prefs.txt",dir);
    unlink(path);

    printf("fresh device:\n");
    CHECK(ml_get_dac_answer(8369,12321)==-1, "unknown DAC -> -1 (ask the question)");

    printf("round trip:\n");
    CHECK(ml_set_dac_answer(8369,12321,true), "save YES");
    CHECK(ml_get_dac_answer(8369,12321)==1, "reads back as YES");
    CHECK(ml_set_dac_answer(8369,12321,false), "overwrite with NO");
    CHECK(ml_get_dac_answer(8369,12321)==0, "reads back as NO (not duplicated)");

    printf("several DACs:\n");
    ml_set_dac_answer(1234,5678,true);
    ml_set_dac_answer(1111,2222,false);
    CHECK(ml_get_dac_answer(8369,12321)==0, "first DAC still NO");
    CHECK(ml_get_dac_answer(1234,5678)==1,  "second DAC YES");
    CHECK(ml_get_dac_answer(1111,2222)==0,  "third DAC NO");
    CHECK(ml_get_dac_answer(9999,9999)==-1, "unseen DAC still unknown");

    printf("compatibility with a file written by the old Kotlin code:\n");
    // Kotlin wrote entries joined by "\n" with NO trailing newline.
    FILE *f=fopen(path,"wb");
    fputs("8369:12321:SI\n4242:1000:NO",f);
    fclose(f);
    CHECK(ml_get_dac_answer(8369,12321)==1, "legacy YES entry honoured");
    CHECK(ml_get_dac_answer(4242,1000)==0,  "legacy final line without newline honoured");
    CHECK(ml_get_dac_answer(1,2)==-1,       "absent device still unknown");

    printf("malformed input:\n");
    f=fopen(path,"wb"); fputs("garbage\n\n:::\n8369:12321:SI\n",f); fclose(f);
    CHECK(ml_get_dac_answer(8369,12321)==1, "junk lines skipped, real entry found");

    printf(fails?"\nFAILURES: %d\n":"\nALL PASS (%d failures)\n",fails);
    return fails?1:0;
}
