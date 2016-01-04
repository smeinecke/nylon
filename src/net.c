/*
 * net.c
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: net.c,v 1.20.2.1 2006/08/19 22:51:39 marius Exp $
 */

#include <sys/types.h>
#include <sys/socket.h>
#ifdef __sun__
#include <sys/sockio.h>
#endif /* __sun__ */
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <event.h>
#include <unistd.h>
#include <assert.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "atomicio.h"
#include "access.h"
#include "cleanup.h"
#include "net.h"
#include "print.h"
#include "nylon.h"

/* Methods */
#include "socks4.h"
#include "socks5.h"
#include "mirror.h"

/* Only IPv4 ... for now */
#define MAKEHINTS(x) do {              \
	memset(&(x), 0, sizeof(x));    \
	(x).ai_family = PF_INET;       \
	(x).ai_socktype = SOCK_STREAM; \
} while (0);

#define PENDINGDATA(x) (x->pos > 0)

#define BUFFERSZ 1024

struct proxydesc {
	int                 sock;
	char                hostname[NI_MAXHOST];
	char                port[NI_MAXSERV];
	struct sockaddr_in  in;
	int                 state;
	struct iovec        iov;
	u_int               pos;
	struct event       *ev;
	struct proxydesc   *dst;
};

struct listenq {
	struct event          ev;
	int                   sock;
	struct conndesc      *conn;
	TAILQ_ENTRY(listenq)  next;
};


static TAILQ_HEAD(listenqh, listenq) listenq_head;
extern cleanup_t *cleanup;
static char connstr[512];

static struct addrinfo  *get_ai_from_ifip(char *, char *);
static struct addrinfo  *get_ai_from_addrpair(char *);
static void              proxy(int, short, void *);
static struct proxydesc *newdesc(u_int);
static struct proxydesc *freedesc(struct proxydesc *);
static void              schedule(struct proxydesc *);
static void              net_accept(int, short, void *);
static int               net_negotiate(int, struct conndesc *);
static int               net_setup_proxy(int, int);
static void              net_setup_proxy_cleanup(void *);
static void              net_setup_cleanup(void *);

/* From nylon.c */
void signal_setup(void);

/*
 * XXX circular buffers (begpos, endpos, etc.)
 * XXX collect listening sockets
 * XXX fix mess with err, vs. return ...
 * XXX on failure; indicate which connection (with connstr)
 */

void
_try_resolve_proxydesc(struct proxydesc *d, int flags)
{
	int error;

	error = getnameinfo((struct sockaddr *)&d->in, sizeof(d->in),
	    d->hostname, sizeof(d->hostname),
	    d->port, sizeof(d->port), flags);

	if (error != 0) {
		/*
		 * Best-effort: We don't care if it gets truncated.
		 * This is just for UI anyway.
		 */

		strlcpy(d->hostname,
		    inet_ntoa(d->in.sin_addr),
		    sizeof(d->hostname));
		snprintf(d->port, sizeof(d->port), "%d", ntohs(d->in.sin_port));
		warnxv(0, "Could not resolve for %s:%s (%s)",
		    d->hostname, d->port, gai_strerror(error));
	}
}

/*
 * Set up listening socket and add event
 */
