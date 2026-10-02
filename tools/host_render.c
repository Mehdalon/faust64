/* host_render.c - render a Faust DSP to a WAV on the host, through the SAME
 * signal path the N64 architecture uses (float compute -> clamp -> x32000 ->
 * int16 stereo). The point of the lab is that what you hear here is what the
 * ROM will do, so this file must not "improve" on arch/n64.c.
 *
 * Built by lab/faustlab; not meant to be compiled by hand. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "faust/gui/CInterface.h"

#ifndef LAB_SR
#define LAB_SR 22050
#endif
#ifndef LAB_MAXPARAM
#define LAB_MAXPARAM 64
#endif

#include "gen.c"

#define FCAT_(a, b) a##b
#define FCAT(a, b)  FCAT_(a, b)
#define FDSP(verb)  FCAT(verb, FAUSTCLASS)

/* ---- control collection: same flattening the N64 architecture does -------- */
typedef struct {
    const char *label; FAUSTFLOAT *zone;
    FAUSTFLOAT init, lo, hi, step;
} lab_ctrl;
static lab_ctrl g_ctrl[LAB_MAXPARAM];
static int g_nctrl = 0;

static void add(const char *l, FAUSTFLOAT *z, FAUSTFLOAT i, FAUSTFLOAT lo, FAUSTFLOAT hi, FAUSTFLOAT st)
{
    if (g_nctrl >= LAB_MAXPARAM) return;
    g_ctrl[g_nctrl++] = (lab_ctrl){ l, z, i, lo, hi, st > 0 ? st : (hi - lo) / 100.0f };
}
static void u_box(void *i, const char *l) { (void)i; (void)l; }
static void u_close(void *i) { (void)i; }
static void u_decl(void *i, FAUSTFLOAT *z, const char *k, const char *v)
{ (void)i; (void)z; (void)k; (void)v; }
static void u_slider(void *i, const char *l, FAUSTFLOAT *z, FAUSTFLOAT in,
                     FAUSTFLOAT lo, FAUSTFLOAT hi, FAUSTFLOAT st)
{ (void)i; add(l, z, in, lo, hi, st); }
static void u_btn(void *i, const char *l, FAUSTFLOAT *z) { (void)i; add(l, z, 0, 0, 1, 1); }
static void u_bar(void *i, const char *l, FAUSTFLOAT *z, FAUSTFLOAT lo, FAUSTFLOAT hi)
{ (void)i; (void)l; (void)z; (void)lo; (void)hi; }
static void u_sf(void *i, const char *l, const char *u, struct Soundfile **s)
{ (void)i; (void)l; (void)u; (void)s; }

static short clip16(float v)
{
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return (short)(v * 32000.0f);
}

static void wr32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc((v >> 24) & 255, f); }
static void wr16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

typedef struct { int idx; float lo, hi; } sweep_t;

