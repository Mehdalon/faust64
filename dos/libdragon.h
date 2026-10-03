/* libdragon.h for DOS - the few libdragon drawing calls ui/faustgui.h uses,
 * on VGA mode 13h (320 x 200, 256 colours). With this, the N64 interface code
 * (ui/faustui.h + ui/faustgui.h) runs on DOS unchanged.
 *
 * Colours: graphics_make_color() returns 0x00RRGGBB; the first draw in a new
 * colour gives it the next free palette entry (the GUI uses ~20 colours). The
 * text font is the VGA BIOS 8x8 font, read once at start-up (dosgfx_init). */
#ifndef DOS_LIBDRAGON_H
#define DOS_LIBDRAGON_H
#include <stdint.h>
#include <string.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/movedata.h>

typedef struct { int width, height; uint8_t *buffer; } surface_t;

#define DOS_W 320
#define DOS_H 200
static uint8_t  dos__fb[DOS_W * DOS_H];
static surface_t dos__surf = { DOS_W, DOS_H, dos__fb };
static uint8_t  dos__font[256 * 8];
static uint32_t dos__pal[256];
static int      dos__npal = 0;
static uint32_t dos__fg = 0xFFFFFF, dos__bg = 0;

static uint32_t display_get_width(void)  { return DOS_W; }
static uint32_t display_get_height(void) { return DOS_H; }

static uint32_t graphics_make_color(int r, int g, int b, int a)
{
    (void)a;
    return ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) | (uint32_t)(b & 255);
}

/* palette index for a colour: an existing entry, a new one, or the nearest */
static uint8_t dos__index(uint32_t c)
{
    for (int i = 0; i < dos__npal; i++) if (dos__pal[i] == c) return (uint8_t)i;
    if (dos__npal < 256) {
        int i = dos__npal++;
        dos__pal[i] = c;
        outportb(0x3C8, i);                         /* VGA DAC: 6 bits a channel */
        outportb(0x3C9, (c >> 18) & 63); outportb(0x3C9, (c >> 10) & 63); outportb(0x3C9, (c >> 2) & 63);
        return (uint8_t)i;
    }
    int best = 0; long bd = 1L << 30;
    for (int i = 0; i < 256; i++) {
        long dr = (long)((dos__pal[i] >> 16) & 255) - (long)((c >> 16) & 255);
        long dg = (long)((dos__pal[i] >> 8) & 255) - (long)((c >> 8) & 255);
        long db = (long)(dos__pal[i] & 255) - (long)(c & 255);
        long d = dr * dr + dg * dg + db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return (uint8_t)best;
}

static void graphics_fill_screen(surface_t *s, uint32_t c)
{
    memset(s->buffer, dos__index(c), (size_t)s->width * s->height);
}

static void graphics_draw_box(surface_t *s, int x, int y, int w, int h, uint32_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->width)  w = s->width - x;
    if (y + h > s->height) h = s->height - y;
    if (w <= 0 || h <= 0) return;
    uint8_t k = dos__index(c);
    for (int j = 0; j < h; j++) memset(s->buffer + (size_t)(y + j) * s->width + x, k, (size_t)w);
}

static void dos__pset(surface_t *s, int x, int y, uint8_t k)
{
    if ((unsigned)x < (unsigned)s->width && (unsigned)y < (unsigned)s->height)
        s->buffer[(size_t)y * s->width + x] = k;
}

static void graphics_draw_line(surface_t *s, int x0, int y0, int x1, int y1, uint32_t c)
{
    uint8_t k = dos__index(c);
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy;
    for (;;) {
        dos__pset(s, x0, y0, k);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

static void graphics_set_color(uint32_t fg, uint32_t bg) { dos__fg = fg; dos__bg = bg; }

/* libdragon draws text with a transparent background unless bg is opaque; the
 * GUI always clears the area first, so only the foreground is drawn here */
static void graphics_draw_text(surface_t *s, int x, int y, const char *msg)
{
    uint8_t k = dos__index(dos__fg);
    for (; *msg; msg++, x += 8) {
        const uint8_t *g = dos__font + (uint8_t)*msg * 8;
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b++)
                if (g[r] & (0x80 >> b)) dos__pset(s, x + b, y + r, k);
    }
}

/* mode 13h, and the BIOS 8x8 font (int 10h AX=1130h BH=3 -> ES:BP) */
static void dosgfx_init(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r); r.x.ax = 0x0013; __dpmi_int(0x10, &r);
    memset(&r, 0, sizeof r); r.x.ax = 0x1130; r.h.bh = 3; __dpmi_int(0x10, &r);
    dosmemget(r.x.es * 16 + r.x.bp, 128 * 8, dos__font);
    memset(&r, 0, sizeof r); r.x.ax = 0x1130; r.h.bh = 4; __dpmi_int(0x10, &r);   /* chars 128..255 */
    dosmemget(r.x.es * 16 + r.x.bp, 128 * 8, dos__font + 128 * 8);
}

static void dosgfx_text_mode(void)
{
    __dpmi_regs r; memset(&r, 0, sizeof r); r.x.ax = 0x0003; __dpmi_int(0x10, &r);
}

/* copy the frame to the screen. No wait for the vertical retrace: that can
 * block for 14 ms, longer than the sound can wait. */
static void dosgfx_show(void)
{
    dosmemput(dos__fb, sizeof dos__fb, 0xA0000);
}
#endif
