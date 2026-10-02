/* faust64.h - Faust DSP as a game's audio system, for libdragon.
 *
 * A game does not want a slider page. It wants music that loops, sound effects
 * it can fire, a master chain, and one call per frame. That is all this is.
 *
 *   F64_DECLARE(e_music)                 // once per Faust class you compiled
 *   F64_DECLARE(e_sfx)
 *   F64_DECLARE(m_master)
 *
 *   f64_init(16000, 4);
 *   f64_music(&F64_ENGINE(e_music));
 *   f64_sfx_engine(&F64_ENGINE(e_sfx));
 *   f64_master(&F64_ENGINE(m_master));   // optional
 *   ...
 *   while (1) { ...game...; f64_update(); }
 *
 *   f64_sfx(F64_SFX_LASER);              // fire a one-shot
 *   f64_set(F64_MUSIC, "bpm", 160);      // poke any control by its Faust label
 *
 * Compile each .dsp with `faust -lang c -ftz 1 -cn <ident>` into its own
 * translation unit. -ftz matters: without it the VR4300 traps denormals into
 * software and a decaying envelope costs a quarter of the audio budget.
 *
 * f64_update() is cheap when there is nothing to do - it fills only the AI
 * buffers that are actually free, so calling it every frame is correct. */
#ifndef FAUST64_H
#define FAUST64_H

#include <libdragon.h>
#include <stdlib.h>
#include <string.h>
#include "faust/gui/CInterface.h"
#include "faustui.h"

#ifndef F64_CHUNK
#define F64_CHUNK 64
#endif

typedef struct {
    const char *name;
    void *(*nw)(void);
    void  (*in)(void *, int);
    void  (*ui)(void *, UIGlue *);
    void  (*cp)(void *, int, float **, float **);
    int   (*no)(void *);
    int   (*ni)(void *);
} f64_engine;

#define F64_DECLARE(ident)                                  \
    extern void *new##ident(void);                          \
    extern void  init##ident(void *, int);                  \
    extern void  buildUserInterface##ident(void *, UIGlue *); \
    extern void  compute##ident(void *, int, float **, float **); \
    extern int   getNumOutputs##ident(void *);              \
    extern int   getNumInputs##ident(void *)

#define F64_ENGINE(ident) ((f64_engine){                    \
    #ident, new##ident, init##ident, buildUserInterface##ident, \
    compute##ident, getNumOutputs##ident, getNumInputs##ident })

enum { F64_MUSIC = 0, F64_SFX = 1, F64_MASTER = 2, F64_SLOTS = 3 };

/* the kinds game/sfx.dsp knows about; they are just indices into its `kind` */
enum {
    F64_SFX_LASER = 0, F64_SFX_JUMP, F64_SFX_COIN,
    F64_SFX_BOOM, F64_SFX_HIT, F64_SFX_POWERUP
};

typedef struct {
    const f64_engine *def;
    void  *dsp;
    fui_t  ui;
    int    nout;
    float  gain;
} f64_slot;

static f64_slot f64__slot[F64_SLOTS];
static float   *f64__mix[2], *f64__tmp[2], *f64__zero;
static int      f64__blk, f64__sr, f64__sfx_hold;
static float    f64__vu, f64__vupk;
static int      f64__inited;

static void f64__prep(f64_slot *s, const f64_engine *e, int sr)
{
    s->def = e;
    s->dsp = e->nw();
    e->in(s->dsp, sr);
    fui_build(&s->ui, s->dsp, e->ui);
    s->nout = e->no(s->dsp);
    if (s->gain == 0.0f) s->gain = 1.0f;
}

static void f64_init(int sr, int buffers)
{
    audio_init(sr, buffers);
    f64__sr  = audio_get_frequency();
    f64__blk = audio_get_buffer_length();
    for (int c = 0; c < 2; c++) {
        f64__mix[c] = (float *)malloc(sizeof(float) * F64_CHUNK);
        f64__tmp[c] = (float *)malloc(sizeof(float) * F64_CHUNK);
    }
    f64__zero = (float *)malloc(sizeof(float) * F64_CHUNK);
    memset(f64__zero, 0, sizeof(float) * F64_CHUNK);
    f64__inited = 1;
}

static void f64_music(const f64_engine *e)     { f64__prep(&f64__slot[F64_MUSIC],  e, f64__sr); }
static void f64_sfx_engine(const f64_engine *e){ f64__prep(&f64__slot[F64_SFX],    e, f64__sr); }
static void f64_master(const f64_engine *e)    { f64__prep(&f64__slot[F64_MASTER], e, f64__sr); }

