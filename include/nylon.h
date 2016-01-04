/*
 * nylon.h
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: nylon.h,v 1.3 2002/10/26 17:36:30 marius Exp $
 */

#ifndef NYLON_H
#define NYLON_H

/* Macros to set/clear/test flags. */
#define SET(t, f)       ((t) |= (f))
#define CLR(t, f)       ((t) &= ~(f))
#define ISSET(t, f)     ((t) & (f))

#endif /* NYLON_H */