int
net_setup(char *ifip_bind, char *ifip_connect, char *port, char *mirror_addr,
    char *chain_addr, int support)
{
	int servsock = -1, on = 1, error;
	struct conndesc *conn;
	struct addrinfo hints, *ai;
	struct listenq *lq;
	char xhost[NI_MAXHOST], xport[NI_MAXSERV];
	static char portstr[NI_MAXSERV];

	TAILQ_INIT(&listenq_head);

	if ((conn = calloc(1, sizeof(*conn))) == NULL)
		errv(0, 1, "calloc()");

	if (mirror_addr != NULL) {
		if ((conn->mirror_ai = get_ai_from_addrpair(mirror_addr)) == NULL)
			errxv(0, 1, "Error resolving host:pair address");
		if (port == NULL) {
			snprintf(portstr, sizeof(portstr), "%i",
			    ntohs(((struct sockaddr_in *)
				      conn->mirror_ai->ai_addr)->sin_port));
			port = portstr;
		}
	}

	if (chain_addr != NULL)
		if ((conn->chain_ai = get_ai_from_addrpair(chain_addr)) == NULL)
			errxv(0, 1, "Error resolving host:pair address");

	conn->support = support;

	if (ifip_bind != NULL) {
		if ((conn->serv_ai = get_ai_from_ifip(ifip_bind, port)) == NULL)
			errxv(0, 1, "Error resolving binding if/ip address");
	} else {
		MAKEHINTS(hints);
		hints.ai_flags = AI_PASSIVE;
		if ((error = getaddrinfo(NULL, port, &hints, &conn->serv_ai))
		    != 0)
			errxv(0, 1, "Unable to resolve port: %s: %s", port,
			    gai_strerror(error));
	}

	if (ifip_connect != NULL &&
	    (conn->bind_ai = get_ai_from_ifip(ifip_connect, NULL)) == NULL)
		errxv(0, 1, "Error resolving connecting if/ip address");

	for (ai = conn->serv_ai; ai != NULL; ai = ai->ai_next) {
		if ((servsock = socket(ai->ai_family,
			 ai->ai_socktype, ai->ai_protocol)) == -1)
			errv(0, 1, "socket()");

		if (setsockopt(servsock, SOL_SOCKET, SO_REUSEADDR,
			&on, sizeof(on)) == -1)
			warnv(0, "setsockopt()");

		if (fcntl(servsock, F_SETFL, O_NONBLOCK) == -1)
			errv(0, 1, "fcntl()");

		if (bind(servsock, ai->ai_addr, ai->ai_addrlen) == -1)
			errv(0, 1, "bind()");

		if (listen(servsock, 10) == -1)
			errv(0, 1, "listen()");

		if ((lq = calloc(1, sizeof(*lq))) == NULL)
			errv(0, 1, "calloc()");

		lq->conn = conn;
		lq->sock = servsock;

		if (getnameinfo(ai->ai_addr, ai->ai_addrlen, xhost,
			sizeof(xhost), xport, sizeof(xport), 0) != 0)
			errxv(0, 1, "Name resolution failed");

		warnxv(0, "Listening on %s:%s", xhost, xport);

		event_set(&lq->ev, servsock, EV_READ, net_accept, lq);
		if (event_add(&lq->ev, NULL) == -1)
			errv(0, 1, "event_add()");

		TAILQ_INSERT_TAIL(&listenq_head, lq, next);
	}

	if (cleanup_add(cleanup, net_setup_cleanup, &listenq_head) == -1)
		errxv(0, 1, "cleanup_add()");

	return (servsock);
}

static void
net_setup_cleanup(void *_head)
{
	struct listenqh *head = (struct listenqh *)_head;
	struct listenq *lq;

	while ((lq = TAILQ_FIRST(head)) != NULL) {
		TAILQ_REMOVE(head, lq, next);
		close(lq->sock);
		free(lq->conn);
		event_del(&lq->ev);
		free(lq);
	}
}

static void
net_accept(int fd, short ev, void *data)
{
	struct sockaddr cliaddr;
	socklen_t addrlen = sizeof(cliaddr);
	int clisock, remsock;
	struct listenq *lq = (struct listenq *)data;
	struct conndesc *conn = lq->conn;

	if ((clisock = accept(fd, &cliaddr, &addrlen)) == -1) {
		warnv(0, "accept()");
		goto out;
	}

	if (!access_host((struct sockaddr_in *)&cliaddr)) {
		warnxv(2, "Client %s rejected", 
		    inet_ntoa(((struct sockaddr_in *)&cliaddr)->sin_addr));
		goto out;
	}

	switch (fork()) {
	case -1:
		warnv(0, "fork()");
		break;
	case 0:
		if ((remsock = net_negotiate(clisock, conn)) == NET_FAIL) {
			close(clisock);
			errxv(1, 1, "Negotiation failed");
		} else if (remsock == NET_NOPROXY) {
			/* SOCKS4A command 0xf0 succeeded */
			exit(0);
		}

		assert(remsock >= 0);

		cleanup_cleanup(cleanup);
		cleanup_free(cleanup);

		if ((cleanup = cleanup_new()) == NULL)
			errxv(0, 1, "Failed setting up cleanup functionality");

		/* Create new event loop */
		event_init();
		if (net_setup_proxy(clisock, remsock) == -1) {
			cleanup_cleanup(cleanup);
			errxv(0, 1, "Error setting up proxy");
		}
		signal_setup();
		event_dispatch();
		errxv(0, 1, "Event error");
	default:
		break;
	}

 out:
	close(clisock);
	if (event_add(&lq->ev, NULL) == -1)
		errv(0, 1, "event_add()");
}

