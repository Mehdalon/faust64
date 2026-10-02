/* bench.c - time DSPs on the real VR4300 and print one table.
 *
 * Each DSP is compiled by faust into its own translation unit (so their
 * generated symbols cannot collide) and registered in a generated table, by
 * tools/bench.sh (every demo) or tools/optsweep.sh (one demo, compiled several
 * ways).
 *
 * "load" is the honest number: cycles spent in compute() for one sample,
 * divided by the cycles one sample actually lasts at the target rate.
 * 100% means the console cannot keep up.
 *
 * The whole table is run several times and each entry keeps its BEST pass.
 * A single pass conflates the DSP's cost with cache warm-up and with where in
 * the pass it happened to run: measured, the same variant came out at 664 and
 * at 1196 cycles/sample in two builds of the same sweep. Interleaving the
 * repeats and taking the minimum removes the run-order part of that. It does
 * NOT remove the part caused by code and data landing in different places in
 * different builds - for that, compare only within one build. */
#include <libdragon.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef BENCH_SR
#define BENCH_SR 22050
#endif
#ifndef BENCH_N
#define BENCH_N 4096
#endif
#ifndef BENCH_REPS
#define BENCH_REPS 5
#endif
/* Run each DSP for a while BEFORE timing it.
 *
 * This matters more than it sounds. Timing 4096 samples straight from reset
 * measures a DSP in a state it is almost never in: envelopes have not decayed,
 * delay lines are empty, and nothing has reached the denormal range yet. That
 * is why the bench used to report -ftz as a small LOSS on frenchcore (1858 vs
 * 2000 cyc/sample) while the live ROM measured it as a 24-point WIN - the
 * bench simply never ran long enough for denormals to appear. One second of
 * settling puts the DSP in the state the console actually plays it in. */
#ifndef BENCH_SETTLE
#define BENCH_SETTLE BENCH_SR
#endif
#define BENCH_MAX 32

typedef struct {
    const char *name;
    void *(*nw)(void);
    void  (*in)(void *, int);
    void  (*cp)(void *, int, float **, float **);
    int   (*no)(void *);
    int   (*ni)(void *);
} bench_entry;

extern const bench_entry BENCH[];
extern const int BENCH_COUNT;

int main(void)
{
    console_init();
    console_set_render_mode(RENDER_MANUAL);
    console_clear();
    printf("FAUST on N64 - bench @ %d Hz\n", BENCH_SR);
    printf("%-11s %6s %7s (best of %d)\n", "demo", "cyc/s", "load", BENCH_REPS);
    printf("--------------------------------\n");
    console_render();

    const int CH = 64;
    float *sc[8]; float *ip[8], *op[8];
    for (int c = 0; c < 8; c++) sc[c] = (float *)malloc(sizeof(float) * CH);

    /* VR4300 COUNT ticks at half the CPU clock; TICKS_PER_SECOND accounts for
     * that, so work in ticks throughout. */
    const float ticks_per_sample = (float)TICKS_PER_SECOND / (float)BENCH_SR;

    int ne = BENCH_COUNT;
    if (ne > BENCH_MAX) ne = BENCH_MAX;

    static float best[BENCH_MAX];
    static void *dsps[BENCH_MAX];
    for (int e = 0; e < ne; e++) {
        best[e] = 1e30f;
        dsps[e] = BENCH[e].nw();
        BENCH[e].in(dsps[e], BENCH_SR);
    }

    printf("settling %d samples...\n", BENCH_SETTLE);
    console_render();
    for (int e = 0; e < ne; e++) {
        const bench_entry *b = &BENCH[e];
        int nin = b->ni(dsps[e]), nout = b->no(dsps[e]);
        for (int c = 0; c < 8; c++) memset(sc[c], 0, sizeof(float) * CH);
        for (int c = 0; c < nin  && c < 8; c++) ip[c] = sc[c];
        for (int c = 0; c < nout && c < 8; c++) op[c] = sc[c];
        for (int done = 0; done < BENCH_SETTLE; done += CH)
            b->cp(dsps[e], CH, ip, op);
    }

    for (int rep = 0; rep < BENCH_REPS; rep++) {
        for (int e = 0; e < ne; e++) {
            const bench_entry *b = &BENCH[e];
            void *dsp = dsps[e];
            int nin = b->ni(dsp), nout = b->no(dsp);
            for (int c = 0; c < 8; c++) memset(sc[c], 0, sizeof(float) * CH);
            for (int c = 0; c < nin  && c < 8; c++) ip[c] = sc[c];
            for (int c = 0; c < nout && c < 8; c++) op[c] = sc[c];

            b->cp(dsp, CH, ip, op);                 /* warm the caches first */

            uint32_t t0 = TICKS_READ();
            for (int done = 0; done < BENCH_N; done += CH) b->cp(dsp, CH, ip, op);
            uint32_t used = (uint32_t)TICKS_DISTANCE(t0, TICKS_READ());

            float cps = (float)used / (float)BENCH_N;
            if (cps < best[e]) best[e] = cps;
        }
    }

    for (int e = 0; e < ne; e++) {
        int load = (int)(best[e] / ticks_per_sample * 100.0f + 0.5f);
        printf("%-11s %6d %6d%%%s\n", BENCH[e].name, (int)(best[e] + 0.5f), load,
               load >= 100 ? " X" : "");
        console_render();
    }

    printf("--------------------------------\n");
    printf("X = will not run in real time here\n");
    console_render();
    while (1) { }
}
