/*
 * access.c
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: access.c,v 1.5 2003/06/08 05:56:47 marius Exp $
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "expanda.h"
#include "print.h"

struct ip_chain {
        struct in_addr        addr;
        struct in_addr        mask;
        TAILQ_ENTRY(ip_chain) next;
};

static TAILQ_HEAD(ip_chainh, ip_chain) allow_chain, deny_chain;
static struct ip_chainh tallow_chain, tdeny_chain;

static int  makechain(void *, char **);
static void destroychain(void *);
static void buildchain(struct ip_chainh *, char *, char *);
static int  checkchain(struct ip_chainh *, struct ip_chainh *,
    struct sockaddr_in *);

void
access_setup(char *allow, char *deny)
{
	TAILQ_INIT(&allow_chain);
	TAILQ_INIT(&deny_chain);

	buildchain(&allow_chain, allow, "allow");
	buildchain(&deny_chain, deny, "deny");
}

void
access_target_setup(char *allow, char *deny)
{
	TAILQ_INIT(&tallow_chain);
	TAILQ_INIT(&tdeny_chain);

	buildchain(&tallow_chain, allow, "target allow");
	buildchain(&tdeny_chain, deny, "target deny");
}

static void
buildchain(struct ip_chainh *head, char *list, char *what)
{
	char **arr;

	if (list == NULL || *list == '\0')
		return;

	if ((arr = expanda(list)) == NULL)
		errxv(0, 1, "Error expanding %s list", what);
	if (makechain(head, arr) == -1)
		errxv(0, 1, "Error making %s list", what);
	freea(arr);
}

int
access_host(struct sockaddr_in *in)
{
	return (checkchain(&deny_chain, &allow_chain, in));
}

int
access_target(struct sockaddr_in *in)
{
	return (checkchain(&tdeny_chain, &tallow_chain, in));
}

static int
checkchain(struct ip_chainh *deny, struct ip_chainh *allow,
    struct sockaddr_in *in)
{
	int a = 0;
 	struct ip_chain *node;
	struct ip_chainh *ip_list[] = {deny, allow, NULL};
	struct ip_chainh **il = ip_list;

	do {
		TAILQ_FOREACH(node, *il, next) {
			in_addr_t addr_in, addr_node;

			addr_in = in->sin_addr.s_addr & htonl(node->mask.s_addr);
			addr_node = node->addr.s_addr & htonl(node->mask.s_addr);

			if (memcmp(&addr_in, &addr_node, sizeof(addr_in)) == 0)
				return (a);
		}
		a = 1;
	} while (*(++il));

	if (TAILQ_EMPTY(allow))
		return (1);

	return (0);
}

static int
makechain(void *_head, char **hostlist)
{
        struct ip_chain *node;
        unsigned i;
	char **host = hostlist;
	struct ip_chainh *head = (struct ip_chainh *)_head;
	struct addrinfo *ai = NULL, hints, *res;
	struct in_addr mask;
	int error;

        while (*host != NULL) {
                for (i = 0; (*host)[i] != '\0' && (*host)[i] != '/'; i++);

                mask.s_addr = 0xFFFFFFFF;
                if (i != strlen(*host)) {
			char *end;
			long bits = strtol(*host + i + 1, &end, 10);

			if (end == *host + i + 1 || *end != '\0' ||
			    bits < 0 || bits > 32) {
				warnxv(1, "Invalid prefix length in %s", *host);
				goto fail;
			}
			mask.s_addr = (bits == 0) ? 0 :
			    0xFFFFFFFF << (32 - bits);
		}

                (*host)[i] = '\0';

		/* Only IPv4 ... for now */
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_INET;
		hints.ai_socktype = SOCK_STREAM;
		if ((error = getaddrinfo(*host, NULL, &hints, &ai)) != 0) {
			warnxv(1, "Error resolving name %s: %s", *host,
			    gai_strerror(error));
			goto fail;
		}

		for (res = ai; res != NULL; res = res->ai_next) {
			if ((node = calloc(1, sizeof(*node))) == NULL) {
				warnv(1, "calloc()");
				goto fail;
			}

			memcpy(&node->addr,
			    &((struct sockaddr_in *)res->ai_addr)->sin_addr,
			    sizeof(node->addr));
			memcpy(&node->mask, &mask, sizeof(node->mask));

			TAILQ_INSERT_TAIL(head, node, next);
		}
		freeaddrinfo(ai);
		host++;
        }

	return (0);

 fail:
	if (ai != NULL)
		freeaddrinfo(ai);
	destroychain(head);
	return (-1);
}

static void
destroychain(void *_head)
{
	struct ip_chainh *head = (struct ip_chainh *)_head;
	struct ip_chain *node;

	while ((node = TAILQ_FIRST(head)) != NULL) {
		TAILQ_REMOVE(head, node, next);
		free(node);
	}
}
