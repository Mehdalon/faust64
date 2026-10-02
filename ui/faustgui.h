/* faustgui.h - draw a Faust UI on the N64 as an actual interface: faders,
 * knobs, buttons, checkboxes and meters, laid out from the group tree the DSP
 * itself declares. Nothing here is per-DSP; add a slider to a .dsp and a fader
 * appears, mark it [style:knob] and it becomes a knob.
 *
 * Layout follows Faust's own semantics: a vgroup stacks its children
 * vertically, an hgroup places them side by side, a tab box shows one child at
 * a time. The one liberty taken is that a vgroup which would run off the bottom
 * of a 240-line screen wraps into a second column instead of being clipped.
 *
 * Include after faustui.h. Needs libdragon's graphics.h. */
#ifndef FAUSTGUI_H
#define FAUSTGUI_H

#include <libdragon.h>
#include "faustui.h"

/* ---- theme --------------------------------------------------------------- */
typedef struct {
    uint32_t bg, panel, line, text, dim, accent, track, fill, hot, meter, warn;
    uint32_t bevel_hi, bevel_lo, screw, led_off, amber;
} fgui_theme;

static fgui_theme fgui_t_default(void)
{
    fgui_theme t;
    t.bg     = graphics_make_color( 16,  18,  24, 255);
    t.panel  = graphics_make_color( 28,  31,  40, 255);
    t.line   = graphics_make_color( 54,  59,  74, 255);
    t.text   = graphics_make_color(226, 230, 240, 255);
    t.dim    = graphics_make_color(132, 140, 160, 255);
    t.accent = graphics_make_color(127, 209, 185, 255);
    t.track  = graphics_make_color( 44,  48,  61, 255);
    t.fill   = graphics_make_color( 96, 176, 158, 255);
    t.hot    = graphics_make_color(255, 214, 102, 255);
    t.meter  = graphics_make_color(120, 220, 140, 255);
    t.warn   = graphics_make_color(232, 108,  76, 255);
    /* a rack panel reads as metal because of the bevel, not the colour */
    t.bevel_hi = graphics_make_color( 74,  80,  96, 255);
    t.bevel_lo = graphics_make_color(  8,   9,  13, 255);
    t.screw    = graphics_make_color( 96, 102, 118, 255);
    t.led_off  = graphics_make_color( 34,  38,  48, 255);
    t.amber    = graphics_make_color(255, 176,  64, 255);
    return t;
}

/* ---- small drawing helpers ---------------------------------------------- */
#define FGUI_CH 8                      /* built-in font cell */
#define FGUI_CW 8
/* A CRT does not show the whole framebuffer. Everything is inset by this much
 * so nothing important lands in the overscan; measured against ares, which
 * shows more than a real TV will. */
#define FGUI_MX 14
#define FGUI_MY 6

static void fgui__frame(surface_t *s, int x, int y, int w, int h, uint32_t c)
{
    if (w <= 0 || h <= 0) return;
    graphics_draw_line(s, x,         y,         x + w - 1, y,         c);
    graphics_draw_line(s, x,         y + h - 1, x + w - 1, y + h - 1, c);
    graphics_draw_line(s, x,         y,         x,         y + h - 1, c);
    graphics_draw_line(s, x + w - 1, y,         x + w - 1, y + h - 1, c);
}

/* A raised bevel: light on the top and left, dark on the bottom and right.
 * Two lines is all it takes to stop a flat rectangle looking like a flat
 * rectangle. */
static void fgui__bevel(surface_t *s, int x, int y, int w, int h,
                        uint32_t hi, uint32_t lo)
{
    if (w <= 1 || h <= 1) return;
    graphics_draw_line(s, x, y, x + w - 2, y, hi);
    graphics_draw_line(s, x, y, x, y + h - 2, hi);
    graphics_draw_line(s, x + w - 1, y + 1, x + w - 1, y + h - 1, lo);
    graphics_draw_line(s, x + 1, y + h - 1, x + w - 1, y + h - 1, lo);
}

/* sunken: the same trick inverted, for anything a control sits inside */
static void fgui__inset(surface_t *s, int x, int y, int w, int h,
                        uint32_t hi, uint32_t lo)
{
    fgui__bevel(s, x, y, w, h, lo, hi);
}

