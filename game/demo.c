/* demo.c - what game/faust64.h looks like from inside a game.
 *
 * Music loops, six sound effects on the buttons, a master filter you can sweep,
 * and a moving thing on screen so the audio is clearly not the whole program.
 * The audio side is eight lines; that is the point. */
#include <libdragon.h>
#include <stdio.h>
#include "faust64.h"

F64_DECLARE(e_music);
F64_DECLARE(e_sfx);
F64_DECLARE(m_master);

#ifndef GAME_SR
#define GAME_SR 16000
#endif

static const char *SFXNAME[6] = { "laser", "jump", "coin", "boom", "hit", "power" };

int main(void)
{
    joypad_init();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    graphics_set_default_font();

    f64_init(GAME_SR, 4);
    static f64_engine eM, eS, eX;
    eM = F64_ENGINE(e_music);
    eS = F64_ENGINE(e_sfx);
    eX = F64_ENGINE(m_master);
    f64_music(&eM);
    f64_sfx_engine(&eS);
    f64_master(&eX);
    f64_gain(F64_MUSIC, 0.55f);
    f64_gain(F64_SFX,   0.9f);

    uint32_t bg  = graphics_make_color( 16,  18,  24, 255);
    uint32_t fg  = graphics_make_color(226, 230, 240, 255);
    uint32_t dim = graphics_make_color(132, 140, 160, 255);
    uint32_t acc = graphics_make_color(127, 209, 185, 255);
    uint32_t hot = graphics_make_color(255, 214, 102, 255);

    int px = 40, py = 150, vy = 0, onground = 1, last = -1, coins = 0, pick = 0;
    float cut = 12000.0f;
    uint32_t last_poll = 0;

    while (1) {
        uint32_t now = TICKS_READ();
        if (TICKS_DISTANCE(last_poll, now) > (int32_t)(TICKS_PER_SECOND / 250)) {
            last_poll = now;
            joypad_poll();
            joypad_buttons_t p = joypad_get_buttons_pressed(JOYPAD_PORT_1);
            joypad_buttons_t h = joypad_get_buttons_held(JOYPAD_PORT_1);

            /* Everything is reachable with A, B, START and the d-pad, because
             * an emulator may have nothing else bound. Z/L/R are shortcuts for
             * a pad that has them. */
            if (p.a && onground) { vy = -9; onground = 0; f64_sfx(F64_SFX_JUMP); last = F64_SFX_JUMP; }
            if (p.b)     { f64_sfx(pick); last = pick; }
            if (p.start) { f64_sfx(F64_SFX_COIN); last = F64_SFX_COIN; coins++; }
            if (p.z)     { f64_sfx(F64_SFX_BOOM);    last = F64_SFX_BOOM; }
            if (p.l)     { f64_sfx(F64_SFX_HIT);     last = F64_SFX_HIT; }
            if (p.r)     { f64_sfx(F64_SFX_POWERUP); last = F64_SFX_POWERUP; }

            if (p.d_up)   pick = (pick + 5) % 6;
            if (p.d_down) pick = (pick + 1) % 6;
            if (h.d_left)  px -= 3;
            if (h.d_right) px += 3;
            if (px < 8) px = 8;
            if (px > 300) px = 300;

            /* the master filter, swept while jumping - a game would drive this
             * from something like "player is underwater", not from a button */
            if (!onground) cut /= 1.02f;
            else           cut *= 1.03f;
            if (cut < 300.0f)   cut = 300.0f;
            if (cut > 14000.0f) cut = 14000.0f;
            f64_set(F64_MASTER, "cut", cut);

            vy += 1;
            py += vy;
            if (py >= 150) { py = 150; vy = 0; onground = 1; }
        }

        f64_update();                 /* the whole audio system, once a frame */

        surface_t *s = display_get();      /* blocking: a game draws every frame */
        graphics_fill_screen(s, bg);

        graphics_set_color(acc, bg);
        graphics_draw_text(s, 14, 10, "FAUST64 GAME DEMO");
        graphics_set_color(dim, bg);
        char b[72];
        sprintf(b, "cut %5d Hz   coins %d", (int)cut, coins);
        graphics_draw_text(s, 14, 24, b);

        int lit = (int)(f64_vu() * 20.0f + 0.5f);
        for (int k = 0; k < 20; k++)
            graphics_draw_box(s, 190 + k * 5, 10, 4, 7,
                              (k < lit) ? (k > 16 ? hot : acc)
                                        : graphics_make_color(44, 48, 61, 255));

        graphics_draw_box(s, 0, 170, 320, 3, graphics_make_color(54, 59, 74, 255));
        graphics_draw_box(s, px, py, 16, 20, hot);

        graphics_set_color(fg, bg);
        sprintf(b, "B plays: %-6s   UP/DN picks", SFXNAME[pick]);
        graphics_draw_text(s, 14, 190, b);
        graphics_set_color(dim, bg);
        graphics_draw_text(s, 14, 202, "A jump   START coin   L/R move");
        if (last >= 0) {
            sprintf(b, "last: %s", SFXNAME[last]);
            graphics_draw_text(s, 14, 214, b);
        }
        display_show(s);
    }
}
