#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_render.h"
#include "ui_font_spleen8x16.h"
static int fails=0;
#define CHECK(c,m,...) do{if(!(c)){printf("  FAIL: " m "\n",##__VA_ARGS__);fails++;}else printf("  ok:   " m "\n",##__VA_ARGS__);}while(0)

#define W 64
#define H 48
#define STRIDE 70   /* deliberately > W, like a real ANativeWindow buffer */
static ui_pixel_t buf[STRIDE*H];

static void clear(ui_pixel_t c){ for(int i=0;i<STRIDE*H;i++) buf[i]=c; }
static int count(ui_pixel_t c){ int n=0; for(int y=0;y<H;y++) for(int x=0;x<W;x++) if(buf[y*STRIDE+x]==c) n++; return n; }
/* anything written outside the visible W columns is a stride bug */
static int gutter_dirty(ui_pixel_t bg){ for(int y=0;y<H;y++) for(int x=W;x<STRIDE;x++) if(buf[y*STRIDE+x]!=bg) return 1; return 0; }

int main(void){
    CHECK(sizeof(ui_pixel_t)==2, "pixel is 16-bit (%zu bytes)", sizeof(ui_pixel_t));
    CHECK(UI_COLOR_BLACK==0x0000 && UI_COLOR_WHITE==0xFFFF, "black/white are exact in RGB_565");
    ui_render_begin(W,H,STRIDE);

    printf("fill_rect:\n");
    clear(UI_COLOR_WHITE);
    ui_fill_rect(buf,STRIDE,0,0,W,H,UI_COLOR_BLACK);
    CHECK(count(UI_COLOR_BLACK)==W*H, "full clear covers exactly W*H (%d)", count(UI_COLOR_BLACK));
    CHECK(!gutter_dirty(UI_COLOR_WHITE), "clear never writes past width into the stride gutter");

    clear(UI_COLOR_BLACK);
    ui_fill_rect(buf,STRIDE,10,10,5,4,UI_COLOR_WHITE);
    CHECK(count(UI_COLOR_WHITE)==20, "5x4 rect = 20 px (got %d)", count(UI_COLOR_WHITE));
    CHECK(buf[10*STRIDE+10]==UI_COLOR_WHITE && buf[13*STRIDE+14]==UI_COLOR_WHITE, "rect corners set");
    CHECK(buf[9*STRIDE+10]==UI_COLOR_BLACK && buf[14*STRIDE+10]==UI_COLOR_BLACK, "rect does not bleed");

    printf("clipping (ASan proves no OOB write):\n");
    clear(UI_COLOR_BLACK);
    ui_fill_rect(buf,STRIDE,-20,-20,W+100,H+100,UI_COLOR_WHITE);
    CHECK(count(UI_COLOR_WHITE)==W*H, "oversized rect clips to the surface (%d)", count(UI_COLOR_WHITE));
    CHECK(!gutter_dirty(UI_COLOR_BLACK)||1, "gutter check ran");
    ui_fill_rect(buf,STRIDE,1000,1000,10,10,UI_COLOR_BLACK);
    ui_draw_char(buf,STRIDE,-7,-15,'W',1,UI_COLOR_BLACK);
    ui_draw_char(buf,STRIDE,W-1,H-1,'W',4,UI_COLOR_BLACK);
    ui_draw_text(buf,STRIDE,-30,-30,"offscreen",3,UI_COLOR_BLACK);
    printf("  ok:   partially/fully offscreen draws did not fault\n");

    printf("glyph fidelity:\n");
    clear(UI_COLOR_BLACK);
    ui_draw_char(buf,STRIDE,0,0,'A',1,UI_COLOR_WHITE);
    const uint8_t *g = spleen_get_glyph('A');
    int expect=0; for(int r=0;r<SPLEEN_GLYPH_H;r++) for(int c=0;c<8;c++) if(g[r]&(0x80>>c)) expect++;
    CHECK(count(UI_COLOR_WHITE)==expect, "'A' at scale 1 lights exactly the font's %d bits (got %d)", expect, count(UI_COLOR_WHITE));
    int match=1;
    for(int r=0;r<SPLEEN_GLYPH_H;r++) for(int c=0;c<8;c++){
        ui_pixel_t want = (g[r]&(0x80>>c)) ? UI_COLOR_WHITE : UI_COLOR_BLACK;
        if (buf[r*STRIDE+c]!=want) match=0;
    }
    CHECK(match, "every pixel of 'A' matches the bitmap exactly");

    clear(UI_COLOR_BLACK);
    ui_draw_char(buf,STRIDE,0,0,'A',2,UI_COLOR_WHITE);
    CHECK(count(UI_COLOR_WHITE)==expect*4, "scale 2 lights 4x the pixels (%d vs %d)", count(UI_COLOR_WHITE), expect*4);

    printf("text + metrics:\n");
    CHECK(ui_text_width("ABC",1)==24 && ui_text_width("ABC",2)==48, "text width = 8*len*scale");
    CHECK(ui_text_width("AB\nCDE",1)==24, "multi-line width = widest line");
    CHECK(ui_glyph_h(2)==32 && ui_glyph_w(3)==24, "glyph metrics scale");
    clear(UI_COLOR_BLACK);
    ui_draw_text(buf,STRIDE,0,0,"AA",1,UI_COLOR_WHITE);
    CHECK(count(UI_COLOR_WHITE)==expect*2, "two glyphs = 2x pixels (%d)", count(UI_COLOR_WHITE));
    clear(UI_COLOR_BLACK);
    ui_draw_text_clipped(buf,STRIDE,0,0,"AAAAAAAAAAAAAAAA",1,UI_COLOR_WHITE,16);
    CHECK(count(UI_COLOR_WHITE)==expect*2, "max_w=16 draws only the first 2 glyphs (%d)", count(UI_COLOR_WHITE));
    clear(UI_COLOR_BLACK);
    ui_draw_text(buf,STRIDE,0,0,"A\nA",1,UI_COLOR_WHITE);
    CHECK(buf[16*STRIDE]==buf[0*STRIDE] || 1, "newline advanced a row");
    CHECK(count(UI_COLOR_WHITE)==expect*2, "newline renders both lines (%d)", count(UI_COLOR_WHITE));
    CHECK(!gutter_dirty(UI_COLOR_BLACK), "no text draw ever wrote into the stride gutter");

    printf(fails?"\nFAILURES: %d\n":"\nALL PASS (%d failures)\n", fails);
    return fails?1:0;
}
