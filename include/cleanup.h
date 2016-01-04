/*
 * cleanup.h
 *
 * Copyright (c) 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: cleanup.h,v 1.1 2002/09/14 23:26:53 marius Exp $
 */

#ifndef CLEANUP_H
#define CLEANUP_H

typedef struct cleanup cleanup_t;

cleanup_t *cleanup_new(void);
cleanup_t *cleanup_free(cleanup_t *);
int        cleanup_add(cleanup_t *, void (*)(void *), void *);
int        cleanup_remove(cleanup_t *, void (*)(void *), void *);
void       cleanup_cleanup(cleanup_t *);

/* Utility */
void       cleanup_close(void *);

#endif /* CLEANUP_H */
