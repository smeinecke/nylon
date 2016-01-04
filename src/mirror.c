/*
 * mirror.c
 *
 * Copyright 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: mirror.c,v 1.6 2003/06/08 05:56:47 marius Exp $
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <stdio.h>
#include <netdb.h>
#include <unistd.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "net.h"
#include "print.h"

int
mirror_setup(struct conndesc *conn)
{
	int remsock;
	struct addrinfo *ai = conn->mirror_ai;

	if ((remsock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
		warnv(0, "socket()");
		return (-1);
	}

	if ((ai = conn->bind_ai) != NULL &&
	    bind(remsock, ai->ai_addr, ai->ai_addrlen) == -1) {
		warnv(0, "bind()");
		return (-1);
	}

	ai = conn->mirror_ai;

	if (connect(remsock, ai->ai_addr, ai->ai_addrlen) == -1) {
		close(remsock);
		warnv(0, "connect()");
		return (-1);
	}

	return (remsock);
}
