/* dos.c - Faust on MS-DOS: the DSP runs live on the PC (32-bit DJGPP, x87 FPU),
 * sound through a Sound Blaster 16, the controls as faders on a VGA screen.
 *
 * Built by faust2dos: the Faust program's C (-lang c -cn mydsp) is included
 * ahead of this file. The interface is ui/faustui.h + ui/faustgui.h, the same
 * code as the N64 build, drawn through dos/libdragon.h on VGA mode 13h.
 *
 * Sound: 16-bit stereo, auto-init DMA on the high DMA channel, two halves.
 * No interrupt handler: the loop reads the DMA controller's position and
 * refills the half that has just been played (the card's IRQ is masked at the
 * interrupt controller while playing, and acknowledged at the card).
 * The card is found from the BLASTER variable (A220 I5 D1 H5 T6), as DOS
 * programs always did; DOSBox sets it.
 *
 * Keys: Up/Down choose a control, Left/Right change it (hold to go faster),
 * Space/Enter press a button or flip a checkbox, Tab/PgUp/PgDn change page,
 * R resets every control, Esc quits.
 *
 * FAUSTDOS_TEST=N (environment): play as usual for N seconds, then write
 * SCREEN.PPM (what is on the VGA screen) and TEST.TXT: the sample rate measured
 * from how fast the card takes the buffers, CPU load, late buffers - and quit.
 *
 * FAUST_DOS_SELFTEST: no sound, no screen - renders 4096 samples from the reset
 * state and prints sum/min/max, as the N64 self-test does, for comparison with
 * tools/selftest.sh on the host. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pc.h>
#include <dpmi.h>
#include <go32.h>
#include <conio.h>
#include <time.h>
#include <sys/movedata.h>
#include "libdragon.h"
#include "faustui.h"
#include "faustgui.h"

#ifndef FAUST_DOS_SR
#define FAUST_DOS_SR 22050
#endif
#ifndef FAUST_DOS_HALF
#define FAUST_DOS_HALF 1024         /* frames per half buffer: 46 ms at 22050 (512 had a late buffer now and then in DOSBox) */
#endif
#ifndef FAUST_DOS_DSPNAME
#define FAUST_DOS_DSPNAME "faust"
#endif
#define CHUNK 64

static inline short clip16(float v)
{
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return (short)(v * 32000.0f);
}

/* ---- Sound Blaster 16 ---------------------------------------------------- */
static int sb_base = 0x220, sb_irq = 5, sb_dma16 = 5;
static int sb_dma_seg = 0, sb_dma_sel = 0;
static unsigned long sb_phys = 0;
static int sb_pic_was_masked = 0;

static void sb_write(int v)
{
    for (int t = 0; t < 100000 && (inportb(sb_base + 0xC) & 0x80); t++) {}
    outportb(sb_base + 0xC, v);
}
static int sb_read(void)
{
    for (int t = 0; t < 100000; t++) if (inportb(sb_base + 0xE) & 0x80) return inportb(sb_base + 0xA);
    return -1;
}
static int sb_reset(void)
{
    outportb(sb_base + 6, 1);
    for (int i = 0; i < 100; i++) inportb(sb_base + 6);         /* > 3 us */
    outportb(sb_base + 6, 0);
    return sb_read() == 0xAA;
}
static void sb_parse_blaster(void)
{
    const char *b = getenv("BLASTER");
    if (!b) return;
    for (const char *p = b; *p; p++) {
        if (*p == 'A' || *p == 'a') sb_base = (int)strtol(p + 1, NULL, 16);
        if (*p == 'I' || *p == 'i') sb_irq = atoi(p + 1);
        if (*p == 'H' || *p == 'h') sb_dma16 = atoi(p + 1);
    }
}

/* 16-bit DMA channels 5..7: ports on the second controller */
static const int dma_addr[4] = { 0xC0, 0xC4, 0xC8, 0xCC }, dma_cnt[4] = { 0xC2, 0xC6, 0xCA, 0xCE };
static const int dma_page[4] = { 0x8F, 0x8B, 0x89, 0x8A };

