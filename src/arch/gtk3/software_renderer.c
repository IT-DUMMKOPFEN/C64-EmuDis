/**
 * \file software_renderer.c
 * \brief   Software (Cairo) renderer for the GTK3 UI - C64-EmuDis addition.
 *
 * This renderer needs neither OpenGL nor DirectX. It is used when the
 * accelerated renderer of the platform is not available, for example in
 * virtual machines without a working 3D driver (Parallels/VMware/VirtualBox),
 * over remote desktop sessions, or when the user selects it.
 *
 * The emulation thread renders each frame into a back buffer which is then
 * swapped (under the canvas lock) with a "ready" buffer. The UI thread swaps
 * the ready buffer with its display buffer and paints it with Cairo, scaled
 * to the widget while keeping the aspect ratio. Three buffers mean neither
 * thread ever waits for the other longer than a pointer swap.
 *
 * \author IT-Dummkopfen
 */

/* This file is part of C64-EmuDis, based on VICE.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include "vice.h"

#include <gtk/gtk.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archdep.h"
#include "lib.h"
#include "log.h"
#include "palette.h"
#include "resources.h"
#include "ui.h"
#include "uistatusbar.h"
#include "util.h"
#include "video.h"
#include "videoarch.h"

#include "software_renderer.h"

#define CANVAS_LOCK()   pthread_mutex_lock(&canvas->lock)
#define CANVAS_UNLOCK() pthread_mutex_unlock(&canvas->lock)

/** \brief One frame buffer (Cairo RGB24, 32 bit per pixel) */
typedef struct sw_buffer_s {
    uint32_t *pixels;
    size_t capacity;            /**< allocated pixels */
    unsigned int width;
    unsigned int height;
    float pixel_aspect_ratio;
} sw_buffer_t;

typedef struct sw_context_s {
    sw_buffer_t back;           /**< written by the emulation thread */
    sw_buffer_t ready;          /**< newest complete frame */
    sw_buffer_t display;        /**< painted by the UI thread */
    int frame_pending;          /**< ready holds a frame not yet shown */

    unsigned int emulated_width_next;
    unsigned int emulated_height_next;
    float pixel_aspect_ratio_next;

    int min_width;              /**< minimum widget size enforced via gtk */
    int min_height;
    int min_width_set;
    int min_height_set;
} sw_context_t;

static log_t sw_log = LOG_DEFAULT;


static void buffer_ensure(sw_buffer_t *b, unsigned int w, unsigned int h)
{
    size_t need = (size_t)w * (size_t)h;

    if (need > b->capacity) {
        lib_free(b->pixels);
        b->pixels = lib_calloc(need, sizeof(uint32_t));
        b->capacity = need;
    }
    b->width = w;
    b->height = h;
}

static void swap_buffers(sw_buffer_t *a, sw_buffer_t *b)
{
    sw_buffer_t t = *a;
    *a = *b;
    *b = t;
}