static void f64_gain(int slot, float g)
{
    if (slot >= 0 && slot < F64_SLOTS) f64__slot[slot].gain = g;
}

/* Set any control of any slot by the label the .dsp gave it. Returns 0 if
 * there is no such control - a typo here is otherwise silent. */
static int f64_set(int slot, const char *label, float value)
{
    if (slot < 0 || slot >= F64_SLOTS) return 0;
    fui_t *u = &f64__slot[slot].ui;
    for (int i = 0; i < u->n; i++) {
        fui_item *it = &u->it[i];
        if (!it->zone || !it->label || strcmp(it->label, label) != 0) continue;
        if (value < it->lo) value = it->lo;
        if (value > it->hi) value = it->hi;
        *it->zone = value;
        return 1;
    }
    return 0;
}

static float f64_get(int slot, const char *label)
{
    if (slot < 0 || slot >= F64_SLOTS) return 0.0f;
    fui_t *u = &f64__slot[slot].ui;
    for (int i = 0; i < u->n; i++)
        if (u->it[i].zone && u->it[i].label && !strcmp(u->it[i].label, label))
            return *u->it[i].zone;
    return 0.0f;
}

/* Fire a one-shot. The gate is held for a couple of buffers and then dropped,
 * which is what a Faust `button` expects. */
static void f64_sfx(int kind)
{
    if (!f64__slot[F64_SFX].dsp) return;
    f64_set(F64_SFX, "kind", (float)kind);
    f64_set(F64_SFX, "gate", 1.0f);
    f64__sfx_hold = 2;
}

static float f64_vu(void) { return f64__vu; }

static inline short f64__clip(float v)
{
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return (short)(v * 32000.0f);
}

static void f64__run(f64_slot *s, int n, float *outL, float *outR)
{
    if (!s->dsp) { outL[0] = outL[0]; return; }
    float *op[2] = { f64__tmp[0], f64__tmp[1] };
    float *ip[2] = { f64__zero, f64__zero };
    s->def->cp(s->dsp, n, ip, op);
    const float *l = f64__tmp[0];
    const float *r = (s->nout >= 2) ? f64__tmp[1] : f64__tmp[0];
    for (int i = 0; i < n; i++) { outL[i] += l[i] * s->gain; outR[i] += r[i] * s->gain; }
}

static void f64_update(void)
{
    if (!f64__inited) return;
    /* Bounded on purpose. A game calls this inside its frame loop, so it must
     * never be able to sit here filling buffers while the frame goes unserved;
     * at most it tops up every buffer the AI has, once. */
    int guard = 8;
    while (guard-- > 0 && audio_can_write()) {
        short *out = audio_write_begin();
        for (int off = 0; off < f64__blk; off += F64_CHUNK) {
            int n = f64__blk - off;
            if (n > F64_CHUNK) n = F64_CHUNK;
            memset(f64__mix[0], 0, sizeof(float) * n);
            memset(f64__mix[1], 0, sizeof(float) * n);

            if (f64__slot[F64_MUSIC].dsp) f64__run(&f64__slot[F64_MUSIC], n, f64__mix[0], f64__mix[1]);
            if (f64__slot[F64_SFX].dsp)   f64__run(&f64__slot[F64_SFX],   n, f64__mix[0], f64__mix[1]);

            if (f64__slot[F64_MASTER].dsp) {
                float *ip[2] = { f64__mix[0], f64__mix[1] };
                float *op[2] = { f64__tmp[0], f64__tmp[1] };
                f64__slot[F64_MASTER].def->cp(f64__slot[F64_MASTER].dsp, n, ip, op);
                memcpy(f64__mix[0], f64__tmp[0], sizeof(float) * n);
                memcpy(f64__mix[1], f64__tmp[1], sizeof(float) * n);
            }

            short *o = out + 2 * off;
            for (int i = 0; i < n; i++) {
                float a = f64__mix[0][i], b = f64__mix[1][i];
                float m = (a < 0 ? -a : a);
                if (m > f64__vupk) f64__vupk = m;
                o[2*i]   = f64__clip(a);
                o[2*i+1] = f64__clip(b);
            }
        }
        audio_write_end();

        if (f64__sfx_hold > 0 && --f64__sfx_hold == 0) f64_set(F64_SFX, "gate", 0.0f);
        f64__vu = (f64__vupk > f64__vu) ? f64__vupk : f64__vu * 0.85f;
        f64__vupk = 0.0f;
    }
}

#endif /* FAUST64_H */