static void fgui__screw(surface_t *s, int cx, int cy, uint32_t c, uint32_t dark)
{
    graphics_draw_box(s, cx - 2, cy - 2, 5, 5, c);
    graphics_draw_line(s, cx - 1, cy, cx + 1, cy, dark);   /* the slot */
}

static void fgui__disc(surface_t *s, int cx, int cy, int r, uint32_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int span = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        if (span > 0)
            graphics_draw_line(s, cx - span, cy + dy, cx + span, cy + dy, c);
    }
}

static void fgui__ring(surface_t *s, int cx, int cy, int r, uint32_t c)
{
    int px = 0, py = 0;
    for (int a = 0; a <= 32; a++) {
        float th = (float)a * (2.0f * 3.14159265f / 32.0f);
        int x = cx + (int)(cosf(th) * r + 0.5f);
        int y = cy + (int)(sinf(th) * r + 0.5f);
        if (a) graphics_draw_line(s, px, py, x, y, c);
        px = x; py = y;
    }
}

/* Faust's own arc convention: a knob sweeps 270 degrees, from lower-left
 * round to lower-right, so the dead zone sits at the bottom. */
static void fgui__knob_pointer(surface_t *s, int cx, int cy, int r, float t, uint32_t c)
{
    float th = (135.0f + t * 270.0f) * 3.14159265f / 180.0f;
    int x = cx + (int)(cosf(th) * (r - 2) + 0.5f);
    int y = cy + (int)(sinf(th) * (r - 2) + 0.5f);
    graphics_draw_line(s, cx, cy, x, y, c);
    graphics_draw_line(s, cx + 1, cy, x, y, c);
}

static void fgui__text(surface_t *s, int x, int y, uint32_t fg, uint32_t bg, const char *msg)
{
    graphics_set_color(fg, bg);
    graphics_draw_text(s, x, y, msg);
}

/* value -> 0..1, honouring [scale:log] so a log fader looks log */
static float fgui__norm(const fui_item *it)
{
    float v = *it->zone;
    if (it->hi <= it->lo) return 0.0f;
    float t;
    if (it->logscale && it->lo > 0.0f && v > 0.0f)
        t = logf(v / it->lo) / logf(it->hi / it->lo);
    else
        t = (v - it->lo) / (it->hi - it->lo);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t;
}

static void fgui__label(const fui_item *it, char *buf, int cap)
{
    const char *l = it->label ? it->label : "";
    int i = 0;
    for (; l[i] && l[i] != '[' && i < cap - 1; i++) buf[i] = l[i];
    while (i > 0 && buf[i - 1] == ' ') i--;
    buf[i] = 0;
}

/* ---- natural sizes ------------------------------------------------------- */
static void fgui__size(const fui_item *it, int *w, int *h)
{
    switch (it->kind) {
    case FUI_HSLIDER: case FUI_NENTRY: *w = 292; *h = 21; break;
    case FUI_VSLIDER:                  *w =  30; *h = 76; break;
    case FUI_BUTTON: case FUI_CHECK:   *w =  92; *h = 18; break;
    case FUI_HBAR:                     *w = 292; *h = 16; break;
    case FUI_VBAR:                     *w =  20; *h = 64; break;
    default:                           *w =  80; *h = 18; break;
    }
    if (it->knob) { *w = 54; *h = 44; }   /* wide enough for a 6-char label */
}

/* ---- layout -------------------------------------------------------------- */
/* Returns the index just past the subtree starting at i, and reports the size
 * it wants through the ow and oh out-parameters. Two passes: measure, then place. */
static int fgui__measure(fui_t *u, int i, int *ow, int *oh, int availh)
{
    fui_item *it = &u->it[i];
    if (it->kind == FUI_VGROUP || it->kind == FUI_HGROUP || it->kind == FUI_TGROUP) {
        int horiz = (it->kind == FUI_HGROUP);
        int head  = (it->kind == FUI_TGROUP) ? 0
                  : (it->label && it->label[0] && it->label[0] != '0') ? FGUI_CH + 2 : 0;
        int j = i + 1, w = 0, h = 0, colw = 0, colh = 0;
        while (j < u->n && u->it[j].kind != FUI_CLOSE) {
            int cw, ch;
            j = fgui__measure(u, j, &cw, &ch, availh - head);
            if (cw == 0 && ch == 0) continue;               /* hidden, or another tab */
            if (horiz) {
                w += cw + 2;
                if (ch > h) h = ch;
            } else {
                colh += ch;
                if (cw > colw) colw = cw;
                if (colh > h) h = colh;
            }
        }
        if (!horiz) w += colw;
        *ow = w; *oh = h + head;
        return (j < u->n) ? j + 1 : j;
    }
    if (it->kind == FUI_CLOSE) { *ow = 0; *oh = 0; return i + 1; }
    if (it->hidden || (it->tab >= 0 && it->tab != u->opentab)) { *ow = 0; *oh = 0; return i + 1; }
    fgui__size(it, ow, oh);
    return i + 1;
}

