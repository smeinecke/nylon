/*
 * socks5.h
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org.
 *
 * $Id: socks5.h,v 1.3 2002/12/02 14:57:20 marius Exp $
 */

#ifndef SOCKS5_H
#define SOCKS5_H

int socks5_negotiate(int, struct conndesc *);

#endif /* SOCKS5_H */