/* ------------------------------------------------------------------------ */
/* UI thread                                                                */
/* ------------------------------------------------------------------------ */

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    video_canvas_t *canvas = data;
    sw_context_t *ctx;
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;
    double vw, vh, scale_x = 1.0, scale_y = 1.0;
    double dw, dh, ox, oy;
    unsigned int bw, bh;
    float par;
    int filter;

    CANVAS_LOCK();
    ctx = canvas->renderer_context;
    if (ctx == NULL) {
        CANVAS_UNLOCK();
        return FALSE;
    }
    if (ctx->frame_pending) {
        swap_buffers(&ctx->ready, &ctx->display);
        ctx->frame_pending = 0;
    }
    bw = ctx->display.width;
    bh = ctx->display.height;
    par = ctx->display.pixel_aspect_ratio;
    CANVAS_UNLOCK();

    /* background (grey in a window, black in fullscreen - like VICE) */
    if (ui_is_fullscreen_from_canvas(canvas)) {
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    } else {
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    }
    cairo_paint(cr);

    if (ctx->display.pixels == NULL || bw == 0 || bh == 0) {
        return TRUE;
    }

    vw = gtk_widget_get_allocated_width(widget);
    vh = gtk_widget_get_allocated_height(widget);
    if (vw < 1.0 || vh < 1.0) {
        return TRUE;
    }

    /* same layout rules as the DirectX/OpenGL renderers */
    if (canvas->videoconfig->aspect_mode != VIDEO_ASPECT_MODE_NONE) {
        double viewport_aspect = vw / vh;
        double emulated_aspect = (double)bw / (double)bh;

        if (canvas->videoconfig->aspect_mode == VIDEO_ASPECT_MODE_TRUE) {
            emulated_aspect *= par > 0.0f ? par : 1.0;
        } else {
            emulated_aspect *= canvas->videoconfig->aspect_ratio;
        }
        if (emulated_aspect < viewport_aspect) {
            scale_x = emulated_aspect / viewport_aspect;
        } else {
            scale_y = viewport_aspect / emulated_aspect;
        }
    }
    dw = vw * scale_x;
    dh = vh * scale_y;
    ox = floor((vw - dw) / 2.0);
    oy = floor((vh - dh) / 2.0);

    canvas->screen_display_w = dw;
    canvas->screen_display_h = dh;
    canvas->screen_origin_x = ox;
    canvas->screen_origin_y = oy;

    surface = cairo_image_surface_create_for_data((unsigned char *)ctx->display.pixels,
                                                  CAIRO_FORMAT_RGB24,
                                                  (int)bw, (int)bh,
                                                  (int)bw * 4);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return TRUE;
    }

    filter = canvas->videoconfig->glfilter;

    cairo_save(cr);
    cairo_rectangle(cr, ox, oy, dw, dh);
    cairo_clip(cr);
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, dw / (double)bw, dh / (double)bh);
    cairo_set_source_surface(cr, surface, 0.0, 0.0);
    pattern = cairo_get_source(cr);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_PAD);
    cairo_pattern_set_filter(pattern,
                             filter == 0 ? CAIRO_FILTER_NEAREST :
                             filter == 1 ? CAIRO_FILTER_BILINEAR :
                                           CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_surface_destroy(surface);
    return TRUE;
}

/** \brief Called by the window's frame clock (UI thread) */
static void sw_queue_redraw(GdkFrameClock *clock, video_canvas_t *canvas)
{
    sw_context_t *ctx;
    int pending, mw, mh;

    ui_update_statusbars();

    CANVAS_LOCK();
    ctx = canvas->renderer_context;
    if (ctx == NULL) {
        CANVAS_UNLOCK();
        return;
    }
    pending = ctx->frame_pending;
    mw = ctx->min_width;
    mh = ctx->min_height;
    CANVAS_UNLOCK();

    if (canvas->event_box == NULL) {
        return;
    }
    if (mw != ctx->min_width_set || mh != ctx->min_height_set) {
        gtk_widget_set_size_request(canvas->event_box, mw, mh);
        ctx->min_width_set = mw;
        ctx->min_height_set = mh;
    }
    if (pending) {
        gtk_widget_queue_draw(canvas->event_box);
    }
}

static void sw_initialise_canvas(video_canvas_t *canvas)
{
    sw_context_t *ctx;

    if (sw_log == LOG_DEFAULT) {
        sw_log = log_open("Software renderer");
    }

    ctx = lib_calloc(1, sizeof(sw_context_t));
    ctx->min_width_set = -1;
    ctx->min_height_set = -1;

    CANVAS_LOCK();
    canvas->renderer_context = ctx;
    CANVAS_UNLOCK();

    gtk_widget_set_app_paintable(canvas->event_box, TRUE);
    g_signal_connect_unlocked(canvas->event_box, "draw", G_CALLBACK(on_draw), canvas);

    log_message(sw_log, "Using the Cairo software renderer (no GPU needed).");
}

