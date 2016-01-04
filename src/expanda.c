/*
 * expanda.c
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: expanda.c,v 1.4 2002/11/18 06:45:07 marius Exp $
 */

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "expanda.h"

#define DEFAULT_ALLOC 8

static char **xalloc(u_int, char **);

char **
expanda(const char *_str)
{
	char **arr = NULL, *tok, *str;
	u_int i, ac;

	ac = i = 0;

	if ((str = strdup(_str)) == NULL)
		return (NULL);

	while((tok = strsep(&str, " ")) != NULL) {
		if (*tok == '\0')
			continue;

		if ((arr = xalloc(i, arr)) == NULL)
			goto fail;

		if ((arr[i++] = strdup(tok)) == NULL)
			goto fail;
	}

	if ((arr = xalloc(i, arr)) == NULL)
		goto fail;

	arr[i] = NULL;

	return (arr);

 fail:
	if (arr != NULL)
		freea(arr);
	free(str);
	return (NULL);
}

char **
freea(char **str)
{
	char **p = str;

	while (*p != NULL)
		free(*p++);

	free(str);

	return (NULL);
}

static char **
xalloc(u_int i, char **arr)
{
	char **xrr;
	u_int ac = i / DEFAULT_ALLOC;

	if (i % DEFAULT_ALLOC == 0) {
		xrr = realloc(arr, sizeof(char *) * DEFAULT_ALLOC * ++ac);
		if (xrr == NULL) {
			arr[i - 1] = NULL;
			freea(arr);
			return (NULL);
		}
		arr = xrr;
	}

	return (arr);
}