static int
net_setup_proxy(int clisock, int remsock)
{
	struct proxydesc *clidesc, *remdesc;
	socklen_t len;
	int flags;
	extern int noresolve;

	if ((clidesc = newdesc(BUFFERSZ)) == NULL) 
		goto fail;
	if ((remdesc = newdesc(BUFFERSZ)) == NULL)
		goto fail1;

	clidesc->sock = clisock;
	remdesc->sock = remsock;

	if (fcntl(clisock, F_SETFL, O_NONBLOCK) == -1) {
		warnv(0, "fcntl()");
		goto fail2;
	}
	if (fcntl(remsock, F_SETFL, O_NONBLOCK) == -1) {
		warnv(0, "fcntl()");
		goto fail2;
	}

	len = sizeof(clidesc->in);
	if (getpeername(clisock, (struct sockaddr *)&clidesc->in, &len) == -1) {
		warnv(0, "Failed to retrieve address information from client");
		goto fail2;
	}
	len = sizeof(remdesc->in);
	if (getpeername(remsock, (struct sockaddr *)&remdesc->in, &len) == -1) {
		warnv(0, "Failed to retrieve address information from target");
		goto fail2;
	}

	flags = NI_NOFQDN;

	if (noresolve)
		flags |= NI_NUMERICHOST | NI_NUMERICSERV;

	_try_resolve_proxydesc(clidesc, flags);
	_try_resolve_proxydesc(remdesc, flags);

	snprintf(connstr, sizeof(connstr), "%s:%s <=> %s:%s",
	    clidesc->hostname, clidesc->port,
	    remdesc->hostname, remdesc->port);

	setproctitle("%s", connstr);

	warnxv(4, "Connecting %s", connstr);

	clidesc->dst = remdesc;
	remdesc->dst = clidesc;

	schedule(clidesc);
	schedule(remdesc);

	if (cleanup_add(cleanup, net_setup_proxy_cleanup, remdesc) == -1)
		goto fail2;
	if (cleanup_add(cleanup, net_setup_proxy_cleanup, clidesc) == -1)
		goto fail1;

	return (0);

 fail2:
	freedesc(remdesc);
 fail1:
	freedesc(clidesc);
 fail:
	return (-1);
}

static void
net_setup_proxy_cleanup(void *_desc)
{
	struct proxydesc *desc = _desc;

	close(desc->sock);
	event_del(desc->ev);
	freedesc(desc);
}

static int
net_negotiate(int clisock, struct conndesc *conn)
{
	u_char vn;
	int remsock = -1;

	/* Mirror mode */
	if (conn->mirror_ai != NULL)
		return (mirror_setup(conn));

	/* SOCKS */
	if (atomicio(read, clisock, &vn, 1) != 1) {
		warnv(0, "read()");
		return (-1);
	}

	/* SOCKS 4 and SOCKS 5 supported */
	switch (vn) {
	case 4:
		if (!ISSET(conn->support, NET_SUPPORT_SOCKS4)) {
			warnxv(1, "SOCKS4 support turned off");
			return (-1);
		}
		remsock = socks4_negotiate(clisock, conn);
		break;
	case 5:
		if (!ISSET(conn->support, NET_SUPPORT_SOCKS5)) {
			warnxv(1, "SOCKS5 support turned off");
			return (-1);
		}
		remsock = socks5_negotiate(clisock, conn);
		break;
	default:
		break;
	}

	return (remsock);
}

static void
proxy(int fd, short ev, void *data)
{
	struct proxydesc *d = (struct proxydesc *)data;
	int ret;

	/*
	 * XXX - what happens when socket errors ... eofpending() on
	 * read, then what happens on write?  should we perhaps
	 * shutdown() on failure/eofpending?
	 */

	if (ev & EV_READ) {
		/* Read into dst's buffer */
		ret = read(d->sock, d->dst->iov.iov_base + d->dst->pos,
		    d->dst->iov.iov_len - d->dst->pos);

		switch (ret) {
		case -1:
			if (!(errno == EINTR || errno == EAGAIN)) {
				if (!ISSET(d->state, NET_STATE_EOFPENDING)) {
					SET(d->state, NET_STATE_EOFPENDING);
					break;
				}
				cleanup_cleanup(cleanup);
				errv(0, 1, "(%s)", connstr);
			}
			break;
		case 0:
			if (!ISSET(d->state, NET_STATE_EOFPENDING)) {
				SET(d->state, NET_STATE_EOFPENDING);
				break;
			}
			cleanup_cleanup(cleanup);
			errxv(2, 0, "(%s) Terminated connection", connstr);
			/* NOTREACHED */
		default:
			d->dst->pos += ret;
			break;
		}
	}

	if (ev & EV_WRITE) {
		ret = write(d->sock, d->iov.iov_base, d->pos);

		if (ret == -1) {
			if (errno != EAGAIN) {
				cleanup_cleanup(cleanup);
				errv(0, 1, "(%s)", connstr);
			}
			goto out;
		}

		if (ret == d->pos) {
			d->pos = 0;
		} else {
			memmove(d->iov.iov_base, d->iov.iov_base + ret,
			    d->pos - ret);
			d->pos -= ret;
		}
	}

 out:
	schedule(d);
	if (event_del(d->dst->ev) == -1)
		warnv(0, "event_del()");
	schedule(d->dst);
}

