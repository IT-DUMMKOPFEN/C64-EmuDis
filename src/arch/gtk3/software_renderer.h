/**
 * \file software_renderer.h
 * \brief   Software (Cairo) renderer for the GTK3 UI - C64-EmuDis addition.
 *
 * This file is part of C64-EmuDis (GPL-2.0-or-later).
 */

#ifndef VICE_SOFTWARE_RENDERER_H
#define VICE_SOFTWARE_RENDERER_H

#include <stdbool.h>
#include "videoarch.h"
#include "cmdline.h"

/** \brief Values of the "EmuDisRenderer" resource */
enum {
    EMUDIS_RENDERER_AUTO = 0,     /**< GPU renderer, software if the GPU fails */
    EMUDIS_RENDERER_GPU = 1,      /**< always OpenGL (Linux/macOS) or DirectX (Windows) */
    EMUDIS_RENDERER_SOFTWARE = 2  /**< always the Cairo software renderer */
};

extern vice_renderer_backend_t vice_software_backend;

int  software_renderer_resources_init(void);
int  software_renderer_cmdline_options_init(void);
int  software_renderer_get_choice(void);
int  software_renderer_get_saved(void);
int  software_renderer_set_saved(int choice);
void software_renderer_set_active(bool active);
bool software_renderer_is_active(void);
bool software_renderer_wanted(bool gpu_available);

#endif
