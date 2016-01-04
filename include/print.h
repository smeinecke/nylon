/*
 * print.h
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: print.h,v 1.3 2002/09/14 18:49:30 marius Exp $
 */

#ifndef PRINT_H
#define PRINT_H

void print_setup(int, int);
void errv(int, int, const char *, ...);
void errxv(int, int, const char *, ...);
void warnv(int, const char *, ...);
void warnxv(int, const char *, ...);

#endif /* PRINT_H */
