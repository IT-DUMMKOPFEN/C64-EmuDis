/** \file   actions-help.c
 * \brief   UI action implementations for help-related dialogs
 *
 * \author  Bas Wassink <b.wassink@ziggo.nl>
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
 */

#include "vice.h"

#include <gtk/gtk.h>
#include <stddef.h>
#include <stdbool.h>

#include "archdep.h"
#include "basedialogs.h"
#include "debug_gtk3.h"
#include "lib.h"
#include "log.h"
#include "uiabout.h"
#include "uiactions.h"
#include "uicmdline.h"
#include "uicompiletimefeatures.h"
#include "uisettings.h"
#include "util.h"

#include "actions-help.h"
#include "emudis.h"


/** \brief  Open the PDF manual using an external application
 *
 * \param[in]   self    action map
 *
 * \note    Keep the debug_gtk3() calls for now, this code hardly works on
 *          Windows at all and needs work.
 */
static void help_manual_action(ui_action_map_t *self)
{
    /* C64-EmuDis: bundled HTML manual or the online manual (see emudis_manual.c);
       the PDF manual is not part of the packages */
    emudis_show_manual();
    ui_action_finish(ACTION_HELP_MANUAL);
}

/** \brief  Pop up dialog with command line options help
 *
 * \param[in]   self    action map
 */
static void help_command_line_action(ui_action_map_t *self)
{
    uicmdline_dialog_show();
}

/** \brief  Pop up dialog with compile time features help
 *
 * \param[in]   self    action map
 */
static void help_compile_time_action(ui_action_map_t *self)
{
    uicompiletimefeatures_dialog_show();
}

/** \brief  Pop up setting dialog and activate the hotkeys editor
 *
 * \param[in]   self    action map
 */
static void help_hotkeys_action(ui_action_map_t *self)
{
    if (machine_class != VICE_MACHINE_VSID) {
        ui_settings_dialog_show("host/hotkeys");
    } else {
        ui_settings_dialog_show("hotkeys");
    }
}

/** \brief  Pop up About dialog
 *
 * \param[in]   self    action map
 */
static void help_about_action(ui_action_map_t *self)
{
    ui_about_dialog_show();
}


/** \brief  List of help-related actions */
static const ui_action_map_t help_actions[] = {
    {   .action   = ACTION_HELP_MANUAL,
        .handler  = help_manual_action,
        .blocks   = true,
        .uithread = true
    },
    {   .action  = ACTION_HELP_COMMAND_LINE,
        .handler = help_command_line_action,
        .blocks  = true,
        .dialog  = true,
    },
    {   .action  = ACTION_HELP_COMPILE_TIME,
        .handler = help_compile_time_action,
        .blocks  = true,
        .dialog  = true
    },
    {   .action  = ACTION_HELP_HOTKEYS,
        .handler = help_hotkeys_action,
        /* FIXME:   Find a way for the settings dialog to signal it's going down
         *          without resorting to a ton of ui_action_finish(ACTION_FOO)
         *          calls for each shortcut into the settings dialog
         *
         *          Perhaps a `.settings=true` that acts like `.dialog=true`,
         *          passed for each action popping up the settings dialog and
         *          activating a node ?
         */
        /*
        .blocks = true,
        .dialog = true
        */
    },
    {   .action  = ACTION_HELP_ABOUT,
        .handler = help_about_action,
        .blocks  = true,
        .dialog  = true
    },
    UI_ACTION_MAP_TERMINATOR
};


/** \brief  Register help actions */
void actions_help_register(void)
{
    ui_actions_register(help_actions);
}
