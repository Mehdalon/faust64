/* fxcheck - render a DSP twice, float and fixed-point, and compare.
 * Built by tools/fxcheck.sh; not meant to be compiled by hand. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "faust/gui/CInterface.h"

#ifndef CHK_SR
#define CHK_SR 16000
#endif

#ifdef CHK_FIXED
#include "faustfx.h"
#endif

#include "gen.c"

#define FCAT_(a, b) a##b
#define FCAT(a, b)  FCAT_(a, b)
#define FDSP(verb)  FCAT(verb, FAUSTCLASS)

int main(int argc, char **argv)
{
    int total = (argc > 1) ? atoi(argv[1]) : 16000;
    const int CH = 64;
    FAUSTCLASS *dsp = FDSP(new)();
    FDSP(init)(dsp, CHK_SR);
    int nin = FDSP(getNumInputs)(dsp), nout = FDSP(getNumOutputs)(dsp);
    int nch = nin > nout ? nin : nout; if (nch < 1) nch = 1;
    FAUSTFLOAT *sc[8], *ip[8], *op[8];
    for (int c = 0; c < nch && c < 8; c++) {
        sc[c] = (FAUSTFLOAT *)calloc(CH, sizeof(FAUSTFLOAT));
    }
    for (int c = 0; c < nin  && c < 8; c++) ip[c] = sc[c];
    for (int c = 0; c < nout && c < 8; c++) op[c] = sc[c];

    FILE *f = fopen(argv[2] ? argv[2] : "/dev/null", "wb");
    total -= total % CH;
    // Feed a test signal rather than silence. Faust's interval analysis only
    // produces usable fixed-point formats for NON-recursive DSPs, and those
    // are all input-processing DSPs (waveshapers, gain stages), so with a
    // silent input there is nothing to compare.
    double ph = 0.0;
    for (int done = 0; done < total; done += CH) {
        if (nin > 0) {
            for (int i = 0; i < CH; i++) {
                float v = (float)(0.6 * sin(ph));
                ph += 2.0 * M_PI * 220.0 / (double)CHK_SR;
                for (int c = 0; c < nin && c < 8; c++) sc[c][i] = (FAUSTFLOAT)v;
            }
        }
        FDSP(compute)(dsp, CH, ip, op);
        for (int i = 0; i < CH; i++) {
            float v = (float)sc[0][i];
            fwrite(&v, sizeof(float), 1, f);
        }
    }
    fclose(f);
    fprintf(stderr, "%s: %d samples\n",
#ifdef CHK_FIXED
            "fixed", total);
#else
            "float", total);
#endif
    return 0;
}
