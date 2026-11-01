/**
 * \file emudis_manual.c
 * \brief   C64-EmuDis: open the VICE manual and web pages
 *
 * VICE's "Browse VICE manual" looked for vice.pdf, which is not part of the
 * C64-EmuDis packages (building it needs TeX). The packages now contain the
 * manual as one HTML file (made with makeinfo from doc/vice.texi), which is
 * opened in the default browser; without it the online manual is opened.
 *
 * \author IT-Dummkopfen
 */

/* This file is part of C64-EmuDis (GPL-2.0-or-later). */

#include "vice.h"

#include <gtk/gtk.h>
#include <stdbool.h>
#include <string.h>

#ifdef WINDOWS_COMPILE
#include <windows.h>
#include <shellapi.h>
#endif

#include "archdep.h"
#include "lib.h"
#include "log.h"
#include "util.h"
#include "vice_gtk3.h"

#include "emudis.h"

#define EMUDIS_MANUAL_FILE  "vice-manual.html"
#define EMUDIS_MANUAL_URL   "https://vice-emu.sourceforge.io/vice_toc.html"

/** \brief  Open a URI (http(s):// or file://) in the default application */
bool emudis_open_uri(const char *uri)
{
#ifdef WINDOWS_COMPILE
    wchar_t *wuri;
    int len;
    HINSTANCE res;

    len = MultiByteToWideChar(CP_UTF8, 0, uri, -1, NULL, 0);
    wuri = lib_malloc((size_t)len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, uri, -1, wuri, len);
    res = ShellExecuteW(NULL, L"open", wuri, NULL, NULL, SW_SHOWNORMAL);
    lib_free(wuri);
    if ((INT_PTR)res > 32) {
        return true;
    }
    log_error(LOG_DEFAULT, "could not open '%s' (ShellExecute %d).", uri, (int)(INT_PTR)res);
    return false;
#else
    GError *error = NULL;
    gchar *argv[3];

# ifdef MACOS_COMPILE
    argv[0] = "/usr/bin/open";
# else
    if (gtk_show_uri_on_window(NULL, uri, GDK_CURRENT_TIME, &error)) {
        return true;
    }
    g_clear_error(&error);
    argv[0] = "xdg-open";
# endif
    argv[1] = (gchar *)uri;
    argv[2] = NULL;
    if (g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        return true;
    }
    log_error(LOG_DEFAULT, "could not open '%s': %s", uri,
              error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return false;
#endif
}

/* look for the bundled HTML manual (Windows: bin/../doc, macOS bundle:
   Resources/share/vice/doc, Linux folder/AppImage: next to the program) */
static char *find_manual(void)
{
    const char *boot = archdep_boot_path();
    const char *subdirs[] = {
        "doc", "../doc", "../share/vice/doc", "../share/c64-emudis/doc", NULL
    };
    int i;

    for (i = 0; subdirs[i] != NULL; i++) {
        char *path = util_join_paths(boot, subdirs[i], EMUDIS_MANUAL_FILE, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            return path;
        }
        lib_free(path);
    }
    return NULL;
}

/** \brief  Show the VICE manual (bundled HTML, else the online manual) */
void emudis_show_manual(void)
{
    char *path = find_manual();

    if (path != NULL) {
        gchar *uri = g_filename_to_uri(path, NULL, NULL);
        bool ok = (uri != NULL) && emudis_open_uri(uri);
        log_message(LOG_DEFAULT, "VICE manual: %s", path);
        g_free(uri);
        lib_free(path);
        if (ok) {
            return;
        }
    } else {
        log_message(LOG_DEFAULT, "VICE manual not bundled, opening %s", EMUDIS_MANUAL_URL);
    }
    if (!emudis_open_uri(EMUDIS_MANUAL_URL)) {
        vice_gtk3_message_error(NULL, "VICE manual",
                                "The VICE manual could not be opened.\n\n"
                                "You can read it online at\n%s", EMUDIS_MANUAL_URL);
    }
}
