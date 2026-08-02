// Host harness for music_library.c: builds a fake storage tree, points
// the scanner at it, and checks the results. Compiled with -DML_TEST so
// music_library.c uses the injected roots instead of /storage.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <sys/stat.h>
#include "music_library.h"

static int fails = 0;
#define CHECK(cond, msg, ...) do { if (!(cond)) { printf("  FAIL: " msg "\n", ##__VA_ARGS__); fails++; } else { printf("  ok:   " msg "\n", ##__VA_ARGS__); } } while (0)


static void touch(const char *p) { FILE *f = fopen(p, "wb"); if (f) { fputs("x", f); fclose(f); } }

int main(void) {
    const char *base = getenv("TESTDIR");
    char b[512], files[512];
    snprintf(b, sizeof b, "%s/storage", base);
    snprintf(files, sizeof files, "%s/files", base);

    char p[1024];
    #define MK(sub) do { snprintf(p, sizeof p, "%s/%s", b, sub); mkdir(p, 0755); } while(0)
    #define TF(sub) do { snprintf(p, sizeof p, "%s/%s", b, sub); touch(p); } while(0)
    mkdir(base, 0755); mkdir(b, 0755); mkdir(files, 0755);
    MK("Music"); MK("Music/Album B"); MK("Music/album a"); MK("Download");
    MK("Android"); MK("Android/data"); MK("Android/data/com.evil"); MK("Android/media");
    MK(".hidden"); MK("LOST.DIR"); MK("DCIM"); MK("DCIM/.thumbnails");
    TF("Music/Album B/02 second.flac");
    TF("Music/Album B/01 first.flac");
    TF("Music/album a/track.FLAC");          // case-insensitive extension
    TF("Music/loose.flac");
    TF("Download/from-web.flac");
    TF("Android/media/podcast.flac");        // Android/media IS scanned
    TF("Android/data/com.evil/hidden.flac"); // sandbox: must be skipped
    TF(".hidden/nope.flac");                 // hidden dir: must be skipped
    TF("LOST.DIR/nope.flac");
    TF("DCIM/.thumbnails/nope.flac");
    TF("Music/notmusic.txt");
    TF("Music/flac");                        // bare name, not an extension
    TF("Music/.flac");                       // hidden file named .flac

    // symlink loop: the old Kotlin walk followed these
    snprintf(p, sizeof p, "%s/Music/loop", b);
    char tgt[512]; snprintf(tgt, sizeof tgt, "%s", b);
    if (symlink(tgt, p) != 0) printf("  (symlink not created)\n");

    ml_init(files);

    printf("scan:\n");
    StrList lib;
    CHECK(ml_scan(&lib), "ml_scan returns true");
    for (int i = 0; i < lib.count; i++) printf("        %s\n", lib.items[i]);
    CHECK(lib.count == 7, "found 7 tracks (got %d)", lib.count);

    int sorted = 1;
    for (int i = 1; i < lib.count; i++) if (strcasecmp(lib.items[i-1], lib.items[i]) > 0) sorted = 0;
    CHECK(sorted, "sorted case-insensitively");

    int leaked = 0;
    for (int i = 0; i < lib.count; i++) {
        if (strstr(lib.items[i], "Android/data")) leaked = 1;
        if (strstr(lib.items[i], ".hidden")) leaked = 1;
        if (strstr(lib.items[i], "thumbnails")) leaked = 1;
        if (strstr(lib.items[i], "/loop/")) leaked = 1;
    }
    CHECK(!leaked, "skipped Android/data, hidden dirs, .thumbnails and the symlink loop");
    CHECK(strstr(lib.items[0], "Android/media") != NULL, "Android/media IS scanned");

    printf("cache:\n");
    StrList cached;
    CHECK(ml_load_cache(&cached), "cache written and reloadable");
    CHECK(cached.count == lib.count, "cache has same count (%d vs %d)", cached.count, lib.count);
    int same = (cached.count == lib.count);
    for (int i = 0; same && i < cached.count; i++) if (strcmp(cached.items[i], lib.items[i])) same = 0;
    CHECK(same, "cache round-trips every path exactly");
    ml_str_list_free(&cached);

    printf("playlists:\n");
    char n1[128], n2[128];
    CHECK(ml_create_auto_playlist(n1, sizeof n1), "create playlist 1");
    CHECK(strcmp(n1, "Playlist 1") == 0, "first is 'Playlist 1' (got '%s')", n1);
    CHECK(ml_create_auto_playlist(n2, sizeof n2), "create playlist 2");
    CHECK(strcmp(n2, "Playlist 2") == 0, "second is 'Playlist 2' (got '%s')", n2);

    ml_add_to_playlist(n1, lib.items[0]);
    ml_add_to_playlist(n1, lib.items[1]);
    ml_add_to_playlist(n1, lib.items[0]); // duplicate: ignored
    StrList pl;
    CHECK(ml_load_playlist(n1, &pl), "load playlist");
    CHECK(pl.count == 2, "2 entries, duplicate ignored (got %d)", pl.count);
    CHECK(pl.count == 2 && strcmp(pl.items[0], lib.items[0]) == 0, "insertion order preserved");
    ml_str_list_free(&pl);

    StrList names;
    CHECK(ml_list_playlists(&names), "list playlists");
    CHECK(names.count == 2, "2 playlists listed (got %d)", names.count);
    ml_str_list_free(&names);

    ml_delete_playlist(n1);
    CHECK(ml_list_playlists(&names) && names.count == 1, "delete removes one");
    ml_str_list_free(&names);

    CHECK(!ml_load_playlist("../../../etc/passwd", &pl), "path traversal rejected");
    CHECK(!ml_load_playlist("a/b", &pl), "slash in name rejected");

    ml_str_list_free(&lib);
    printf(fails ? "\nFAILURES: %d\n" : "\nALL PASS (%d failures)\n", fails);
    return fails ? 1 : 0;
}