static void dma_start(unsigned long phys, unsigned words)
{
    int c = sb_dma16 & 3;
    outportb(0xD4, 4 | c);                                       /* mask */
    outportb(0xD8, 0);                                           /* clear flip-flop */
    outportb(0xD6, 0x58 | c);                                    /* single, auto-init, memory -> card */
    unsigned long w = phys >> 1;
    outportb(dma_addr[c], w & 0xFF); outportb(dma_addr[c], (w >> 8) & 0xFF);
    outportb(dma_page[c], (phys >> 16) & 0xFE);
    outportb(dma_cnt[c], (words - 1) & 0xFF); outportb(dma_cnt[c], ((words - 1) >> 8) & 0xFF);
    outportb(0xD4, c);                                           /* unmask */
}

/* words of the buffer still to play in this pass */
static unsigned dma_left(void)
{
    int c = sb_dma16 & 3;
    unsigned a, b;
    do {
        outportb(0xD8, 0); a = inportb(dma_cnt[c]); a |= inportb(dma_cnt[c]) << 8;
        outportb(0xD8, 0); b = inportb(dma_cnt[c]); b |= inportb(dma_cnt[c]) << 8;
    } while (a - b > 4 && b - a > 4);
    return (b + 1) & 0xFFFF;
}

static void pic_mask(int on)
{
    int port = sb_irq < 8 ? 0x21 : 0xA1, bit = 1 << (sb_irq & 7);
    int m = inportb(port);
    if (on) { sb_pic_was_masked = (m & bit) != 0; outportb(port, m | bit); }
    else if (!sb_pic_was_masked) outportb(port, m & ~bit);
}

/* DOS memory for the DMA buffer: below 1 MB, not crossing a 128 KB boundary */
static int sb_alloc(unsigned bytes)
{
    int sel, seg = __dpmi_allocate_dos_memory((bytes * 2 + 15) / 16, &sel);   /* twice the size: room to align */
    if (seg < 0) return 0;
    unsigned long lo = (unsigned long)seg * 16, p = lo;
    if ((p >> 17) != ((p + bytes - 1) >> 17)) p = ((p >> 17) + 1) << 17;       /* start at the next 128 KB boundary */
    if (p + bytes > lo + bytes * 2) { __dpmi_free_dos_memory(sel); return 0; }
    sb_dma_seg = seg; sb_dma_sel = sel; sb_phys = p;
    return 1;
}

static void sb_stop(void)
{
    sb_write(0xD5);                                              /* pause 16-bit */
    sb_write(0xD9);                                              /* leave auto-init */
    outportb(0xD4, 4 | (sb_dma16 & 3));
    inportb(sb_base + 0xF);
    pic_mask(0);
    if (sb_dma_sel) __dpmi_free_dos_memory(sb_dma_sel);
}

/* ---- main ------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    mydsp *dsp = newmydsp();
    initmydsp(dsp, FAUST_DOS_SR);
    const int nin = getNumInputsmydsp(dsp), nout = getNumOutputsmydsp(dsp);
    float *buf[8]; FAUSTFLOAT *ip[8], *op[8];
    for (int c = 0; c < 8; c++) { buf[c] = calloc(CHUNK, sizeof(float)); ip[c] = op[c] = buf[c]; }
    float *in0 = calloc(CHUNK, sizeof(float));
    for (int c = 0; c < nin && c < 8; c++) ip[c] = in0;           /* no audio in: silence */
    for (int c = 0; c < nout && c < 8; c++) op[c] = buf[c];

#ifdef FAUST_DOS_SELFTEST
    {
        long long sum = 0; int lo = 32767, hi = -32768;
        for (int done = 0; done < 4096; done += CHUNK) {
            computemydsp(dsp, CHUNK, ip, op);
            for (int i = 0; i < CHUNK; i++) {
                short v = clip16(op[0][i]);
                sum += v; if (v < lo) lo = v; if (v > hi) hi = v;
            }
        }
        printf("SELFTEST %s\nsr %d n 4096\nsum %lld\nmin %d\nmax %d\n", FAUST_DOS_DSPNAME, FAUST_DOS_SR, sum, lo, hi);
        return 0;
    }
