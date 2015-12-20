/*
 * socks4.c
 *
 * Copyright (c) 2001, 2002, 2006 Marius Aamodt Eriksen <marius@monkey.org>
 *
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>

#include <sys/uio.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "atomicio.h"
#include "print.h"
#include "net.h"
#include "socks4.h"

#define SOCKS4_MAX_USERID   1024
#define SOCKS4_CD_CONNECT   1
#define SOCKS4_CD_RESOLVE   240
#define SOCKS4_CD_BIND      2
#define SOCKS4_CD_GRANT     90
#define SOCKS4_CD_REJECT    91

struct socks4_hdr {
	u_char    vn;
	u_char    cd;
	u_int16_t destport;
	u_int32_t destaddr;
};

static int _socks4_tryconnect(int, struct sockaddr_in *,
    struct socks4_hdr *, struct conndesc *);

static int _getstr(int sock, char *buf, int len) {
	u_char byte;
	int pos;

	for (pos = 0; pos < len; pos++) {
		if (atomicio(read, sock, &byte, 1) != 1)
			return -1;

		buf[pos] = byte;

		if (byte == 0)
			return pos;
	}

	return -1;
}

int
socks4_negotiate(int clisock, struct conndesc *conn)
{
	u_char data, *addr;
	char hostname[256];
	int ret;
	struct socks4_hdr hdr4;
	struct sockaddr_in rem_in;
	struct hostent *hent;

	/* This is already implied ... */
	hdr4.vn = 4;

	ret = -1;

	/* Get the seven first bytes after version, until USERID */
	if (atomicio(read, clisock, &hdr4.cd, 7) != 7)
		return (-1);

	switch (hdr4.cd) {
	case SOCKS4_CD_CONNECT:
	case SOCKS4_CD_RESOLVE:
		/* We can only do connect & resolve. */
		break;
	default:
		warnxv(0, "Client attempted unsupported SOCKS4 command %d",
		    hdr4.cd);
		return (-1);
	};

	/* Eat the username; it is not used */
	while ((ret = atomicio(read, clisock, &data, 1)) == 1 && data != 0);

	if (ret != 1)
		return (-1);

	memset(&rem_in, 0, sizeof(rem_in));
	rem_in.sin_family = AF_INET;
	rem_in.sin_port = hdr4.destport;

    	addr = (unsigned char *)&hdr4.destaddr;
    	/* SOCKS4A or tor-resolve? */
 	if ((addr[0] == 0 && addr[1] == 0 && addr[2] == 0 && addr[3] != 0)
	    || (hdr4.cd == SOCKS4_CD_RESOLVE && hdr4.destport == 0)) {
		if (_getstr(clisock, hostname, sizeof(hostname)) < 0)
			return (-1);

		if ((hent = gethostbyname(hostname)) == NULL) {
			hdr4.cd = SOCKS4_CD_REJECT;
		} else {
			rem_in.sin_addr = *(struct in_addr *)hent->h_addr;
			/*
			 * Send back the resolved address as well, for
			 * tor-resolve.
			 */
			hdr4.destaddr = rem_in.sin_addr.s_addr;
		}
	} else {
		rem_in.sin_addr.s_addr = hdr4.destaddr;
	}

 	return (_socks4_tryconnect(clisock, &rem_in, &hdr4, conn));
}

static int
_socks4_tryconnect(int clisock, struct sockaddr_in *rem_in,
    struct socks4_hdr *hdr4, struct conndesc *conn)
{
	struct addrinfo *ai;
	int ret, sock = -1;

	if (hdr4->cd != SOCKS4_CD_CONNECT)
		goto fail_reply;

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return (-1);

	if ((ai = conn->bind_ai) != NULL) {
        if (conn->bind_if_name != NULL &&
            (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, conn->bind_if_name, IFNAMSIZ-1) == -1)) {
			warnv(0, "bind device()");
			return (-1);
        }

		if (bind(sock, ai->ai_addr, ai->ai_addrlen) == -1) {
			warnv(0, "bind()");
			return (-1);
		}
	}

	if (connect(sock, (struct sockaddr *)rem_in,
		sizeof(*rem_in)) == -1) {
		warnv(0, "connect()");
		hdr4->cd = SOCKS4_CD_REJECT;
	} else {
		hdr4->cd = SOCKS4_CD_GRANT;
	}

fail_reply:
	hdr4->vn = 0;

	if (hdr4->cd == SOCKS4_CD_RESOLVE) {
		/*
		 * This is a successfull resolve, we need to set the
		 * return CD to grant.
		 */
		ret = NET_NOPROXY;
		hdr4->cd = SOCKS4_CD_GRANT;
	} else if (hdr4->cd != SOCKS4_CD_GRANT) {
		ret = NET_FAIL;
	} else {
		ret = sock;
	}

	if (atomicio(write, clisock, hdr4, sizeof(*hdr4)) != sizeof(*hdr4))
		ret = NET_FAIL;

	if (ret < 0)
		close(sock);

	/*
	 * When we've done a resolve now (and it's successfull), we
	 * want to return GRANT
	 */


	return (ret);
}