static void
schedule(struct proxydesc *d)
{
	short ev = 0;

	/* Schedule write if there is pending data */
	if (PENDINGDATA(d))
		ev |= EV_WRITE;

	/*
	 * Schedule a read if there is still space in the read buffer,
	 * but not if there is a pending EOF and there is still data
	 * left to write.
	 */
	if (d->dst->pos < d->dst->iov.iov_len &&
	    !(ISSET(d->state, NET_STATE_EOFPENDING) && PENDINGDATA(d->dst)))
		ev |= EV_READ;

	if (ev == 0)
		return;

	event_set(d->ev, d->sock, ev, proxy, d);
	if (event_add(d->ev, NULL) == -1) {
		cleanup_cleanup(cleanup);
		errv(0, 1, "event_add()");
	}
}

static struct proxydesc *
newdesc(u_int sz)
{
	struct proxydesc *d;

	if ((d = calloc(1, sizeof(*d))) == NULL) {
		warnv(0, "calloc()");
		goto fail;
	}

	if ((d->iov.iov_base = malloc(sz)) == NULL) {
		warnv(0, "malloc()");
		goto fail1;
	}

	if ((d->ev = calloc(1, sizeof(*d->ev))) == NULL) {
		warnv(0, "malloc()");
		goto fail2;
	}

	d->iov.iov_len = sz;

	return (d);

 fail2:
	free(d->iov.iov_base);
 fail1:
	free(d);
 fail:
	return (NULL);
}

static struct proxydesc *
freedesc(struct proxydesc *d)
{
	free(d->ev);
	free(d->iov.iov_base);
	free(d);

	return (NULL);
}

struct addrinfo *
get_ai_from_addrpair(char *pair)
{
	struct addrinfo *ret = NULL;
	char *port = NULL, *host, *hosti;

	if ((hosti = host = strdup(pair)) == NULL)
		return (NULL);

	while (*hosti++ != '\0')
		if (*hosti == ':') {
			*hosti = '\0';
			port = ++hosti;
		}

	if (port == NULL) {
		warnxv(0, "Malformed address: %s", pair);
		goto fail;
	}

	ret = get_ai_from_ifip(host, port);
 fail:
	free(host);
	return (ret);
}

static struct addrinfo *
get_ai_from_ifip(char *ifip, char *port)
{
	struct ifreq ifr;
	struct addrinfo *ai, hints;
	int xerrno = 0, fd, error;
	char *str = NULL;

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		warnv(0, "socket()");
		return (NULL);
	}

	strlcpy(ifr.ifr_name, ifip, IFNAMSIZ);

	if (ioctl(fd, SIOCGIFADDR, &ifr) == -1) {
		xerrno = errno;
	} else {
		/*
		 * This is kind of nasty, but I want to be consistent
		 * and let getaddrinfo take care of everything.  E.g.,
		 * POSIX doesn't say anything about how things are
		 * allocated and such.
		 */
		str = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
	}

	close(fd);

	if (xerrno != 0) {
		if (ifip != NULL)
			str = ifip;
		else
			return (NULL);
	}

	MAKEHINTS(hints);
	if ((error = getaddrinfo(str, port, &hints, &ai)) != 0) {
		warnxv(0, "Unable to resolve host: %s: %s",
		    str, gai_strerror(error));
		if (0 /*xerrno != 0*/) {
			errno = xerrno;
			warnv(0,
			    "Unable to retrieve interface information: %s", ifip);
		}
		ai = NULL;
	}

	return (ai);
}