static int fgui__place(fui_t *u, int i, int x, int y, int availh)
{
    fui_item *it = &u->it[i];
    if (it->kind == FUI_VGROUP || it->kind == FUI_HGROUP || it->kind == FUI_TGROUP) {
        int horiz = (it->kind == FUI_HGROUP);
        int head  = (it->kind == FUI_TGROUP) ? 0
                  : (it->label && it->label[0] && it->label[0] != '0') ? FGUI_CH + 2 : 0;
        it->x = x; it->y = y; it->w = 0; it->h = head;
        int j = i + 1, cx = x, cy = y + head, colw = 0;
        while (j < u->n && u->it[j].kind != FUI_CLOSE) {
            int cw, ch;
            fgui__measure(u, j, &cw, &ch, availh - head);
            if (cw == 0 && ch == 0) { j = fgui__place(u, j, cx, cy, availh - head); continue; }
            if (horiz) {
                j = fgui__place(u, j, cx, cy, availh - head);
                cx += cw + 2;
            } else {
                j = fgui__place(u, j, cx, cy, availh - head);
                cy += ch;
                if (cw > colw) colw = cw;
            }
        }
        it->w = (horiz ? cx - x : cx + colw - x);
        it->h = availh;
        return (j < u->n) ? j + 1 : j;
    }
    if (it->kind == FUI_CLOSE) return i + 1;
    if (it->hidden || (it->tab >= 0 && it->tab != u->opentab)) {
        it->w = it->h = 0;
        return i + 1;
    }
    int w, h;
    fgui__size(it, &w, &h);
    it->x = x; it->y = y; it->w = w; it->h = h;
    return i + 1;
}

static void fgui_layout(fui_t *u, int x, int y, int w, int h)
{
    (void)w;
    int i = 0;
    while (i < u->n) i = fgui__place(u, i, x, y, h);
}

/* ---- widget drawing ------------------------------------------------------ */
/* An LED ladder, not a bar: discrete segments, green into amber into red.
 * This is the single thing that makes a meter look like hardware rather than
 * like a progress bar. */
static void fgui__ladder(surface_t *s, const fgui_theme *th, int x, int y,
                         int w, int h, float t, int segs)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int lit = (int)(t * segs + 0.5f);
    int sw = w / segs;
    if (sw < 2) sw = 2;
    graphics_draw_box(s, x - 1, y - 1, segs * sw + 2, h + 2, th->bg);
    for (int k = 0; k < segs; k++) {
        uint32_t c = th->led_off;
        if (k < lit) {
            float f = (float)k / (float)segs;
            c = (f > 0.88f) ? th->warn : (f > 0.70f) ? th->amber : th->meter;
        }
        graphics_draw_box(s, x + k * sw, y, sw - 1, h, c);
    }
    fgui__inset(s, x - 1, y - 1, segs * sw + 2, h + 2, th->bevel_hi, th->bevel_lo);
}