int main(int argc, char **argv)
{
    /* argv: out.wav seconds [name=value | name=lo:hi]... */
    if (argc < 3) { fprintf(stderr, "host_render out.wav seconds [param=value|param=lo:hi]...\n"); return 2; }
    const char *outpath = argv[1];
    double secs = atof(argv[2]);

    FAUSTCLASS *dsp = FDSP(new)();
    FDSP(init)(dsp, LAB_SR);

    UIGlue ui; memset(&ui, 0, sizeof ui);
    ui.openTabBox = u_box; ui.openHorizontalBox = u_box; ui.openVerticalBox = u_box;
    ui.closeBox = u_close; ui.addButton = u_btn; ui.addCheckButton = u_btn;
    ui.addVerticalSlider = u_slider; ui.addHorizontalSlider = u_slider; ui.addNumEntry = u_slider;
    ui.addHorizontalBargraph = u_bar; ui.addVerticalBargraph = u_bar;
    ui.addSoundfile = u_sf; ui.declare = u_decl;
    FDSP(buildUserInterface)(dsp, &ui);

    sweep_t sweeps[LAB_MAXPARAM]; int nsweep = 0;
    for (int a = 3; a < argc; a++) {
        char buf[256]; snprintf(buf, sizeof buf, "%s", argv[a]);
        char *eq = strchr(buf, '=');
        if (!eq) { fprintf(stderr, "bad param '%s' (want name=value)\n", argv[a]); return 2; }
        *eq = 0;
        int idx = -1;
        for (int i = 0; i < g_nctrl; i++)
            if (g_ctrl[i].label && strcmp(g_ctrl[i].label, buf) == 0) { idx = i; break; }
        if (idx < 0) {
            fprintf(stderr, "no control named '%s'. this DSP has:", buf);
            for (int i = 0; i < g_nctrl; i++) fprintf(stderr, " %s", g_ctrl[i].label);
            fprintf(stderr, "\n");
            return 2;
        }
        char *colon = strchr(eq + 1, ':');
        if (colon) {                      /* name=lo:hi -> sweep across the render */
            *colon = 0;
            sweeps[nsweep++] = (sweep_t){ idx, (float)atof(eq + 1), (float)atof(colon + 1) };
        } else {
            *g_ctrl[idx].zone = (FAUSTFLOAT)atof(eq + 1);
        }
    }

    int nin = FDSP(getNumInputs)(dsp), nout = FDSP(getNumOutputs)(dsp);
    int nch = nin > nout ? nin : nout; if (nch < 1) nch = 1;
    const int CH = 64;
    float *sc[8]; FAUSTFLOAT *ip[8], *op[8];
    for (int c = 0; c < nch && c < 8; c++) sc[c] = calloc(CH, sizeof(float));
    for (int c = 0; c < nin  && c < 8; c++) ip[c] = sc[c];
    for (int c = 0; c < nout && c < 8; c++) op[c] = sc[c];

    long total = (long)(secs * LAB_SR);
    total -= total % CH;
    if (total < CH) total = CH;

    FILE *f = fopen(outpath, "wb");
    if (!f) { perror(outpath); return 1; }
    uint32_t nbytes = (uint32_t)(total * 2 * 2);
    fwrite("RIFF", 1, 4, f); wr32(f, 36 + nbytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); wr32(f, 16); wr16(f, 1); wr16(f, 2);
    wr32(f, LAB_SR); wr32(f, LAB_SR * 4); wr16(f, 4); wr16(f, 16);
    fwrite("data", 1, 4, f); wr32(f, nbytes);

    int peak = 0; long clipped = 0; double sumsq = 0;
    for (long done = 0; done < total; done += CH) {
        for (int s = 0; s < nsweep; s++) {      /* linear over the whole render */
            float t = (float)done / (float)total;
            *g_ctrl[sweeps[s].idx].zone = sweeps[s].lo + t * (sweeps[s].hi - sweeps[s].lo);
        }
        FDSP(compute)(dsp, CH, ip, op);
        for (int i = 0; i < CH; i++) {
            float l = sc[0][i], r = (nout >= 2) ? sc[1][i] : sc[0][i];
            if (l > 1.0f || l < -1.0f || r > 1.0f || r < -1.0f) clipped++;
            short a = clip16(l), b = clip16(r);
            if (abs(a) > peak) peak = abs(a);
            if (abs(b) > peak) peak = abs(b);
            sumsq += (double)a * a;
            wr16(f, (uint16_t)a); wr16(f, (uint16_t)b);
        }
    }
    fclose(f);

    double rms = sqrt(sumsq / (double)total);
    /* Report on stderr so the wrapper can show it without polluting stdout. */
    fprintf(stderr, "controls:");
    for (int i = 0; i < g_nctrl; i++) fprintf(stderr, " %s=%g", g_ctrl[i].label, (double)*g_ctrl[i].zone);
    fprintf(stderr, "\n");
    fprintf(stderr, "%ld samples  %d in %d out  peak %d/32000 (%.1f dBFS)  rms %.0f  clipped %ld\n",
            total, nin, nout, peak,
            peak > 0 ? 20.0 * log10((double)peak / 32000.0) : -99.0, rms, clipped);
    return 0;
}
