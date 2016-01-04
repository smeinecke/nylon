/*
 * misc.c
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: misc.c,v 1.1 2002/10/01 23:26:49 marius Exp $
 */

#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

/* From OpenSSH */

char *
get_progname(char *argv0)
{
#ifdef HAVE___PROGNAME
        extern char *__progname;

        return __progname;
#else
        char *p;

        if (argv0 == NULL)
                return "unknown";       /* XXX */
        p = strrchr(argv0, '/');
        if (p == NULL)
                p = argv0;
        else
                p++;
        return p;
#endif
}
