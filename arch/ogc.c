/* ------------------------------------------------------------------------
 * ogc.c - FAUST architecture for the Nintendo GameCube and Wii (libogc).
 *
 * The same idea as arch/n64.c and it works for the same reason: PowerPC has a
 * hardware FPU, so Faust's -lang c output runs unmodified - no fixed point, no
 * transpiler. This is exactly what the pre-FPU consoles (Mega Drive,
 * PlayStation, PC Engine) cannot do.
 *
 * Audio goes out through libogc's ASND: one stereo voice fed from a pair of
 * buffers, swapped in the voice callback. The UI is the same ui/faustui.h tree
 * the N64 build uses - that file is plain printf, so it ports as it stands.
 *
 * Build-time knobs (-D):
 *   FAUST_OGC_SR      output sample rate (ASND runs at 48000; 32000 also fine)
 *   FAUST_OGC_FRAMES  frames per block
 *
 * This ARCHITECTURE section is distributed under the GNU GPL v3 or later, with
 * the standard FAUST architecture exception.
 * ------------------------------------------------------------------------ */

/******************* BEGIN ogc.c ****************/

#include <gccore.h>
#include <asndlib.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <ogc/lwp_watchdog.h>

#include "faust/gui/CInterface.h"

/* 48000 by default: asndlib fixes the hardware mixer at 48 kHz, so any other
 * rate is resampled inside the library. Matching it means the samples reach
 * the DAC exactly as generated. */
#ifndef FAUST_OGC_SR
#define FAUST_OGC_SR 48000
#endif
#ifndef FAUST_OGC_FRAMES
#define FAUST_OGC_FRAMES 1024
#endif
#ifndef FAUST_OGC_DSPNAME
#define FAUST_OGC_DSPNAME "dsp"
#endif
#ifndef FAUST_OGC_CHUNK
#define FAUST_OGC_CHUNK 64
#endif

#define FCAT_(a, b) a##b
#define FCAT(a, b)  FCAT_(a, b)
#define FDSP(verb)  FCAT(verb, FAUSTCLASS)

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

#include "faustui.h"

static FAUSTCLASS *g_dsp;
static fui_t       g_ui;
static int         g_nin, g_nout;

/* Two blocks, swapped in the voice callback: ASND plays one while we fill the
 * other. They must be 32-byte aligned for the DMA. */
#define BLKBYTES (FAUST_OGC_FRAMES * 2 * (int)sizeof(int16_t))
static uint8_t *g_buf[2];
static int      g_cur;
static volatile int g_want;      /* set by the callback, cleared once filled */

/* Is this thing keeping up?
 *
 * On the N64 the answer only became trustworthy once it was on screen: a
 * plausible-sounding audio loop can be underrunning and there is no way to
 * tell by ear whether you are hearing a bad DSP or a starved one. Two numbers
 * settle it:
 *
 *   misses  - the voice callback came round and the main loop had not yet
 *             refilled the other block, so ASND replayed what was there. This
 *             is a direct, unambiguous underrun count.
 *   lag     - how far behind realtime production has fallen, in ms. ASND
 *             consumes exactly `sr` samples a second, so compare what has
 *             been produced against what should have been consumed by now.
 *
 * Both zero means the fault is downstream, not here. */
static volatile uint32_t g_miss;
static uint64_t g_t0;
static int64_t  g_produced, g_worst;

static float *g_in[8], *g_out[8];

static inline int16_t clip16(float v)
{
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return (int16_t)(v * 32000.0f);
}

static void fill(uint8_t *dst)
{
    int16_t *o = (int16_t *)dst;
    for (int off = 0; off < FAUST_OGC_FRAMES; off += FAUST_OGC_CHUNK) {
        int n = FAUST_OGC_FRAMES - off;
        if (n > FAUST_OGC_CHUNK) n = FAUST_OGC_CHUNK;
        FDSP(compute)(g_dsp, n, g_in, g_out);
        int16_t *p = o + 2 * off;
        const float *l = g_out[0];
        const float *r = (g_nout >= 2) ? g_out[1] : g_out[0];
        for (int i = 0; i < n; i++) {
            p[2 * i]     = clip16(l[i]);
            p[2 * i + 1] = clip16(r[i]);
        }
    }
    DCFlushRange(dst, BLKBYTES);      /* the DSP reads this over DMA */
}

static void voice_cb(s32 voice)
{
    /* Queue the block we already prepared, then ask the main loop for another.
     * Nothing heavy happens in here - this is an interrupt. */
    if (g_want) g_miss++;           /* last request was never serviced */
    if (ASND_AddVoice(voice, g_buf[g_cur], BLKBYTES) == SND_OK) {
        g_cur ^= 1;
        g_want = 1;
    }
}

