/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Nirvana Desktop: the strings shown to users go through gettext (domain "nirvana"). */
#ifndef NIRVANA_I18N_H
#define NIRVANA_I18N_H

#ifdef NIRVANA_ENABLE_NLS
#include <libintl.h>
#include <locale.h>
#define _(s) gettext(s)
#define nirvana_i18n_init()                                   \
    do {                                                      \
        setlocale(LC_ALL, "");                                \
        bindtextdomain(NIRVANA_GETTEXT_PACKAGE, NIRVANA_LOCALEDIR); \
        textdomain(NIRVANA_GETTEXT_PACKAGE);                  \
    } while (0)
#else
#include <locale.h>
#define _(s) (s)
#define nirvana_i18n_init() setlocale(LC_ALL, "")
#endif
#define N_(s) (s)

#endif