static void fgui__draw_item(surface_t *s, const fgui_theme *th, const fui_item *it,
                            int selected)
{
    char lab[20], val[FUI_FMT_MAX];
    fgui__label(it, lab, sizeof lab);
    uint32_t nameC = selected ? th->hot : th->dim;
    uint32_t lineC = selected ? th->hot : th->line;

    if (it->kind == FUI_HBAR || it->kind == FUI_VBAR) {
        float t = fgui__norm(it);
        if (it->kind == FUI_HBAR) {
            const int NAMEW = 9 * FGUI_CW;
            int bw = it->w - NAMEW;
            lab[9] = 0;
            fgui__text(s, it->x, it->y + 4, th->dim, th->panel, lab);
            fgui__ladder(s, th, it->x + NAMEW, it->y + 4, bw, 8, t, 20);
        } else {
            int bh = it->h - 10, segs = 10, sh = bh / segs;
            int lit = (int)(t * segs + 0.5f);
            for (int k = 0; k < segs; k++) {
                float f = (float)k / segs;
                uint32_t c = (k < lit) ? ((f > 0.88f) ? th->warn
                                        : (f > 0.70f) ? th->amber : th->meter)
                                       : th->led_off;
                graphics_draw_box(s, it->x + 4, it->y + bh - (k + 1) * sh, 10, sh - 1, c);
            }
            fgui__inset(s, it->x + 3, it->y - 1, 12, bh + 2, th->bevel_hi, th->bevel_lo);
        }
        return;
    }

    if (it->kind == FUI_BUTTON && it->pad) {
        /* hardware-driven: a lamp, not a control - you press the real button */
        int on = (*it->zone > 0.5f);
        graphics_draw_box(s, it->x, it->y + 3, 12, 11, on ? th->amber : th->led_off);
        fgui__inset(s, it->x, it->y + 3, 12, 11, th->bevel_hi, th->bevel_lo);
        lab[9] = 0;
        fgui__text(s, it->x + 16, it->y + 4, on ? th->hot : th->dim, th->panel, lab);
        return;
    }

    if (it->kind == FUI_BUTTON || it->kind == FUI_CHECK) {
        int on = (*it->zone > 0.5f);
        int bx = it->x, by = it->y + 2, bw = 14, bh = 12;
        graphics_draw_box(s, bx, by, bw, bh, on ? th->accent : th->led_off);
        fgui__inset(s, bx, by, bw, bh, th->bevel_hi, th->bevel_lo);
        if (selected) fgui__frame(s, bx - 2, by - 2, bw + 4, bh + 4, th->hot);
        if (on && it->kind == FUI_CHECK) {
            graphics_draw_line(s, bx + 3, by + 6, bx + 6, by + 9, th->bg);
            graphics_draw_line(s, bx + 6, by + 9, bx + 11, by + 3, th->bg);
        }
        fgui__text(s, bx + bw + 4, it->y + 3, selected ? th->hot : th->text, th->panel, lab);
        return;
    }

    if (it->knob) {
        int r = 13, cx = it->x + 23, cy = it->y + 15;
        /* tick marks first, so the cap sits on top of them */
        for (int k = 0; k <= 6; k++) {
            float ang = (135.0f + (k / 6.0f) * 270.0f) * 3.14159265f / 180.0f;
            int x0 = cx + (int)(cosf(ang) * (r + 4) + 0.5f);
            int y0 = cy + (int)(sinf(ang) * (r + 4) + 0.5f);
            int x1 = cx + (int)(cosf(ang) * (r + 2) + 0.5f);
            int y1 = cy + (int)(sinf(ang) * (r + 2) + 0.5f);
            graphics_draw_line(s, x0, y0, x1, y1, th->dim);
        }
        fgui__disc(s, cx, cy, r, th->track);
        fgui__disc(s, cx - 1, cy - 1, r - 4, th->panel);   /* the cap highlight */
        fgui__ring(s, cx, cy, r, lineC);
        /* the travelled arc, drawn as short spokes so it reads as a value */
        float t = fgui__norm(it);
        for (int a = 0; a <= (int)(t * 18); a++) {
            float ang = (135.0f + (a / 18.0f) * 270.0f) * 3.14159265f / 180.0f;
            int x0 = cx + (int)(cosf(ang) * (r - 1) + 0.5f);
            int y0 = cy + (int)(sinf(ang) * (r - 1) + 0.5f);
            int x1 = cx + (int)(cosf(ang) * (r - 4) + 0.5f);
            int y1 = cy + (int)(sinf(ang) * (r - 4) + 0.5f);
            graphics_draw_line(s, x0, y0, x1, y1, th->fill);
        }
        fgui__knob_pointer(s, cx, cy, r, t, selected ? th->hot : th->text);
        lab[6] = 0;
        int lw = (int)strlen(lab) * FGUI_CW;
        fgui__text(s, cx - lw / 2, it->y + 32, nameC, th->panel, lab);
        return;
    }

    if (it->kind == FUI_VSLIDER) {
        int tx = it->x + 11, ty = it->y + 10, th_ = it->h - 20;
        graphics_draw_box(s, tx, ty, 6, th_, th->track);
        fgui__frame(s, tx, ty, 6, th_, th->line);
        float t = fgui__norm(it);
        int hy = ty + th_ - 1 - (int)(t * (th_ - 6));
        graphics_draw_box(s, it->x + 5, hy - 2, 18, 6, selected ? th->hot : th->fill);
        fgui__frame(s, it->x + 5, hy - 2, 18, 6, th->line);
        lab[5] = 0;
        fgui__text(s, it->x, it->y + it->h - 9, nameC, th->panel, lab);
        return;
    }

    /* horizontal fader / numeric entry: name | track | value, three columns
     * that must not overlap - the value is the thing you read while turning it */
    {
        const int NAMEW = 9 * FGUI_CW, VALW = 9 * FGUI_CW;
        int tx = it->x + NAMEW;
        int tw = it->w - NAMEW - VALW - 4;
        if (tw < 20) tw = 20;
        lab[9] = 0;
        fgui__text(s, it->x, it->y + 6, nameC, th->panel, lab);

        graphics_draw_box(s, tx, it->y + 8, tw, 5, th->bg);
        float t = fgui__norm(it);
        int f = (int)(t * (tw - 5));
        if (f > 0) graphics_draw_box(s, tx, it->y + 8, f, 5, th->fill);
        fgui__inset(s, tx, it->y + 8, tw, 5, th->bevel_hi, th->bevel_lo);
        /* the cap: a bevelled block, brighter when it is the one you are on */
        graphics_draw_box(s, tx + f, it->y + 4, 6, 13, selected ? th->hot : th->screw);
        fgui__bevel(s, tx + f, it->y + 4, 6, 13, th->bevel_hi, th->bevel_lo);
        graphics_draw_line(s, tx + f + 1, it->y + 10, tx + f + 4, it->y + 10,
                           th->bevel_lo);

        fui__fmt(*it->zone, it->unit, val);
        val[9] = 0;
        int vx = it->x + it->w - (int)strlen(val) * FGUI_CW;
        fgui__text(s, vx, it->y + 6, selected ? th->hot : th->text, th->panel, val);
    }
}

