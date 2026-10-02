/*
 * print.h
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: print.h,v 1.2 2002/09/24 19:02:57 marius Exp $
 */

#ifndef PRINT_H
#define PRINT_H

#if defined(__GNUC__) || defined(__clang__)
#define NYLON_NORETURN __attribute__((noreturn))
#else
#define NYLON_NORETURN
#endif

void print_setup(int, int);
void errv(int, int, const char *, ...) NYLON_NORETURN;
void errxv(int, int, const char *, ...) NYLON_NORETURN;
void warnv(int, const char *, ...);
void warnxv(int, const char *, ...);
#endif /* PRINT_H */
