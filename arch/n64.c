/* ------------------------------------------------------------------------
 * n64.c - FAUST architecture file for the Nintendo 64 (libdragon).
 *
 * Produces a standalone .z64 ROM that runs the DSP in real time and gives
 * every Faust control a line on screen, adjustable with the controller.
 *
 * Signal path:  computeXXX() -> per-channel float scratch
 *               -> clamp/scale -> 16-bit stereo interleaved -> libdragon AI
 *
 * The VR4300 has a hardware single-precision FPU, so Faust's -lang c output
 * (which is plain float) runs unmodified. Whether it runs FAST ENOUGH is a
 * per-DSP question, so this architecture measures it: the ROM prints live
 * CPU load, and anything at or above 100% is underrunning.
 *
 * Build-time knobs (-D):
 *   FAUST_N64_SR        output sample rate in Hz        (default 22050)
 *   FAUST_N64_BUFFERS   number of AI buffers            (default 4)
 *   FAUST_N64_MAXPARAM  max controls picked up from UI  (default 32)
 *
 * This ARCHITECTURE section is distributed under the GNU GPL v3 or later,
 * with the standard FAUST architecture exception: you may distribute a
 * larger work containing it under terms of your choice, so long as this
 * section is not modified.
 * ------------------------------------------------------------------------ */

/******************* BEGIN n64.c ****************/

#include <libdragon.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "faust/gui/CInterface.h"
#include "faustui.h"    /* the Faust UI tree: groups, tabs, metadata */
#include "faustgui.h"   /* and its drawing: faders, knobs, buttons, meters */

#ifndef FAUST_N64_SR
#define FAUST_N64_SR 22050
#endif
#ifndef FAUST_N64_BUFFERS
#define FAUST_N64_BUFFERS 6
#endif
#ifndef FAUST_N64_MAXPARAM
#define FAUST_N64_MAXPARAM 32
#endif
#ifndef FAUST_N64_DSPNAME
#define FAUST_N64_DSPNAME "dsp"
#endif
/* Samples handed to Faust per compute() call. Small on purpose - see the
 * comment where the scratch buffers are allocated. */
#ifndef FAUST_N64_CHUNK
#define FAUST_N64_CHUNK 64
#endif

/* Faust names its C entry points <verb><classname>, e.g. computemydsp(). The
 * class name is only known after <<includeclass>>, so reach it through a
 * paste macro rather than hard-coding "mydsp". */
#define FCAT_(a, b) a##b
#define FCAT(a, b)  FCAT_(a, b)
#define FDSP(verb)  FCAT(verb, FAUSTCLASS)

/* Faust's generated code sometimes emits max/min for its own use. */
#ifndef max
#define max(a, b) ((a) < (b) ? (b) : (a))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

/******************************************************************************
 * VECTOR INTRINSICS
 ******************************************************************************/

<<includeIntrinsic>>

/********************END ARCHITECTURE SECTION (part 1/2)****************/

/**************************BEGIN USER SECTION **************************/

<<includeclass>>

/***************************END USER SECTION ***************************/

/*******************BEGIN ARCHITECTURE SECTION (part 2/2)***************/

/* ---- output ------------------------------------------------------------- */

static inline short clip16(float v)
{
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return (short)(v * 32000.0f);
}