#endif

    sb_parse_blaster();
    if (!sb_reset()) { printf("faust: no Sound Blaster at %Xh (set BLASTER=A220 I5 D1 H5 T6)\n", sb_base); return 1; }
    sb_write(0xE1); int major = sb_read(); sb_read();
    if (major < 4) { printf("faust: needs a Sound Blaster 16 (DSP 4.xx), found DSP %d.xx\n", major); return 1; }

    const unsigned frames = 2 * FAUST_DOS_HALF, bytes = frames * 4, words = frames * 2;
    if (!sb_alloc(bytes)) { printf("faust: no DOS memory for the sound buffer\n"); return 1; }
    static short half[FAUST_DOS_HALF * 2];
    memset(half, 0, sizeof half);
    dosmemput(half, sizeof half, sb_phys); dosmemput(half, sizeof half, sb_phys + sizeof half);

    static fui_t ui;
    fui_build(&ui, dsp, (void (*)(void *, UIGlue *))buildUserInterfacemydsp);
    int sel = fui_first(&ui);
    fgui_theme theme = fgui_t_default();
    dosgfx_init();

    /* before the card starts: the first screen (slow - the palette is new) and
     * both halves of sound, so nothing has to be done in a hurry at the start */
    {
        char title[40];
        snprintf(title, sizeof title, "%.12s", FAUST_DOS_DSPNAME);
        fgui_draw(&dos__surf, &ui, sel, &theme, title, "", "", NULL, 0, -1, 0);
        dosgfx_show();
        for (int h = 0; h < 2; h++) {
            for (int f = 0; f < FAUST_DOS_HALF; f += CHUNK) {
                computemydsp(dsp, CHUNK, ip, op);
                const float *l = op[0], *r = nout > 1 ? op[1] : op[0];
                for (int i = 0; i < CHUNK; i++) { half[2 * (f + i)] = clip16(l[i]); half[2 * (f + i) + 1] = clip16(r[i]); }
            }
            dosmemput(half, sizeof half, sb_phys + (unsigned long)h * sizeof half);
        }
    }
    pic_mask(1);
    dma_start(sb_phys, words);
    sb_write(0x41); sb_write(FAUST_DOS_SR >> 8); sb_write(FAUST_DOS_SR & 0xFF);   /* output rate */
    sb_write(0xB6); sb_write(0x30);                              /* 16-bit, auto-init, FIFO; stereo signed */
    sb_write((FAUST_DOS_HALF * 2 - 1) & 0xFF); sb_write(((FAUST_DOS_HALF * 2 - 1) >> 8) & 0xFF);

    int next = 0;                    /* the half to fill once the card has moved past it */
    int dirty = 1, rep = 0, lastkey = 0, ntab = fui_tabs(&ui);
    float vu = 0, vupk = 0, load = 0;
    long late = 0;
    uclock_t last_draw = uclock(), last_key = 0, btn_until = 0; int btn_item = -1;
    const double budget = (double)FAUST_DOS_HALF / FAUST_DOS_SR;
    const char *tenv = getenv("FAUSTDOS_TEST");
    const double test_s = tenv ? atof(tenv) : 0;
    const uclock_t t_start = uclock(); long fills = 0; float load_max = 0;

    for (;;) {
        /* sound: which half is the card playing? fill the other one */
        unsigned left = dma_left();
        int playing = left > words / 2 ? 0 : 1;
        if (playing != next) {
            uclock_t t0 = uclock();
            for (int f = 0; f < FAUST_DOS_HALF; f += CHUNK) {
                computemydsp(dsp, CHUNK, ip, op);
                const float *l = op[0], *r = nout > 1 ? op[1] : op[0];
                for (int i = 0; i < CHUNK; i++) {
                    half[2 * (f + i)] = clip16(l[i]); half[2 * (f + i) + 1] = clip16(r[i]);
                    float a = fabsf(l[i]); if (a > vupk) vupk = a;
                }
            }
            dosmemput(half, sizeof half, sb_phys + (unsigned long)next * sizeof half);
            inportb(sb_base + 0xF);                              /* acknowledge the card's interrupt */
            double dt = (double)(uclock() - t0) / UCLOCKS_PER_SEC;
            load = 0.9f * load + 0.1f * (float)(100.0 * dt / budget);
            /* if the card is already past the half just written, it played old samples */
            unsigned now = dma_left(); int pnow = now > words / 2 ? 0 : 1;
            if (pnow == next) late++;
            next ^= 1; fills++;
            if (fills > 8 && load > load_max) load_max = load;
        }
        if (test_s > 0 && (double)(uclock() - t_start) / UCLOCKS_PER_SEC > test_s) {
            double el = (double)(uclock() - t_start) / UCLOCKS_PER_SEC;
            FILE *f = fopen("SCREEN.PPM", "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", DOS_W, DOS_H);
                for (int i = 0; i < DOS_W * DOS_H; i++) {
                    uint32_t c = dos__pal[dos__fb[i]];
                    fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f);
                }
                fclose(f);
            }
            f = fopen("TEST.TXT", "w");
            if (f) {
                fprintf(f, "dsp %s\nrate_set %d\nrate_measured %.1f\nseconds %.2f\nbuffers %ld\nlate %ld\ncpu_load %.1f\ncpu_load_max %.1f\n",
                        FAUST_DOS_DSPNAME, FAUST_DOS_SR, fills * (double)FAUST_DOS_HALF / el, el, fills, late, load, load_max);
                fclose(f);
            }
            break;
        }
        /* a pressed button is held for 150 ms (the keyboard only says "pressed") */
        if (btn_item >= 0 && uclock() > btn_until) { *ui.it[btn_item].zone = 0; btn_item = -1; dirty = 1; }
        /* keys */
        if (kbhit()) {
            int k = getkey();
            uclock_t t = uclock();
            rep = (k == lastkey && t - last_key < UCLOCKS_PER_SEC / 4) ? rep + 1 : 0;
            lastkey = k; last_key = t;
            int held_ms = rep * 33;
            if (k == 27) break;
            else if (k == 0x148) sel = fui_next(&ui, sel, -1);                       /* up */
            else if (k == 0x150) sel = fui_next(&ui, sel, 1);                        /* down */
            else if (k == 0x14B || k == 0x14D) {                                     /* left / right */
                if (sel >= 0 && ui.it[sel].kind != FUI_BUTTON && ui.it[sel].kind != FUI_CHECK)
                    fui_adjust(&ui, sel, k == 0x14D ? 1 : -1, fui_accel(held_ms));
            }
            else if ((k == ' ' || k == 13) && sel >= 0) {
                if (ui.it[sel].kind == FUI_CHECK) fui_adjust(&ui, sel, 1, 1);
                else if (ui.it[sel].kind == FUI_BUTTON) { *ui.it[sel].zone = 1; btn_item = sel; btn_until = t + UCLOCKS_PER_SEC * 15 / 100; }
            }
            else if ((k == 9 || k == 0x149 || k == 0x151) && ntab > 1) {             /* tab, page up/down */
                ui.opentab = (ui.opentab + (k == 0x149 ? ntab - 1 : 1)) % ntab;
                sel = fui_first(&ui);
            }
            else if (k == 'r' || k == 'R') fui_reset_all(&ui);
            dirty = 1;
        }
        /* screen: on change, or 15 times a second while the meter moves */
        uclock_t t = uclock();
        if (t - last_draw > UCLOCKS_PER_SEC / 15 && (dirty || vupk > 0.002f || vu > 0.002f)) {
            last_draw = t; dirty = 0;
            vu = vupk > vu ? vupk : vu * 0.72f; vupk = 0;
            char title[40], status[40], tabval[16];
            snprintf(title, sizeof title, "%.12s", FAUST_DOS_DSPNAME);
            snprintf(status, sizeof status, "%dk cpu%d late%ld", (FAUST_DOS_SR + 500) / 1000, (int)load, late);
            fgui_row rows[1]; int nr = 0;
            if (ntab > 1) {
                snprintf(tabval, sizeof tabval, "< %.10s >", ui.tabname[ui.opentab] ? ui.tabname[ui.opentab] : "?");
                rows[0].label = "page"; rows[0].value = tabval; nr = 1;
            }
            fgui_draw(&dos__surf, &ui, sel, &theme, title, status,
                      "\x18\x19 pick \x1b\x1a set SPC R ESC", rows, nr, -1, vu);
            dosgfx_show();
        }
    }
    sb_stop();
    dosgfx_text_mode();
    printf("faust: %s, %d Hz, %ld late buffers\n", FAUST_DOS_DSPNAME, FAUST_DOS_SR, late);
    return 0;
}