static void sw_destroy_context(video_canvas_t *canvas)
{
    sw_context_t *ctx;

    CANVAS_LOCK();
    ctx = canvas->renderer_context;
    if (ctx != NULL) {
        lib_free(ctx->back.pixels);
        lib_free(ctx->ready.pixels);
        lib_free(ctx->display.pixels);
        lib_free(ctx);
        canvas->renderer_context = NULL;
    }
    CANVAS_UNLOCK();
}


/* ------------------------------------------------------------------------ */
/* Emulation thread                                                         */
/* ------------------------------------------------------------------------ */

static void sw_update_context(video_canvas_t *canvas, unsigned int width, unsigned int height)
{
    sw_context_t *ctx;

    CANVAS_LOCK();
    ctx = canvas->renderer_context;
    if (ctx != NULL) {
        ctx->emulated_width_next = width;
        ctx->emulated_height_next = height;
        ctx->pixel_aspect_ratio_next = canvas->geometry->pixel_aspect_ratio;

        if (canvas->videoconfig->aspect_mode == VIDEO_ASPECT_MODE_TRUE) {
            ctx->min_width = (int)ceil((double)width * ctx->pixel_aspect_ratio_next);
        } else {
            ctx->min_width = (int)width;
        }
        ctx->min_height = (int)height;
    }
    CANVAS_UNLOCK();
}

static void sw_refresh_rect(video_canvas_t *canvas,
                            unsigned int xs, unsigned int ys,
                            unsigned int xi, unsigned int yi,
                            unsigned int w, unsigned int h)
{
    sw_context_t *ctx;
    unsigned int bw, bh;

    CANVAS_LOCK();
    ctx = canvas->renderer_context;
    if (ctx == NULL || ctx->emulated_width_next == 0 || ctx->emulated_height_next == 0) {
        CANVAS_UNLOCK();
        return;
    }
    bw = ctx->emulated_width_next;
    bh = ctx->emulated_height_next;
    buffer_ensure(&ctx->back, bw, bh);
    ctx->back.pixel_aspect_ratio = ctx->pixel_aspect_ratio_next;
    CANVAS_UNLOCK();

    /* only the emulation thread touches the back buffer */
    if (xi + w > bw) {
        w = bw > xi ? bw - xi : 0;
    }
    if (yi + h > bh) {
        h = bh > yi ? bh - yi : 0;
    }
    if (w > 0 && h > 0) {
        video_canvas_render(canvas, (uint8_t *)ctx->back.pixels, w, h, xs, ys, xi, yi, bw * 4);
    }

    CANVAS_LOCK();
    swap_buffers(&ctx->back, &ctx->ready);
    ctx->frame_pending = 1;
    CANVAS_UNLOCK();
}

static void sw_set_palette(video_canvas_t *canvas)
{
    int i;
    struct palette_s *palette = canvas ? canvas->palette : NULL;
    video_render_color_tables_t *color_tables;

    if (palette == NULL) {
        return;
    }
    color_tables = &canvas->videoconfig->color_tables;

    /* Cairo RGB24 is a native endian 0x00RRGGBB word */
    for (i = 0; i < palette->num_entries; i++) {
        palette_entry_t c = palette->entries[i];
        uint32_t code = ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8)
                        | (uint32_t)c.blue | (0xffU << 24);
        video_render_setphysicalcolor(canvas->videoconfig, i, code, 32);
    }
    for (i = 0; i < 256; i++) {
        video_render_setrawrgb(color_tables, (unsigned int)i,
                               (uint32_t)i << 16, (uint32_t)i << 8, (uint32_t)i);
    }
    video_render_setrawalpha(color_tables, 0xffU << 24);
    video_render_initraw(canvas->videoconfig);
}

vice_renderer_backend_t vice_software_backend = {
    sw_initialise_canvas,
    sw_update_context,
    sw_destroy_context,
    sw_refresh_rect,
    sw_queue_redraw,
    sw_set_palette
};


/* ------------------------------------------------------------------------ */
/* Selection                                                                */
/* ------------------------------------------------------------------------ */

static int renderer_choice = EMUDIS_RENDERER_AUTO;

