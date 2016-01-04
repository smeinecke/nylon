/*
 * net.h
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org>
 * 
 * $Id: net.h,v 1.6.2.1 2006/08/19 22:51:38 marius Exp $
 */

#ifndef NET_H
#define NET_H

#define NET_STATE_EOFPENDING 0x1

#define NET_SUPPORT_SOCKS4 0x01
#define NET_SUPPORT_SOCKS5 0x02

/* Return codes from negotation routines. */
#define NET_FAIL -1		/* Negotiation failed */
#define NET_NOPROXY -2		/* Negotiation succeeded - but don't proxy. */

struct conndesc {
	struct addrinfo *mirror_ai;
	struct addrinfo *bind_ai;
	struct addrinfo *serv_ai;
	struct addrinfo *chain_ai;
	int              support;
};

int net_setup(char *, char *, char *, char *, char *, int);

#endif /* NET_H */
