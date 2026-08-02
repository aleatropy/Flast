#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "music_library.h"
#define ALBUM_NAME_MAX 192
typedef struct { StrList library; StrList music_view; int music_offset; int musica_selected;
                 int musica_mode; char musica_current_album[ALBUM_NAME_MAX]; } AppState;
static AppState g_state;
#include "view_extract.inc"

static int fails = 0;
#define CHECK(c, m, ...) do { if(!(c)){printf("  FAIL: " m "\n", ##__VA_ARGS__); fails++;} else printf("  ok:   " m "\n", ##__VA_ARGS__);} while(0)

static const char *PATHS[] = {
    "/storage/emulated/0/Music/Artist/Blue Album/01 one.flac",
    "/storage/emulated/0/Music/Artist/Blue Album/02 two.flac",
    "/storage/emulated/0/Music/Artist/aqua album/03 three.flac",
    "/storage/emulated/0/Download/loose.flac",
    "/storage/emulated/0/Music/Artist/Blue Album/03 three.flac",
    "/rootfile.flac",
    "/storage/emulated/0/Music/DM/Songs Of Faith/Disc 1/01 a.flac",
    "/storage/emulated/0/Music/DM/Songs Of Faith/Disc 1/02 b.flac",
    "/storage/emulated/0/Music/DM/Songs Of Faith/Disc 2/01 c.flac",
    "/storage/emulated/0/Music/DM/Live Box/CD3/01 d.flac",
    "/storage/emulated/0/Music/DM/Odd/disk_04/01 e.flac",
    "/storage/emulated/0/Music/DM/CD Single/01 f.flac",
    "/storage/emulated/0/Music/DM/Disc/01 g.flac",
};
int main(void) {
    int n = sizeof PATHS / sizeof *PATHS;
    size_t tot = 0; for (int i=0;i<n;i++) tot += strlen(PATHS[i])+1;
    char *arena = malloc(tot); char **items = malloc(sizeof(char*)*n);
    size_t off=0; for (int i=0;i<n;i++){ size_t l=strlen(PATHS[i])+1; memcpy(arena+off,PATHS[i],l); items[i]=arena+off; off+=l; }
    g_state.library.arena=arena; g_state.library.items=items; g_state.library.count=n; g_state.library.owns_arena=true;

    char a[ALBUM_NAME_MAX];
    album_of(PATHS[0], a, sizeof a);  CHECK(!strcmp(a,"Blue Album"), "album_of nested -> '%s'", a);
    album_of(PATHS[3], a, sizeof a);  CHECK(!strcmp(a,"Download"),   "album_of one level -> '%s'", a);
    album_of(PATHS[5], a, sizeof a);  CHECK(!strcmp(a,"(unknown)"),  "album_of at fs root -> '%s'", a);
    album_of("noslash.flac", a, sizeof a); CHECK(!strcmp(a,"(unknown)"), "album_of no slash -> '%s'", a);

    printf("multi-disc folding:\n");
    album_of("/m/Artist/Songs Of Faith/Disc 1/01 a.flac", a, sizeof a);
    CHECK(!strcmp(a,"Songs Of Faith"), "'Disc 1' folds into the album -> '%s'", a);
    album_of("/m/Artist/Live Box/CD3/01 d.flac", a, sizeof a);
    CHECK(!strcmp(a,"Live Box"), "'CD3' folds -> '%s'", a);
    album_of("/m/Artist/Odd/disk_04/01 e.flac", a, sizeof a);
    CHECK(!strcmp(a,"Odd"), "'disk_04' folds -> '%s'", a);
    album_of("/m/Artist/CD Single/01 f.flac", a, sizeof a);
    CHECK(!strcmp(a,"CD Single"), "'CD Single' is a REAL album, not folded -> '%s'", a);
    album_of("/m/Artist/Disc/01 g.flac", a, sizeof a);
    CHECK(!strcmp(a,"Disc"), "bare 'Disc' is a REAL album, not folded -> '%s'", a);
    album_of("/m/Artist/Disc 2 Remastered/01 h.flac", a, sizeof a);
    CHECK(!strcmp(a,"Disc 2 Remastered"), "trailing text blocks folding -> '%s'", a);
    album_of("/Disc 1/01 i.flac", a, sizeof a);
    CHECK(!strcmp(a,"Disc 1"), "no grandparent to fold into -> '%s'", a);

    printf("mode 0 (all tracks):\n");
    set_music_mode_all();
    CHECK(music_count()==n, "shows all %d tracks", music_count());
    CHECK(music_list()==&g_state.library, "borrows the library, allocates nothing");
    CHECK(!strcmp(music_label_at(0),"01 one.flac"), "label is the filename -> '%s'", music_label_at(0));
    CHECK(music_label_at(0) > music_path_at(0), "label points INTO the path (no copy)");

    printf("mode 1 (albums):\n");
    build_album_list();
    CHECK(g_state.musica_mode==1, "mode is 1");
    CHECK(music_count()==9, "9 unique albums after folding (got %d)", music_count());
    for (int i=0;i<music_count();i++) printf("        [%d] %s\n", i, music_path_at(i));
    int sorted=1; for(int i=1;i<music_count();i++) if(strcasecmp(music_path_at(i-1),music_path_at(i))>0) sorted=0;
    CHECK(sorted, "albums sorted case-insensitively");
    CHECK(!strcmp(music_label_at(0), music_path_at(0)), "album label is the whole entry");

    printf("mode 2 (one album):\n");
    filter_music_by_album("Blue Album");
    CHECK(g_state.musica_mode==2, "mode is 2");
    CHECK(music_count()==3, "3 tracks in Blue Album (got %d)", music_count());
    CHECK(!g_state.music_view.owns_arena, "view borrows the library arena, copies no path text");
    int inside=1; for(int i=0;i<music_count();i++) if(music_path_at(i)<arena||music_path_at(i)>=arena+tot) inside=0;
    CHECK(inside, "every entry points into the library arena");
    CHECK(!strcmp(g_state.musica_current_album,"Blue Album"), "current album recorded");

    filter_music_by_album("aqua album");
    CHECK(music_count()==1, "1 track in 'aqua album' (got %d)", music_count());
    filter_music_by_album("Nonexistent");
    CHECK(music_count()==0, "unknown album -> empty, no crash");

    set_music_mode_all();
    CHECK(music_count()==n, "back to all tracks");
    ml_str_list_free(&g_state.music_view);
    ml_str_list_free(&g_state.library);
    printf(fails?"\nFAILURES: %d\n":"\nALL PASS (%d failures)\n", fails);
    return fails?1:0;
}
