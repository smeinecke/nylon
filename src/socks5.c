/*
 * socks5.c
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: socks5.c,v 1.12 2003/06/08 07:40:00 marius Exp $
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "atomicio.h"
#include "print.h"
#include "net.h"

#define SOCKS5_ATYP_IPV4      1
#define SOCKS5_ATYP_FQDN      3
#define SOCKS5_CD_CONNECT     1
#define SOCKS5_CD_BIND        2
#define SOCKS5_CD_UDP_ASSOC   3

/*
 * XXX proper error replies
 */

/* Request */
struct socks5_req {
	u_char    vn;          /* Version number */
	u_char    cd;          /* Command */
	u_char    rsv;         /* Reserved */
	u_char    atyp;        /* Address type */
	u_int32_t destaddr;    /* Dest address */
	u_int16_t destport;    /* Dest port */
};

/* Version reply */
struct socks5_v_repl {
	u_char ver;       /* Version */
	u_char res;       /* Response */
};

static int socks5_connect(int, struct sockaddr_in *, struct socks5_req *,
    struct conndesc *);
static int socks5_bind(int, struct sockaddr_in *, struct socks5_req *);

int
socks5_negotiate(int clisock, struct conndesc *conn)
{
	u_int i;
	char hostname[256];
	u_char nmethods, len, junk;
	struct sockaddr_in rem_in;
	struct socks5_req req5;
	struct socks5_v_repl rep5;
	struct hostent *hent;

	req5.vn = 5;
	req5.rsv = 0;

	/*
	 * Start by retrieving number of methods, version number has
	 * already been consumed by the calling procedure
	 */

	if (atomicio(read, clisock, &nmethods, 1) != 1) {
		warnv(1, "read()");
		return (-1);
	}

	/* Eat up methods */

	i = 0;
	while (i++ < nmethods)
		if (atomicio(read, clisock, &junk, 1) != 1) {
			warnv(1, "read()");
			return (-1);
		}

	/*
	 * We don't support any authentication methods yet, so simply
	 * ignore it and send reply with no authentication required.
	 */

	rep5.ver = 5;
	rep5.res = 0;

	if (atomicio(write, clisock, &rep5, 2) != 2) {
		warnv(1, "write()");
		return (-1);
	}

	/* Receive data up to atyp */
	if (atomicio(read, clisock, &req5, 4) != 4) {
		warnv(1, "read()");
		return (-1);
	}
	if (req5.vn != 5)
		return (-1);

	memset(&rem_in, 0, sizeof(rem_in));

	switch (req5.atyp) {
	case SOCKS5_ATYP_IPV4:
		if (atomicio(read, clisock, &req5.destaddr, 4) != 4) {
			warnv(1, "read()");
			return (-1);
		}
		rem_in.sin_family = AF_INET;
		rem_in.sin_addr.s_addr = req5.destaddr;
		break;
	case SOCKS5_ATYP_FQDN:
		if (atomicio(read, clisock, &len, 1) != 1) {
			warnv(1, "read()");
			return (-1);
		}
		if (atomicio(read, clisock, hostname, len) != len) {
			warnv(1, "read()");
			return (-1);
		}
		hostname[len] = '\0';
		if ((hent = gethostbyname(hostname)) == NULL) {
			/* XXX no hstrerror() on solaris */
#ifndef __sun__
			warnxv(1, "gethostbyname(): %s", hstrerror(h_errno));
#endif /* __sun__ */
			return (-1);
		}
		rem_in.sin_family = AF_INET;
		rem_in.sin_addr = *(struct in_addr *)hent->h_addr;
		break;
	default:
		return (-1);
	}

	if (atomicio(read, clisock, &req5.destport, 2) != 2) {
		warnv(1, "read()");
		return (-1);
	}

	rem_in.sin_port = req5.destport;

	/*
	 * Now we have a filled in in_addr for the target host:
	 * target_in, no socket yet.  This is provided by the command
	 * specific functions multiplexed in the next switch
	 * statement.
	 */

	switch (req5.cd) {
	case SOCKS5_CD_CONNECT:
		return (socks5_connect(clisock, &rem_in, &req5, conn));
	case SOCKS5_CD_BIND:
		return (socks5_bind(clisock, &rem_in, &req5));
	case SOCKS5_CD_UDP_ASSOC:
	default:
		return (-1);
	}
}

static int
socks5_connect(int clisock, struct sockaddr_in *rem_in, struct socks5_req *req5,
    struct conndesc *conn)
{
	int remsock;
	struct addrinfo *ai;

	/* XXX use bind_ai for socket creation, also */
	if ((remsock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		warnv(0, "socket()");
		return (-1);
	}

	if ((ai = conn->bind_ai) != NULL)
        if (conn->bind_if_name != NULL) {
            if (setsockopt(remsock, SOL_SOCKET, SO_BINDTODEVICE, conn->bind_if_name, IFNAMSIZ-1) == -1) {
                warnv(0, "bind device()");
                return (-1);
            }
        }

		if (bind(remsock, ai->ai_addr, ai->ai_addrlen) == -1) {
			warnv(0, "bind()");
			return (-1);
		}

	if (connect(remsock, (struct sockaddr *)rem_in, sizeof(*rem_in)) == -1) {
		warnv(0, "connect()");
		req5->cd = 1;
	} else {
		req5->cd = 0;
	}

	/* getpeername() */

	req5->atyp = SOCKS5_ATYP_IPV4;
	/* XXX fill in address and port of our server (getsockname()) */

	if (atomicio(write, clisock, req5, 10) != 10) {
		warnv(1, "write()");
		goto fail;
	}

	if (req5->cd == 1)
		goto fail;

	return (remsock);

 fail:
	close(remsock);
	return (-1);
}

static int
socks5_bind(int clisock, struct sockaddr_in *tgt_in, struct socks5_req *req5)
{
        struct sockaddr_in cli_in;
	socklen_t len;
	int tgtsock = -1, listensock;

	if ((listensock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		warnv(1, "socket()");
                return (-1);
	}

        if (bind(listensock, (struct sockaddr *)tgt_in, sizeof(*tgt_in)) == -1) {
                warnv(1, "bind()");
                goto out;
        }

        if (listen(listensock, 1) == -1) {
		warnv(1, "listen()");
		goto out;
        }

	/* Reply: success */
	req5->atyp = SOCKS5_ATYP_IPV4;
	req5->cd = 0;

	if (atomicio(write, clisock, req5, 10) != 10)
		goto out;

	len = sizeof(cli_in);
        if ((tgtsock = accept(listensock, (struct sockaddr *)&cli_in, &len)) == -1)
		goto out;

        req5->destaddr = cli_in.sin_addr.s_addr;
        req5->destport = cli_in.sin_port;

	/* Send second reply */
	if (atomicio(write, clisock, req5, 10) != 10) {
		close(tgtsock);
		tgtsock = -1;
	}

 out:
	close(listensock);

	return (tgtsock);
}
