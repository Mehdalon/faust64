/* faustui.h - build a real Faust UI on the N64's console.
 *
 * Faust does not hand you a list of sliders; it hands you a TREE, by calling
 * back into openVerticalBox / openHorizontalBox / openTabBox / closeBox around
 * the widgets, with declare() emitting each widget's metadata just before it.
 * Flattening that away throws out most of what the DSP author wrote. This
 * module keeps it:
 *
 *   - groups nest and are drawn as labelled, indented sections
 *   - a tab box becomes real tabs; only the open one is drawn
 *   - bargraphs are output-only, so they are drawn as live meters, never edited
 *   - buttons and checkboxes render and behave as buttons and checkboxes
 *   - [unit:Hz] is shown, [scale:log] is honoured when adjusting,
 *     [hidden] is respected, [style:knob|menu|radio] changes the drawing
 *
 * Header-only and allocation-free: include it after CInterface.h. */
#ifndef FAUSTUI_H
#define FAUSTUI_H

#include <math.h>
#include <string.h>
#include <stdio.h>

#ifndef FUI_MAX
#define FUI_MAX 64
#endif
#ifndef FUI_META
#define FUI_META 16
#endif

enum {
    FUI_VGROUP, FUI_HGROUP, FUI_TGROUP, FUI_CLOSE,
    FUI_BUTTON, FUI_CHECK, FUI_HSLIDER, FUI_VSLIDER, FUI_NENTRY, FUI_HBAR, FUI_VBAR
};

typedef struct {
    int         kind;
    const char *label;
    FAUSTFLOAT *zone;
    float       init, lo, hi, step;
    int         depth;
    int         tab;          /* which tab of the innermost tab box, -1 = none */
    char        unit[10];
    char        style[10];
    int         logscale;
    int         hidden;
    int         knob;         /* [style:knob] */
    int         pad;          /* 1..N: driven by a physical controller button */
    short       x, y, w, h;   /* filled in by ui/faustgui.h */
} fui_item;

typedef struct {
    fui_item    it[FUI_MAX];
    int         n;
    int         depth;
    int         tabdepth;     /* depth at which a tab box was opened, -1 = none */
    int         tabcount;
    int         curtab;       /* tab being filled while building */
    const char *tabname[8];
    int         opentab;      /* tab the user is looking at */
    /* metadata declared for a zone that has not been added as a widget yet */
    FAUSTFLOAT *m_zone[FUI_META];
    char        m_key[FUI_META][12];
    char        m_val[FUI_META][12];
    int         m_n;
} fui_t;

/* ---- building ----------------------------------------------------------- */

