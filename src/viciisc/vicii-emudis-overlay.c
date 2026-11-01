/*
 * vicii-emudis-overlay.c - C64-EmuDis "Sprites on screen" overlay
 *
 * The cycle exact VIC-II drawing code records for every raster line where
 * each sprite starts and how wide it is (vicii-draw-cycle.c). At the end of a
 * line the values and the draw buffer line are stored here; at the end of the
 * frame consecutive lines of a sprite are joined into rectangles and drawn as
 * red frames with the sprite number into the draw buffer, just before the
 * frame is shown. Multiplexed sprites (one sprite used several times per
 * frame) get one frame per use.
 *
 * The overlay is part of the picture, so it also appears in screenshots and
 * video recordings made while it is on.
 *
 * This file is part of C64-EmuDis (GPL-2.0-or-later).
 */

#include "vice.h"

#include <string.h>

#include "raster.h"
#include "raster-canvas.h"
#include "viewport.h"
#include "viciitypes.h"
#include "vicii-emudis-overlay.h"
#include "emudis_hooks.h"

#define OVL_MAX_LINES   320
#define OVL_RED         2       /* C64 palette: red */
#define OVL_WHITE       1       /* C64 palette: white */

extern int emudis_spr_line_x[8];
extern int emudis_spr_line_w[8];

static uint8_t *line_ptr[OVL_MAX_LINES];
static short line_x[OVL_MAX_LINES][8];
static short line_w[OVL_MAX_LINES][8];
static unsigned char line_valid[OVL_MAX_LINES];

/* 3x5 digits, one row per byte (bit 2 = left) */
static const unsigned char digits[8][5] = {
    { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 3, 1, 7 },
    { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 2, 2, 2 }
};

static int line_is_visible(raster_t *r, unsigned int l)
{
    struct geometry_s *g = r->geometry;

    return (l >= g->first_displayed_line && l <= g->last_displayed_line)
           || (g->screen_size.height <= g->last_displayed_line
               && l <= g->last_displayed_line - g->screen_size.height);
}

static void plot(int line, int x, uint8_t color)
{
    if (line < 0 || line >= OVL_MAX_LINES || !line_valid[line]) {
        return;
    }
    if (x < 0 || x >= (int)vicii.raster.geometry->screen_size.width) {
        return;
    }
    line_ptr[line][x] = color;
}

/* lines of the frame in display order (NTSC shows lines 0+ at the bottom) */
static int frame_lines(raster_t *r, int *order)
{
    struct geometry_s *g = r->geometry;
    unsigned int l;
    int n = 0;

    for (l = g->first_displayed_line; l <= g->last_displayed_line && l < g->screen_size.height
         && l < OVL_MAX_LINES; l++) {
        order[n++] = (int)l;
    }
    if (g->screen_size.height <= g->last_displayed_line) {
        for (l = 0; l <= g->last_displayed_line - g->screen_size.height && l < OVL_MAX_LINES; l++) {
            order[n++] = (int)l;
        }
    }
    return n;
}

static void draw_frame(const int *order, int top, int bottom, int x, int w, int s)
{
    int i, xx, yy;

    /* rectangle: one pixel outside the sprite, so the sprite stays visible */
    for (xx = x - 1; xx <= x + w; xx++) {
        if (top > 0) {
            plot(order[top - 1], xx, OVL_RED);
        }
        plot(order[bottom + 1 < OVL_MAX_LINES ? bottom + 1 : bottom], xx, OVL_RED);
    }
    for (i = top; i <= bottom; i++) {
        plot(order[i], x - 1, OVL_RED);
        plot(order[i], x + w, OVL_RED);
    }

    /* sprite number in a small red tag above the top left corner */
    for (yy = 0; yy < 7; yy++) {
        int row = top - 8 + yy;
        if (row < 0) {
            continue;
        }
        for (xx = 0; xx < 5; xx++) {
            uint8_t c = OVL_RED;
            if (yy >= 1 && yy <= 5 && xx >= 1 && xx <= 3
                && (digits[s][yy - 1] & (4 >> (xx - 1)))) {
                c = OVL_WHITE;
            }
            plot(order[row], x - 1 + xx, c);
        }
    }
}

/* called by raster_canvas_handle_end_of_frame() before the frame is shown */
static void overlay_end_of_frame(raster_t *r)
{
    static int order[OVL_MAX_LINES + 8];
    int n, s, i;

    if (!emudis_sprite_overlay || r != &vicii.raster) {
        return;
    }
    n = frame_lines(r, order);
    order[n] = order[n > 0 ? n - 1 : 0];    /* guard for bottom + 1 */

    for (s = 0; s < 8; s++) {
        int start = -1, sx = 0, sw = 0;

        for (i = 0; i <= n; i++) {
            int l = (i < n) ? order[i] : -1;
            int x = -1, w = 0;

            if (l >= 0 && line_valid[l] && line_x[l][s] >= 0) {
                x = line_x[l][s];
                w = line_w[l][s];
            }
            if (start >= 0 && (x != sx || w != sw)) {
                draw_frame(order, start, i - 1, sx, sw, s);
                start = -1;
            }
            if (start < 0 && x >= 0) {
                start = i;
                sx = x;
                sw = w;
            }
        }
    }
}

/* called at the end of every raster line, before the line is copied */
void vicii_emudis_overlay_line_end(void)
{
    raster_t *r = &vicii.raster;
    unsigned int l = r->current_line;
    int s, offset;

    if (!emudis_sprite_overlay) {
        return;
    }
    if (raster_end_of_frame_overlay_hook == NULL) {
        raster_end_of_frame_overlay_hook = overlay_end_of_frame;
    }
    if (l >= OVL_MAX_LINES) {
        return;
    }
    if (!line_is_visible(r, l)) {
        line_valid[l] = 0;
        return;
    }
    raster_draw_buffer_ptr_update(r);
    line_ptr[l] = r->draw_buffer_ptr;
    line_valid[l] = 1;

    /* dbuf position -> draw buffer x (see DBUF_OFFSET in vicii-draw.c) */
    offset = 17 * 8 - vicii.screen_leftborderwidth;
    for (s = 0; s < 8; s++) {
        if (emudis_spr_line_x[s] >= 0) {
            line_x[l][s] = (short)(emudis_spr_line_x[s] - offset);
            line_w[l][s] = (short)emudis_spr_line_w[s];
        } else {
            line_x[l][s] = -1;
        }
    }
}
