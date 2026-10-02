/* fieldrender.c - run a Faust picture program headless, write Game Boy shade levels.
 * Built by faust2gb with the program's C code included (-cn pic).
 *   fieldrender W H SUB PICTURES out.bin
 * out.bin: PICTURES x W x H bytes, levels 0 (black) .. 3 (white). If every
 * value is already on the 4 levels they are kept exactly, else a 4x4 ordered
 * dither. With SUB > 1 the program takes SUB samples per pixel; the last counts. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
int main(int argc, char **argv) {
    if (argc != 6) { fprintf(stderr, "usage: fieldrender W H SUB PICTURES out.bin\n"); return 2; }
    int W = atoi(argv[1]), H = atoi(argv[2]), S = atoi(argv[3]), P = atoi(argv[4]);
    size_t n = (size_t)W * H * S, px = (size_t)W * H;
    pic *d = newpic(); initpic(d, 48000);
    int no = getNumOutputspic(d), ni = getNumInputspic(d);
    float **o = calloc(no > 0 ? no : 1, sizeof *o), **in = calloc(ni > 0 ? ni : 1, sizeof *in);
    for (int c = 0; c < no; c++) o[c] = calloc(n, 4);
    for (int c = 0; c < ni; c++) in[c] = calloc(n, 4);
    float *all = malloc(px * P * sizeof *all); int exact = 1;
    for (int p = 0; p < P; p++) {
        computepic(d, (int)n, in, o);
        for (size_t i = 0; i < px; i++) {
            float v = o[0][i * S + S - 1]; v = v < 0 ? 0 : v > 1 ? 1 : v;
            all[(size_t)p * px + i] = v;
            if (fabsf(v * 3 - roundf(v * 3)) > 0.01f) exact = 0;
        }
    }
    static const int B[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    FILE *f = fopen(argv[5], "wb"); if (!f) { perror(argv[5]); return 1; }
    for (int p = 0; p < P; p++)
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                float v = all[(size_t)p * px + (size_t)y * W + x] * 3;
                int q = exact ? (int)roundf(v) : (int)floorf(v + (B[y & 3][x & 3] + 0.5f) / 16.0f);
                fputc(q < 0 ? 0 : q > 3 ? 3 : q, f);
            }
    fclose(f);
    fprintf(stderr, "fieldrender: %d pictures %dx%d, %s\n", P, W, H, exact ? "exact 4 levels" : "ordered dither to 4 levels");
    return 0;
}