int main(void)
{
    VIDEO_Init();
    PAD_Init();
    GXRModeObj *rmode = VIDEO_GetPreferredMode(NULL);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    g_dsp = FDSP(new)();
    FDSP(init)(g_dsp, FAUST_OGC_SR);
    fui_build(&g_ui, g_dsp, (void (*)(void *, UIGlue *))FDSP(buildUserInterface));
    int sel = fui_first(&g_ui);

    g_nin  = FDSP(getNumInputs)(g_dsp);
    g_nout = FDSP(getNumOutputs)(g_dsp);
    for (int c = 0; c < 8; c++) {
        g_in[c]  = (float *)memalign(32, sizeof(float) * FAUST_OGC_CHUNK);
        g_out[c] = (float *)memalign(32, sizeof(float) * FAUST_OGC_CHUNK);
        memset(g_in[c],  0, sizeof(float) * FAUST_OGC_CHUNK);
        memset(g_out[c], 0, sizeof(float) * FAUST_OGC_CHUNK);
    }
    for (int c = 0; c < 2; c++) {
        g_buf[c] = (uint8_t *)memalign(32, BLKBYTES);
        memset(g_buf[c], 0, BLKBYTES);
    }

    ASND_Init();
    ASND_Pause(0);
    fill(g_buf[0]);
    fill(g_buf[1]);
    g_cur = 0;
    ASND_SetVoice(0, VOICE_STEREO_16BIT, FAUST_OGC_SR, 0,
                  g_buf[g_cur], BLKBYTES, 255, 255, voice_cb);
    g_cur ^= 1;

    uint32_t frame = 0;
    int rep = 0, editing = 0;
    int prev_x = 0, prev_y = 0, held_ms = 0;
    /* Seed with the two blocks filled before the clock started, or the
     * reading is short by exactly that much and looks like a deficit that
     * is not there. */
    g_produced = 2 * FAUST_OGC_FRAMES;
    g_t0 = gettime();
    while (1) {
        if (g_want) {
            g_want = 0;
            fill(g_buf[g_cur]);
            g_produced += FAUST_OGC_FRAMES;
            int64_t us  = (int64_t)ticks_to_microsecs(gettime() - g_t0);
            int64_t due = us * FAUST_OGC_SR / 1000000;
            int64_t def = due - g_produced;
            if (def > g_worst) g_worst = def;
        }

        PAD_ScanPads();
        u16 pressed = PAD_ButtonsDown(0);
        u16 held    = PAD_ButtonsHeld(0);

        /* Read the analog stick as directions as well as the D-pad.
         *
         * A GameCube pad is an analog-stick machine and most people reach for
         * the stick first; Dolphin's default keyboard mapping also puts the
         * arrow keys on the MAIN STICK and not the D-pad, so a build that
         * only reads the D-pad cannot be controlled at all with stock
         * settings. Edge-detected against the previous direction so holding
         * the stick over gives one step, not a stream. */
        s8 sx = PAD_StickX(0), sy = PAD_StickY(0);
        const s8 DZ = 40;
        int sdir_x = (sx > DZ) - (sx < -DZ);
        int sdir_y = (sy > DZ) - (sy < -DZ);
        if (sdir_y != prev_y) {
            if (sdir_y > 0) pressed |= PAD_BUTTON_UP;
            if (sdir_y < 0) pressed |= PAD_BUTTON_DOWN;
            prev_y = sdir_y;
        }
        if (sdir_x != prev_x) {
            if (sdir_x > 0) pressed |= PAD_BUTTON_RIGHT;
            if (sdir_x < 0) pressed |= PAD_BUTTON_LEFT;
            prev_x = sdir_x;
        }
        if (sdir_y > 0) held |= PAD_BUTTON_UP;
        if (sdir_y < 0) held |= PAD_BUTTON_DOWN;
        if (sdir_x > 0) held |= PAD_BUTTON_RIGHT;
        if (sdir_x < 0) held |= PAD_BUTTON_LEFT;

        if (pressed & PAD_BUTTON_A) editing = !editing;
        int move = 0, dir = 0;
        if (editing) {
            if (pressed & PAD_BUTTON_UP)   dir =  1;
            if (pressed & PAD_BUTTON_DOWN) dir = -1;
        } else {
            if (pressed & PAD_BUTTON_UP)   move = -1;
            if (pressed & PAD_BUTTON_DOWN) move =  1;
        }
        if (pressed & PAD_BUTTON_RIGHT) dir =  1;
        if (pressed & PAD_BUTTON_LEFT)  dir = -1;

        if (move) sel = fui_next(&g_ui, sel, move);

        /* Hold to scrub. This loop runs at VSync, so each frame is ~16.7 ms;
         * one step on the press, a pause, then repeat with the step growing
         * the longer it is held. */
        int hdir = ((held & PAD_BUTTON_RIGHT) ? 1 : 0) - ((held & PAD_BUTTON_LEFT) ? 1 : 0);
        if (editing) hdir += ((held & PAD_BUTTON_UP) ? 1 : 0) - ((held & PAD_BUTTON_DOWN) ? 1 : 0);
        if (hdir == 0) held_ms = 0;
        else           held_ms += 17;

        int step_now = (dir != 0);                       /* the press itself */
        if (hdir != 0 && held_ms > 300) step_now = 1;    /* then repeat */
        if (step_now && sel >= 0) {
            int d = (dir != 0) ? dir : hdir;
            float mul = fui_accel(held_ms) * ((held & PAD_TRIGGER_Z) ? 10.0f : 1.0f);
            fui_adjust(&g_ui, sel, d, mul);
        }
        if (pressed & PAD_BUTTON_START) { fui_reset_all(&g_ui); rep = 0; editing = 0; }
        (void)rep;

        if ((frame++ & 7) == 0) {
            printf("\x1b[2;0H");                 /* home, then repaint */
            printf("FAUST on %s   %s\n",
#ifdef HW_RVL
                   "Wii",
#else
                   "GameCube",
#endif
                   FAUST_OGC_DSPNAME);
            printf("%d Hz  %d frames/block  %d in %d out\n",
                   FAUST_OGC_SR, FAUST_OGC_FRAMES, g_nin, g_nout);
            printf("underruns %lu   realtime lag %d ms%s\n",
                   (unsigned long)g_miss,
                   (int)(g_worst * 1000 / FAUST_OGC_SR),
                   (g_miss > 0) ? "   <- STARVED" : "");
            printf("----------------------------------------\n");
            fui_render(&g_ui, sel);
            printf("----------------------------------------\n");
            printf("UP/DN or STICK move  A edit  START init  hold=faster %s\n",
                   editing ? "[EDITING]" : "        ");
        }
        VIDEO_WaitVSync();
    }
}

/******************** END ogc.c ****************/
