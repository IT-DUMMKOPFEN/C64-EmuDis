/**
 * \file emudis_sdl.h
 * \brief   C64-EmuDis: SDL UI glue and the Android bridge
 *
 * \author IT-Dummkopfen
 */

/* This file is part of C64-EmuDis (GPL-2.0-or-later). */

#ifndef EMUDIS_SDL_H
#define EMUDIS_SDL_H

#include "vice_sdl.h"

/** \brief  Handle a C64-EmuDis user event (Android touch controls)
 *
 * \return  1 if the event was handled, 0 if it is not one of ours
 */
int emudis_sdl_handle_user_event(SDL_Event *e);

#endif