static int set_renderer_choice(int val, void *param)
{
    if (val < EMUDIS_RENDERER_AUTO || val > EMUDIS_RENDERER_SOFTWARE) {
        return -1;
    }
    renderer_choice = val;
    return 0;
}

static const resource_int_t resources_int[] = {
    { "EmuDisRenderer", EMUDIS_RENDERER_AUTO, RES_EVENT_NO, NULL,
      &renderer_choice, set_renderer_choice, NULL },
    RESOURCE_INT_LIST_END
};

static const cmdline_option_t cmdline_options[] = {
    { "-softrender", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "EmuDisRenderer", (void *)EMUDIS_RENDERER_SOFTWARE,
      NULL, "Use the software renderer (no OpenGL/DirectX needed, for VMs)" },
    { "-gpurender", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "EmuDisRenderer", (void *)EMUDIS_RENDERER_GPU,
      NULL, "Always use the OpenGL/DirectX renderer" },
    CMDLINE_LIST_END
};

int software_renderer_resources_init(void)
{
    return resources_register_int(resources_int);
}

int software_renderer_cmdline_options_init(void)
{
    return cmdline_register_options(cmdline_options);
}

/* The choice made in the Tools menu is kept in its own small file in the
 * user config dir, so it can be saved without saving all other settings. */
static int saved_choice = -1;

static char *saved_choice_path(void)
{
    return util_join_paths(archdep_user_config_path(), "renderer.txt", NULL);
}

int software_renderer_get_saved(void)
{
    if (saved_choice < 0) {
        char *path = saved_choice_path();
        FILE *fp = fopen(path, "r");
        char line[32] = "";

        saved_choice = EMUDIS_RENDERER_AUTO;
        if (fp != NULL) {
            if (fgets(line, sizeof line, fp) != NULL) {
                if (strncmp(line, "software", 8) == 0) {
                    saved_choice = EMUDIS_RENDERER_SOFTWARE;
                } else if (strncmp(line, "gpu", 3) == 0) {
                    saved_choice = EMUDIS_RENDERER_GPU;
                }
            }
            fclose(fp);
        }
        lib_free(path);
    }
    return saved_choice;
}

int software_renderer_set_saved(int choice)
{
    char *path;
    FILE *fp;

    archdep_mkdir_recursive(archdep_user_config_path(), 0755);
    path = saved_choice_path();
    fp = fopen(path, "w");
    lib_free(path);
    if (fp == NULL) {
        return -1;
    }
    fputs(choice == EMUDIS_RENDERER_SOFTWARE ? "software\n" :
          choice == EMUDIS_RENDERER_GPU ? "gpu\n" : "auto\n", fp);
    fclose(fp);
    saved_choice = choice;
    renderer_choice = EMUDIS_RENDERER_AUTO;     /* the saved choice applies */
    return 0;
}

/** \brief The effective choice: command line/resource first, then the file */
int software_renderer_get_choice(void)
{
    if (renderer_choice != EMUDIS_RENDERER_AUTO) {
        return renderer_choice;
    }
    return software_renderer_get_saved();
}

/** \brief The renderer actually in use (set when the window is created) */
static bool software_active = false;

void software_renderer_set_active(bool active)
{
    software_active = active;
}

bool software_renderer_is_active(void)
{
    return software_active;
}

bool software_renderer_wanted(bool gpu_available)
{
    const char *env = getenv("C64EMUDIS_RENDERER");

    if (env != NULL) {
        if (strcmp(env, "software") == 0 || strcmp(env, "sw") == 0 || strcmp(env, "cairo") == 0) {
            return true;
        }
        if (strcmp(env, "gpu") == 0 || strcmp(env, "directx") == 0 || strcmp(env, "opengl") == 0) {
            return false;
        }
    }
    switch (software_renderer_get_choice()) {
        case EMUDIS_RENDERER_SOFTWARE:
            return true;
        case EMUDIS_RENDERER_GPU:
            return false;
        default:
            return !gpu_available;
    }
}