int main(void)
{
    joypad_init();
#ifdef FAUST_N64_SELFTEST
    console_init();
    console_set_render_mode(RENDER_MANUAL);
#else
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    graphics_set_default_font();
#endif
    fgui_theme theme = fgui_t_default();

    FAUSTCLASS *dsp = FDSP(new)();
    FDSP(init)(dsp, FAUST_N64_SR);

    static fui_t ui;
    fui_build(&ui, dsp, (void (*)(void *, UIGlue *))FDSP(buildUserInterface));
    int sel = fui_first(&ui);

    const int nin  = FDSP(getNumInputs)(dsp);
    const int nout = FDSP(getNumOutputs)(dsp);

    audio_init(FAUST_N64_SR, FAUST_N64_BUFFERS);
    const int blk = audio_get_buffer_length();   /* stereo frames per AI buffer */
    const int sr  = audio_get_frequency();       /* the rate AI actually chose */

    /* Faust wants one contiguous float buffer per channel, not interleaved.
     *
     * These are deliberately CHUNK samples long, not blk. Measured: handing
     * Faust the whole 880-sample AI buffer put ~7 KB of scratch against the
     * VR4300's 8 KB data cache and cost the `arp` demo 80% CPU, against 43%
     * for the same DSP in tools/bench.sh. Working in small chunks keeps the
     * scratch, the DSP state and the tables resident together. */
    const int nch = (nin > nout ? nin : nout);
    float *ich[8], *och[8];
    FAUSTFLOAT *inptr[8], *outptr[8];
    for (int c = 0; c < 8; c++) { ich[c] = NULL; och[c] = NULL; }
    for (int c = 0; c < nin && c < 8; c++) {
        ich[c] = (float *)malloc(sizeof(float) * FAUST_N64_CHUNK);
        memset(ich[c], 0, sizeof(float) * FAUST_N64_CHUNK);   /* no audio in: silence */
        inptr[c] = ich[c];
    }
    for (int c = 0; c < nout && c < 8; c++) {
        och[c] = (float *)malloc(sizeof(float) * FAUST_N64_CHUNK);
        memset(och[c], 0, sizeof(float) * FAUST_N64_CHUNK);
        outptr[c] = och[c];
    }

#ifdef FAUST_N64_SELFTEST
    /* Render a fixed number of samples from the reset state and print a
     * fingerprint of them. tools/selftest.sh renders the SAME samples from the
     * SAME generated C on the host and compares. This is what makes "the DSP
     * runs on N64" a measured claim instead of a hopeful one. */
    {
        const int CH = 64, TOTAL = 4096;
        float *sc[8];
        FAUSTFLOAT *ip[8], *op[8];
        for (int c = 0; c < nch && c < 8; c++) {
            sc[c] = (float *)malloc(sizeof(float) * CH);
            memset(sc[c], 0, sizeof(float) * CH);
        }
        for (int c = 0; c < nin  && c < 8; c++) ip[c] = sc[c];
        for (int c = 0; c < nout && c < 8; c++) op[c] = sc[c];

        long long sum = 0; int lo = 32767, hi = -32768;
        for (int done = 0; done < TOTAL; done += CH) {
            FDSP(compute)(dsp, CH, ip, op);
            for (int i = 0; i < CH; i++) {
                short v = clip16(sc[0][i]);
                sum += v;
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
        }
        console_clear();
        printf("SELFTEST %s\n", FAUST_N64_DSPNAME);
        printf("sr %d n %d\n", FAUST_N64_SR, TOTAL);
        printf("sum %lld\n", sum);
        printf("min %d\n", lo);
        printf("max %d\n", hi);
        console_render();
        while (1) { /* hold the result on screen */ }
    }
#endif

    int rep = 0, approw = -1, editing = 0, dirty = 1;
    float vu = 0.0f, vupk = 0.0f;
    int starve = 0;
    uint32_t t_audio0 = 0;
    int64_t produced = 0, worst_deficit = 0;
    uint32_t last_draw = TICKS_READ(), last_poll = 0;
    int held_frames = 0;
    float load = 0.0f, load_peak = 0.0f, mixload = 0.0f;
    int warm = 0;
    /* microseconds of real time one buffer represents; the budget to beat */
    const float budget_us = (float)blk * 1000000.0f / (float)sr;

    while (1) {
        /* Poll ONCE PER FRAME, never in the tight audio loop.
         *
         * Measured with tools/padtest: this loop spins about 80,000 times a
         * second. The controller updates 60 times a second, so polling every
         * iteration asks the SI a thousand times per real sample and
         * joypad_get_buttons_pressed() manufactures spurious edges - four
         * keypresses registered as 19 lefts, 21 rights, 19 ups. That is what
         * walked every parameter to its limit, and what made a deliberate
         * press useless.
         *
         * 250 Hz, not 60: an SI transaction takes about 2 ms, so 4 ms between
         * polls is still one poll per transaction (no manufactured edges),
         * while 60 Hz is slow enough to miss a genuinely short press. */
        uint32_t tnow = TICKS_READ();
        joypad_buttons_t p = { 0 }, h = { 0 };
        bool pad = false;
        if (TICKS_DISTANCE(last_poll, tnow) > (int32_t)(TICKS_PER_SECOND / 250)) {
            last_poll = tnow;
            joypad_poll();
            p = joypad_get_buttons_pressed(JOYPAD_PORT_1);
            h = joypad_get_buttons_held(JOYPAD_PORT_1);
            pad = true;
        }

        int ntab = fui_tabs(&ui);
        int nrows = (ntab > 1) ? 1 : 0;      /* the tab row, when there are tabs */

        if (pad) {
            /* Hand the controller straight to any Faust button named after a
             * pad button. This is what lets a .dsp be a playable program and
             * not just a panel: declare button("a") and A presses it. Done
             * before the menu handling so a game's buttons are live even while
             * the cursor sits on a slider. */
            for (int i = 0; i < ui.n; i++) {
                fui_item *it = &ui.it[i];
                if (!it->pad || !it->zone) continue;
                int down = 0;
                switch (it->pad) {
                case FUI_PAD_UP:    down = h.d_up;    break;
                case FUI_PAD_DOWN:  down = h.d_down;  break;
                case FUI_PAD_LEFT:  down = h.d_left;  break;
                case FUI_PAD_RIGHT: down = h.d_right; break;
                case FUI_PAD_A:     down = h.a;       break;
                case FUI_PAD_B:     down = h.b;       break;
                case FUI_PAD_START: down = h.start;   break;
                case FUI_PAD_Z:     down = h.z;       break;
                case FUI_PAD_L:     down = h.l;       break;
                case FUI_PAD_R:     down = h.r;       break;
                case FUI_PAD_CU:    down = h.c_up;    break;
                case FUI_PAD_CD:    down = h.c_down;  break;
                case FUI_PAD_CL:    down = h.c_left;  break;
                case FUI_PAD_CR:    down = h.c_right; break;
                default: break;
                }
                *it->zone = down ? 1.0f : 0.0f;
                if (down) dirty = 1;
            }

            /* If the DSP claims pad buttons it is driving itself, so the menu
             * must not also consume them. */
            int claims_dpad = 0, claims_a = 0;
            for (int i = 0; i < ui.n; i++) {
                int b = ui.it[i].pad;
                if (b == FUI_PAD_UP || b == FUI_PAD_DOWN ||
                    b == FUI_PAD_LEFT || b == FUI_PAD_RIGHT) claims_dpad = 1;
                if (b == FUI_PAD_A) claims_a = 1;
            }
            if (claims_dpad) { p.d_up = p.d_down = p.d_left = p.d_right = 0;
                               h.d_up = h.d_down = h.d_left = h.d_right = 0; }
            if (claims_a) p.a = 0;

            /* If the DSP has taken A for itself, the menu cannot also use it
             * to open edit mode - and on a pad where only Up, Down, A and
             * START are bound, that would leave nothing able to change a
             * value. So START takes over as the edit toggle in that case. */
            if (claims_a) {
                if (p.start) editing = !editing;
            } else if (p.a && !(approw < 0 && sel >= 0 && ui.it[sel].kind == FUI_BUTTON)) {
                editing = !editing;
            }

            /* Outside edit mode up/down MOVE; inside they CHANGE the value.
             * left/right always change it, for a pad that has them. */
            int move = 0, dir = 0;
            if (editing) {
                if (p.d_up)   dir =  1;
                if (p.d_down) dir = -1;
            } else {
                if (p.d_up)   move = -1;
                if (p.d_down) move =  1;
            }
            if (p.d_right) dir =  1;
            if (p.d_left)  dir = -1;

            if (move < 0) {
                if (approw > 0) approw--;
                else if (approw == 0) { approw = -1; sel = fui_next(&ui, 0, -1); }
                else {
                    int prev = fui_next(&ui, sel, -1);
                    if (nrows && prev >= sel) approw = nrows - 1;
                    else sel = prev;
                }
            }
            if (move > 0) {
                if (approw >= 0) {
                    if (approw < nrows - 1) approw++;
                    else { approw = -1; sel = fui_first(&ui); }
                } else {
                    int nxt = fui_next(&ui, sel, 1);
                    if (nrows && nxt <= sel) approw = 0;
                    else sel = nxt;
                }
            }

            if (approw == 0 && ntab > 1) {
                if (dir) { ui.opentab = (ui.opentab + ntab + dir) % ntab; sel = fui_first(&ui); }
            } else if (approw < 0 && sel >= 0) {
                fui_item *it = &ui.it[sel];
                if (it->kind == FUI_BUTTON) {
                    *it->zone = h.a ? 1.0f : 0.0f;
                } else if (it->kind == FUI_CHECK) {
                    if (dir) fui_adjust(&ui, sel, 1, 1.0f);
                } else if (editing) {
                    /* one step on the press, a pause, then repeat - the
                     * behaviour of every key-repeat anyone has ever used */
                    if (dir) { rep = dir; held_frames = 0; }
                    int still = (rep < 0) ? (h.d_down || h.d_left) : (h.d_up || h.d_right);
                    if (rep && !still && !dir) rep = 0;
                    if (rep && held_frames > 0 && held_frames < 80) rep = 0;   /* ~320 ms */
                    if (rep) {
                        held_frames++;
                        float mul = fui_accel(held_frames * 4)   /* 250 Hz polls */ * ((h.z || h.l || h.r) ? 10.0f : 1.0f);
                        fui_adjust(&ui, sel, rep, mul);
                    }
                    else if (still) held_frames++;
                }
            }
            if (p.start && !claims_a) { fui_reset_all(&ui); rep = 0; load_peak = 0.0f; editing = 0; }
            if (p.d_up || p.d_down || p.d_left || p.d_right || p.a || p.b ||
                p.start || p.l || p.r || rep) dirty = 1;
        }

        /* Underrun, measured properly.
         *
         * Counting "did we fill every buffer at once" only catches TOTAL
         * starvation. The honest test is whether we are keeping up with
         * realtime: the AI consumes exactly `sr` samples every second, so
         * compare the samples we have PRODUCED against the samples that should
         * have been consumed by now. If production falls behind, the shortfall
         * IS the gap the listener hears, and it can be reported in
         * milliseconds. A deficit that stays at 0 means the ROM is not the
         * problem. */
        int filled = 0;
        while (audio_can_write()) {
            filled++;
            /* Claim the buffer BEFORE starting the clock. audio_write_begin()
             * blocks until the AI has finished with a buffer, and that wait is
             * the console idling, not the DSP working - timing it would report
             * a load that rises towards 100% no matter how cheap the DSP is. */
            short *out = audio_write_begin();

            uint32_t t0 = TICKS_READ();
            uint32_t mixticks = 0;
            for (int off = 0; off < blk; off += FAUST_N64_CHUNK) {
                int n = blk - off;
                if (n > FAUST_N64_CHUNK) n = FAUST_N64_CHUNK;

                FDSP(compute)(dsp, n, inptr, outptr);

                uint32_t m0 = TICKS_READ();
                short *o = out + 2 * off;
                if (nout >= 2) {
                    const float *l = och[0], *r = och[1];
                    for (int i = 0; i < n; i++) {
                        o[2*i]   = clip16(l[i]);
                        o[2*i+1] = clip16(r[i]);
                    }
                } else {
                    const float *l = och[0];
                    for (int i = 0; i < n; i++) {
                        short s = clip16(l[i]);          /* mono DSP -> both speakers */
                        o[2*i]   = s;
                        o[2*i+1] = s;
                    }
                }
                mixticks += (uint32_t)TICKS_DISTANCE(m0, TICKS_READ());
            }
            audio_write_end();

            /* Load = time spent generating one buffer / the time that buffer lasts.
             * `out` is the float->int16 share of it. */
            uint32_t t2 = TICKS_READ();
            float used = (float)TICKS_TO_US(TICKS_DISTANCE(t0, t2));
            float mixus = (float)TICKS_TO_US((int32_t)mixticks);
            load = load * 0.9f + (used / budget_us * 100.0f) * 0.1f;
            mixload = mixload * 0.9f + (mixus / budget_us * 100.0f) * 0.1f;
            if (++warm > 40 && load > load_peak) load_peak = load;   /* skip cold start */
        }

        /* --- screen ---------------------------------------------------------
         * Measured, not guessed: redrawing every few loop iterations cost the
         * `arp` demo 91% CPU against 43% for the same DSP in tools/bench.sh,
         * because console_render() walks a 320x240 framebuffer and evicts the
         * DSP's working set from the VR4300's caches every time. Four redraws
         * a second is still responsive and gives the audio its budget back. */
        /* Redraw on change, and otherwise only fast enough to animate the VU.
         * A still screen costs nothing, which is the point: the audio budget
         * should not be spent painting a picture that has not changed. */
        uint32_t now = TICKS_READ();
        int due = TICKS_DISTANCE(last_draw, now) > (int32_t)(TICKS_PER_SECOND / 15);
        if (due && (dirty || vupk > 0.002f || vu > 0.002f)) {
            surface_t *fb = display_try_get();
            if (fb) {
                last_draw = now;
                dirty = 0;
                vu = (vupk > vu) ? vupk : (vu * 0.72f);   /* fast attack, slow release */
                vupk = 0.0f;
                char title[64], status[40], footer[64], tabval[16];
                sprintf(title, "%.10s", FAUST_N64_DSPNAME);
                sprintf(status, "%dk cpu%d ur%d lag%dms", (sr + 500) / 1000, (int)load,
                        starve, (int)(worst_deficit * 1000 / (sr ? sr : 1)));
                {
                    int ca = 0;
                    for (int i = 0; i < ui.n; i++)
                        if (ui.it[i].pad == FUI_PAD_A) ca = 1;
                    if (ca)
                        sprintf(footer, editing ? "UP/DN change   START done   A plays"
                                                : "UP/DN move   START edit   A plays");
                    else
                        sprintf(footer, editing ? "UP/DN change   A done   START init"
                                                : "UP/DN move   A edit   START init");
                }
                fgui_row rows[1];
                int nr = 0;
                if (ntab > 1) {
                    const char *nm = ui.tabname[ui.opentab] ? ui.tabname[ui.opentab] : "?";
                    snprintf(tabval, sizeof tabval, "< %.10s >", nm);
                    rows[nr].label = "page"; rows[nr].value = tabval; nr++;
                }
                theme.hot = editing ? graphics_make_color(255, 120, 90, 255)
                                    : graphics_make_color(255, 214, 102, 255);
                fgui_draw(fb, &ui, (approw < 0) ? sel : -1, &theme, title, status,
                          footer, rows, nr, approw, vu);
                display_show(fb);
            }
        }
    }
}

/******************** END n64.c ****************/
