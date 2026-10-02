/* Host reference for tools/selftest.sh: renders the same samples the ROM's
 * SELFTEST block renders, from the same Faust-generated C, and prints the same
 * three numbers. Any disagreement means the N64 build is not computing the DSP
 * the compiler described. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "faust/gui/CInterface.h"

#ifndef HOST_SR
#define HOST_SR 22050
#endif

#include "gen.c"

#define FCAT_(a, b) a##b
#define FCAT(a, b)  FCAT_(a, b)
#define FDSP(verb)  FCAT(verb, FAUSTCLASS)

static short clip16(float v)
{
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return (short)(v * 32000.0f);
}

int main(void)
{
    const int CH = 64, TOTAL = 4096;
    FAUSTCLASS *dsp = FDSP(new)();
    FDSP(init)(dsp, HOST_SR);

    int nin = FDSP(getNumInputs)(dsp), nout = FDSP(getNumOutputs)(dsp);
    int nch = nin > nout ? nin : nout;
    float *sc[8]; FAUSTFLOAT *ip[8], *op[8];
    for (int c = 0; c < nch && c < 8; c++) { sc[c] = calloc(CH, sizeof(float)); }
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
    printf("sum %lld\nmin %d\nmax %d\n", sum, lo, hi);
    return 0;
}