static void fui__copy(char *dst, const char *src, int cap)
{
    int i = 0;
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* Faust puts metadata in the label as "name [key:val]" as well as through
 * declare(); strip it so the drawn name stays short. */
static const char *fui__clean(const char *label)
{
    static char buf[24];
    int i = 0;
    while (label[i] && label[i] != '[' && i < 23) { buf[i] = label[i]; i++; }
    while (i > 0 && buf[i - 1] == ' ') i--;
    buf[i] = 0;
    return buf[0] ? buf : label;
}

/* A button whose label names a controller button is driven BY that button.
 * This is the whole mechanism that lets a .dsp be a playable program rather
 * than a panel of sliders: declare button("a") and the A button presses it.
 * The names are matched case-insensitively against the label with metadata
 * stripped. Returns 0 when the label is not one of them. */
enum {
    FUI_PAD_NONE = 0,
    FUI_PAD_UP, FUI_PAD_DOWN, FUI_PAD_LEFT, FUI_PAD_RIGHT,
    FUI_PAD_A, FUI_PAD_B, FUI_PAD_START, FUI_PAD_Z, FUI_PAD_L, FUI_PAD_R,
    FUI_PAD_CU, FUI_PAD_CD, FUI_PAD_CL, FUI_PAD_CR
};

static int fui__padname(const char *label)
{
    static const char *names[] = {
        "up", "down", "left", "right", "a", "b", "start", "z", "l", "r",
        "cup", "cdown", "cleft", "cright"
    };
    char b[16];
    int i = 0;
    for (; label[i] && label[i] != '[' && label[i] != ' ' && i < 15; i++) {
        char c = label[i];
        b[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    b[i] = 0;
    for (int k = 0; k < 14; k++)
        if (!strcmp(b, names[k])) return k + 1;
    return FUI_PAD_NONE;
}

static fui_item *fui__push(fui_t *u, int kind, const char *label, FAUSTFLOAT *zone)
{
    if (u->n >= FUI_MAX) return NULL;
    fui_item *it = &u->it[u->n++];
    memset(it, 0, sizeof *it);
    it->kind = kind;
    it->label = label;
    it->zone = zone;
    it->depth = u->depth;
    it->tab = (u->tabdepth >= 0) ? u->curtab : -1;
    /* pull in anything declare() left for this zone */
    for (int i = 0; zone && i < u->m_n; i++) {
        if (u->m_zone[i] != zone) continue;
        if (!strcmp(u->m_key[i], "unit"))  fui__copy(it->unit, u->m_val[i], sizeof it->unit);
        if (!strcmp(u->m_key[i], "style")) fui__copy(it->style, u->m_val[i], sizeof it->style);
        if (!strcmp(u->m_key[i], "scale")) it->logscale = !strcmp(u->m_val[i], "log");
        if (!strcmp(u->m_key[i], "hidden")) it->hidden = 1;
    }
    return it;
}

static void fui__open(fui_t *u, int kind, const char *label)
{
    if (kind == FUI_TGROUP && u->tabdepth < 0) {
        u->tabdepth = u->depth;
        u->tabcount = 0;
        u->curtab = -1;
    } else if (u->tabdepth >= 0 && u->depth == u->tabdepth + 1) {
        u->curtab = u->tabcount;        /* a direct child of the tab box = one tab */
        if (u->tabcount < 8) u->tabname[u->tabcount] = label;
        u->tabcount++;
    }
    fui__push(u, kind, label, NULL);
    u->depth++;
}

static void fui__close(fui_t *u)
{
    if (u->depth > 0) u->depth--;
    if (u->tabdepth >= 0 && u->depth == u->tabdepth) { u->tabdepth = -1; u->curtab = -1; }
    fui__push(u, FUI_CLOSE, NULL, NULL);
    u->depth = (u->depth < 0) ? 0 : u->depth;
}

/* The glue. `ui_interface` carries the fui_t*. */
static void fui_g_tab(void *i, const char *l)   { fui__open((fui_t *)i, FUI_TGROUP, l); }
static void fui_g_vbox(void *i, const char *l)  { fui__open((fui_t *)i, FUI_VGROUP, l); }
static void fui_g_hbox(void *i, const char *l)  { fui__open((fui_t *)i, FUI_HGROUP, l); }
static void fui_g_close(void *i)                { fui__close((fui_t *)i); }

static void fui_g_declare(void *i, FAUSTFLOAT *z, const char *k, const char *v)
{
    fui_t *u = (fui_t *)i;
    if (u->m_n >= FUI_META || !z) return;
    u->m_zone[u->m_n] = z;
    fui__copy(u->m_key[u->m_n], k, 12);
    fui__copy(u->m_val[u->m_n], v, 12);
    u->m_n++;
}

static void fui__slider(void *i, int kind, const char *l, FAUSTFLOAT *z, FAUSTFLOAT in,
                        FAUSTFLOAT lo, FAUSTFLOAT hi, FAUSTFLOAT st)
{
    fui_item *it = fui__push((fui_t *)i, kind, l, z);
    if (!it) return;
    it->init = in; it->lo = lo; it->hi = hi;
    it->step = (st > 0) ? st : (hi - lo) / 100.0f;
    if (!strcmp(it->style, "knob")) it->knob = 1;
}

static void fui_g_hslider(void *i, const char *l, FAUSTFLOAT *z, FAUSTFLOAT in,
                          FAUSTFLOAT lo, FAUSTFLOAT hi, FAUSTFLOAT st)
{ fui__slider(i, FUI_HSLIDER, l, z, in, lo, hi, st); }

static void fui_g_vslider(void *i, const char *l, FAUSTFLOAT *z, FAUSTFLOAT in,
                          FAUSTFLOAT lo, FAUSTFLOAT hi, FAUSTFLOAT st)
{ fui__slider(i, FUI_VSLIDER, l, z, in, lo, hi, st); }

static void fui_g_nentry(void *i, const char *l, FAUSTFLOAT *z, FAUSTFLOAT in,
                         FAUSTFLOAT lo, FAUSTFLOAT hi, FAUSTFLOAT st)
{ fui__slider(i, FUI_NENTRY, l, z, in, lo, hi, st); }

static void fui_g_button(void *i, const char *l, FAUSTFLOAT *z)
{
    fui_item *it = fui__push((fui_t *)i, FUI_BUTTON, l, z);
    if (it) { it->lo = 0; it->hi = 1; it->step = 1; it->pad = fui__padname(l ? l : ""); }
}

static void fui_g_check(void *i, const char *l, FAUSTFLOAT *z)
{
    fui_item *it = fui__push((fui_t *)i, FUI_CHECK, l, z);
    if (it) { it->lo = 0; it->hi = 1; it->step = 1; }
}

static void fui__bar(void *i, int kind, const char *l, FAUSTFLOAT *z,
                     FAUSTFLOAT lo, FAUSTFLOAT hi)
{
    fui_item *it = fui__push((fui_t *)i, kind, l, z);
    if (it) { it->lo = lo; it->hi = hi; }
}
static void fui_g_hbar(void *i, const char *l, FAUSTFLOAT *z, FAUSTFLOAT lo, FAUSTFLOAT hi)
{ fui__bar(i, FUI_HBAR, l, z, lo, hi); }
static void fui_g_vbar(void *i, const char *l, FAUSTFLOAT *z, FAUSTFLOAT lo, FAUSTFLOAT hi)
{ fui__bar(i, FUI_VBAR, l, z, lo, hi); }

static void fui_g_soundfile(void *i, const char *l, const char *u, struct Soundfile **s)
{ (void)i; (void)l; (void)u; (void)s; }

/* Call the DSP's own buildUserInterface through this. */
static void fui_build(fui_t *u, void *dsp,
                      void (*build)(void *, UIGlue *))
{
    memset(u, 0, sizeof *u);
    u->tabdepth = -1;
    u->curtab = -1;
    UIGlue g; memset(&g, 0, sizeof g);
    g.uiInterface           = u;
    g.openTabBox            = fui_g_tab;
    g.openVerticalBox       = fui_g_vbox;
    g.openHorizontalBox     = fui_g_hbox;
    g.closeBox              = fui_g_close;
    g.addButton             = fui_g_button;
    g.addCheckButton        = fui_g_check;
    g.addVerticalSlider     = fui_g_vslider;
    g.addHorizontalSlider   = fui_g_hslider;
    g.addNumEntry           = fui_g_nentry;
    g.addHorizontalBargraph = fui_g_hbar;
    g.addVerticalBargraph   = fui_g_vbar;
    g.addSoundfile          = fui_g_soundfile;
    g.declare               = fui_g_declare;
    build(dsp, &g);
}

/* ---- querying ------------------------------------------------------------ */

static int fui_editable(const fui_t *u, int i)
{
    const fui_item *it = &u->it[i];
    if (it->hidden || it->kind == FUI_CLOSE || it->pad) return 0;
    if (it->kind == FUI_HBAR || it->kind == FUI_VBAR) return 0;
    if (it->kind == FUI_VGROUP || it->kind == FUI_HGROUP || it->kind == FUI_TGROUP) return 0;
    if (it->tab >= 0 && it->tab != u->opentab) return 0;
    return 1;
}

static int fui_next(const fui_t *u, int from, int dir)
{
    if (u->n == 0) return -1;
    for (int k = 1; k <= u->n; k++) {
        int i = ((from + dir * k) % u->n + u->n) % u->n;
        if (fui_editable(u, i)) return i;
    }
    return fui_editable(u, from) ? from : -1;
}

static int fui_first(const fui_t *u) { return fui_next(u, u->n - 1, 1); }

/* How much a held direction accelerates, in MILLISECONDS held - not in poll
 * counts, because the N64 build polls at 250 Hz and the GameCube one at 60,
 * and a threshold in polls would mean two different things. */
static float fui_accel(int held_ms)
{
    if (held_ms > 2400) return 48.0f;
    if (held_ms > 1400) return 16.0f;
    if (held_ms >  600) return 4.0f;
    return 1.0f;
}

/* ---- editing ------------------------------------------------------------- */

/* `mul` scales the step. It is a float rather than a coarse/fine flag so the
 * caller can ACCELERATE a held direction: a cutoff with a step of 10 over a
 * range of 80..6000 needs hundreds of steps, and no repeat rate that is
 * comfortable for a small nudge is also comfortable for crossing the range.
 * Growing the step with hold time gives both. */
static void fui_adjust(fui_t *u, int i, int dir, float mul)
{
    if (i < 0 || i >= u->n) return;
    fui_item *it = &u->it[i];
    if (!it->zone) return;
    if (mul < 1.0f) mul = 1.0f;
    float v = *it->zone;
    if (it->kind == FUI_CHECK) { v = (v > 0.5f) ? 0.0f : 1.0f; *it->zone = v; return; }
    if (it->logscale && it->lo > 0.0f && it->hi > it->lo) {
        /* a log control should move by a RATIO, not by a fixed amount, or the
         * low end is unusable and the high end takes a hundred presses */
        float f = powf(it->hi / it->lo, mul / 100.0f);
        v = (dir > 0) ? v * f : v / f;
        if (v < it->lo) v = it->lo;
    } else {
        v += dir * it->step * mul;
    }
    if (v < it->lo) v = it->lo;
    if (v > it->hi) v = it->hi;
    *it->zone = v;
}

static void fui_reset_all(fui_t *u)
{
    for (int i = 0; i < u->n; i++) {
        fui_item *it = &u->it[i];
        if (it->pad) continue;                  /* the pad owns these */
        if (it->zone && it->kind != FUI_HBAR && it->kind != FUI_VBAR)
            *it->zone = it->init;
    }
}

static int fui_tabs(const fui_t *u)
{
    int n = 0;
    for (int i = 0; i < u->n; i++) if (u->it[i].tab + 1 > n) n = u->it[i].tab + 1;
    return n;
}

/* ---- drawing ------------------------------------------------------------- */

/* Writes at most FUI_FMT_MAX bytes; every caller must give it a
 * char[FUI_FMT_MAX]. The size is set by what the COMPILER can prove rather
 * than by what can actually happen: v is clamped to 1e9 and the unit to 9
 * characters, so real output is under 26 bytes, but %d and %03d are each worth
 * 11 in the worst case it must assume, and a buffer smaller than that is a
 * -Wformat-truncation error. */
#define FUI_FMT_MAX 40

static void fui__fmt(float v, const char *unit, char *buf)
{
    int neg = (v < 0.0f);
    if (neg) v = -v;
    if (v > 1e9f) v = 1e9f;
    /* int, not long: on a 64-bit-long target the compiler must assume %ld can
     * print 20 digits, which alone overruns any sane buffer. v is already
     * clamped to 1e9, so an int holds it. */
    int w = (int)v;
    int f = (int)((v - (float)w) * 1000.0f + 0.5f);
    if (f >= 1000) { w++; f -= 1000; }
    if (unit && unit[0])
        snprintf(buf, FUI_FMT_MAX, "%s%d.%03d%.9s", neg ? "-" : "", w, f, unit);
    else
        snprintf(buf, FUI_FMT_MAX, "%s%d.%03d", neg ? "-" : "", w, f);
}

#ifndef FUI_METER_W
#define FUI_METER_W 12
#endif

static void fui__meter(float v, float lo, float hi, char *buf)
{
    float t = (hi > lo) ? (v - lo) / (hi - lo) : 0.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int filled = (int)(t * FUI_METER_W + 0.5f);
    buf[0] = '[';
    for (int i = 0; i < FUI_METER_W; i++) buf[1 + i] = (i < filled) ? '=' : ' ';
    buf[1 + FUI_METER_W] = ']';
    buf[2 + FUI_METER_W] = 0;
}

/* Draws the tree. `sel` is the item index the cursor is on. */
static void fui_render(const fui_t *u, int sel)
{
    int ntab = fui_tabs(u);
    if (ntab > 1) {
        for (int t = 0; t < ntab; t++) {
            const char *nm = (t < 8 && u->tabname[t]) ? fui__clean(u->tabname[t]) : "?";
            printf("%s%.5s%s", (t == u->opentab) ? "[" : " ", nm,
                               (t == u->opentab) ? "]" : " ");
        }
        printf(" Z+L/R\n");
    }
    char val[FUI_FMT_MAX], meter[FUI_METER_W + 4];
    for (int i = 0; i < u->n; i++) {
        const fui_item *it = &u->it[i];
        if (it->kind == FUI_CLOSE || it->hidden) continue;
        if (it->tab >= 0 && it->tab != u->opentab) continue;
        int ind = it->depth * 2;
        if (ind > 8) ind = 8;

        switch (it->kind) {
        case FUI_VGROUP: case FUI_HGROUP: case FUI_TGROUP:
            if (it->kind == FUI_TGROUP) break;            /* the tab bar IS its label */
            if (it->tab >= 0 && it->depth == 1) break;   /* named by the tab bar */
            if (it->label && it->label[0] && strcmp(it->label, "0") != 0)
                printf(" %*s%s\n", ind, "", fui__clean(it->label));
            break;
        case FUI_HBAR: case FUI_VBAR:
            fui__meter(*it->zone, it->lo, it->hi, meter);
            printf(" %*s%-10.10s %s\n", ind, "", fui__clean(it->label), meter);
            break;
        case FUI_CHECK:
            printf("%c%*s%-10.10s     [%c]\n", (i == sel) ? '>' : ' ', ind, "",
                   fui__clean(it->label), (*it->zone > 0.5f) ? 'x' : ' ');
            break;
        case FUI_BUTTON:
            printf("%c%*s%-10.10s   %s(%c)\n", (i == sel) ? '>' : ' ', ind, "",
                   fui__clean(it->label), it->pad ? "pad " : "    ",
                   (*it->zone > 0.5f) ? '*' : ' ');
            break;
        default:
            fui__fmt(*it->zone, it->unit, val);
            printf("%c%*s%-12.12s %11s%s\n", (i == sel) ? '>' : ' ', ind, "",
                   fui__clean(it->label), val,
                   it->logscale ? " ~" : "");
            break;
        }
    }
}

#endif /* FAUSTUI_H */