/* ---- the whole screen ----------------------------------------------------
 * `rows` are app-level selectable lines drawn above the DSP's own widgets
 * (engine, preset, tab...). They exist so the whole interface is reachable with
 * nothing but the d-pad: an N64 pad may have L, R, Z and the C buttons, but an
 * emulator with only the arrows bound does not, and a UI that needs them is
 * simply unusable there. */
typedef struct { const char *label; const char *value; } fgui_row;

static void fgui_draw(surface_t *s, fui_t *u, int sel, const fgui_theme *th,
                      const char *title, const char *status, const char *footer,
                      const fgui_row *rows, int nrows, int selrow, float vu)
{
    int W = (int)display_get_width(), H = (int)display_get_height();
    graphics_fill_screen(s, th->bg);

    const int L = FGUI_MX, R = W - FGUI_MX;
    graphics_draw_box(s, L - 5, FGUI_MY - 2, R - L + 10, H - 2 * FGUI_MY, th->panel);
    fgui__bevel(s, L - 5, FGUI_MY - 2, R - L + 10, H - 2 * FGUI_MY,
                th->bevel_hi, th->bevel_lo);
    fgui__screw(s, L - 1, FGUI_MY + 2, th->screw, th->bevel_lo);
    fgui__screw(s, R - 1, FGUI_MY + 2, th->screw, th->bevel_lo);
    fgui__screw(s, L - 1, H - FGUI_MY - 5, th->screw, th->bevel_lo);
    fgui__screw(s, R - 1, H - FGUI_MY - 5, th->screw, th->bevel_lo);
    graphics_draw_box(s, L + 4, FGUI_MY, R - L - 8, 11, th->bg);
    if (title)  fgui__text(s, L + 6, FGUI_MY + 2, th->accent, th->bg, title);
    if (status) fgui__text(s, R - 6 - (int)strlen(status) * FGUI_CW, FGUI_MY + 2,
                           th->dim, th->bg, status);
    if (vu >= 0.0f) {                       /* output level, 18 segments */
        int tlen = title ? (int)strlen(title) : 0;
        int slen = status ? (int)strlen(status) : 0;
        int vx = L + 6 + (tlen + 1) * FGUI_CW;
        int room = (R - 8 - slen * FGUI_CW) - vx;
        int vw = (room > 90) ? 90 : room;
        if (vw < 18) vw = 0;
        int lit = (int)(vu * 18.0f + 0.5f);
        for (int k = 0; vw && k < 18; k++) {
            uint32_t c = (k >= lit) ? th->track
                       : (k > 15) ? th->warn : (k > 12) ? th->hot : th->meter;
            graphics_draw_box(s, vx + k * (vw / 18), FGUI_MY + 3, (vw / 18) - 1, 6, c);
        }
    }
    graphics_draw_line(s, L - 3, FGUI_MY + 11, R + 2, FGUI_MY + 11, th->line);

    int top = FGUI_MY + 13, ntab = fui_tabs(u);
    if (ntab > 1) {
        int x = L - 3;
        for (int t = 0; t < ntab; t++) {
            const char *nm = (t < 8 && u->tabname[t]) ? u->tabname[t] : "?";
            char b[8];
            int k = 0;
            for (; nm[k] && nm[k] != '[' && k < 5; k++) b[k] = nm[k];
            b[k] = 0;
            int tw = (k + 2) * FGUI_CW;
            int on = (t == u->opentab);
            graphics_draw_box(s, x, top, tw, 11, on ? th->accent : th->track);
            fgui__bevel(s, x, top, tw, 11, th->bevel_hi, th->bevel_lo);
            graphics_draw_box(s, x + 2, top + 4, 3, 3, on ? th->amber : th->led_off);
            fgui__text(s, x + FGUI_CW, top + 2, on ? th->bg : th->dim,
                       on ? th->accent : th->track, b);
            x += tw + 2;
            if (x > R - 20) break;
        }
        top += 13;
    }

    int bot = H - FGUI_MY - 12;
    int scroll = 0;
    graphics_draw_box(s, L - 3, top, R - L + 6, bot - top, th->panel);
    fgui__inset(s, L - 3, top, R - L + 6, bot - top, th->bevel_hi, th->bevel_lo);

    for (int r = 0; r < nrows; r++) {
        int y = top + 2 + r * 13;
        int on = (r == selrow);
        fgui__text(s, L, y + 2, on ? th->hot : th->dim, th->panel, rows[r].label);
        fgui__text(s, L + 9 * FGUI_CW, y + 2, on ? th->hot : th->accent, th->panel,
                   rows[r].value);
        if (on) fgui__frame(s, L - 2, y, R - L + 4, 12, th->hot);
    }
    if (nrows) {
        int y = top + 2 + nrows * 13;
        graphics_draw_line(s, L, y, R, y, th->line);
        top = y + 2;
    }

    /* Lay out as one tall column, then scroll it so the cursor stays on
     * screen. Wrapping into a second column was the previous approach and it
     * cannot work: a full-width fader in column two draws straight over
     * column one. */
    fgui_layout(u, L, top + 3, R - L, 20000);

    if (sel >= 0 && sel < u->n) {
        const fui_item *cur = &u->it[sel];
        int viewh = bot - top - 6;
        if (cur->y + cur->h > top + 3 + viewh) scroll = (cur->y + cur->h) - (top + 3 + viewh);
        if (cur->y - scroll < top + 3) scroll = cur->y - (top + 3);
    }

    for (int i = 0; i < u->n; i++) {
        fui_item *it = &u->it[i];
        if (it->hidden || it->kind == FUI_CLOSE) continue;
        if (it->tab >= 0 && it->tab != u->opentab) continue;
        it->y -= scroll;
        if (it->y + it->h < top || it->y > bot) { it->y += scroll; continue; }
        if (it->kind == FUI_VGROUP || it->kind == FUI_HGROUP) {
            if (it->tab >= 0 && it->depth == 1) continue;   /* the tab names it */
            if (it->label && it->label[0] && it->label[0] != '0')
                fgui__text(s, it->x, it->y, th->dim, th->panel, it->label);
            it->y += scroll;
            continue;
        }
        if (it->kind == FUI_TGROUP) { it->y += scroll; continue; }
        if (it->w == 0) { it->y += scroll; continue; }
        fgui__draw_item(s, th, it, i == sel);
        it->y += scroll;
    }

    graphics_draw_line(s, L - 3, bot, R + 2, bot, th->line);
    if (footer) fgui__text(s, L, bot + 3, th->dim, th->bg, footer);
}

#endif /* FAUSTGUI_H */
