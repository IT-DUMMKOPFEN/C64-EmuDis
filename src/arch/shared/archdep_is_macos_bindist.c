/** \file   archdep_is_macos_bindist.c
 * \brief   Determine if running from a macOS binary distribution
 *
 * \author  David Hogan <david.q.hogan@gmail.com>
 */

/*
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <stdlib.h>
#include <string.h>

#include "archdep_boot_path.h"
#include "archdep_defs.h"

#include "archdep_is_macos_bindist.h"


/** \brief  Determine if we're running a MacOS bindist
 *
 * \return  0 if bindist, -1 otherwise
 */
int archdep_is_macos_bindist(void) {
#ifdef MACOS_COMPILE
    /* C64-EmuDis: accept any app bundle name (upstream only accepted
       "VICE.app", so "C64-EmuDis.app" did not find its ROMs) */
    static const char *BINDIST_BOOT_SUFFIX = ".app/Contents/Resources/bin";
    char resolved_boot_path[PATH_MAX];
    size_t len, slen;

    if (realpath(archdep_boot_path(), resolved_boot_path) == NULL) {
        return 0;
    }
    len = strlen(resolved_boot_path);
    slen = strlen(BINDIST_BOOT_SUFFIX);
    if (len > slen && strcmp(resolved_boot_path + len - slen, BINDIST_BOOT_SUFFIX) == 0) {
        return 1;
    }
#endif
    return 0;
}
