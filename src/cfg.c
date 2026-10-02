/*	$OpenBSD: conf.c,v 1.24 2000/10/27 19:22:36 niklas Exp $	*/
/*	$EOM: conf.c,v 1.46 2000/10/26 16:17:19 ho Exp $	*/

/*
 * Copyright (c) 1998, 1999, 2000 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 2000 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Ericsson Radio Systems.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#ifndef WIN32
#include <sys/param.h>
#include <sys/mman.h>
#endif /* WIN32 */
#include <sys/stat.h>
#include <sys/queue.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_ERR
#include <err.h>
#endif /* HAVE_ERR */

#include "cfg.h"

struct conf_trans {
	TAILQ_ENTRY (conf_trans) link;
	int trans;
	enum conf_op { CONF_SET, CONF_REMOVE, CONF_REMOVE_SECTION } op;
	char *section;
	char *tag;
	char *value;
	int override;
	int is_default;
};

TAILQ_HEAD (conf_trans_head, conf_trans) conf_trans_queue;

struct conf_binding {
	LIST_ENTRY (conf_binding) link;
	char *section;
	char *tag;
	char *value;
	int is_default;
};

extern char *conf_path;
LIST_HEAD (conf_bindings, conf_binding) conf_bindings[256];

static char *conf_addr;

static __inline__ u_int8_t
conf_hash(char *s)
{
	u_int8_t hash = 0;

	while (*s) {
		hash = ((hash << 1) | (hash >> 7)) ^ tolower (*s);
		s++;
	}
	return (hash);
}

/*
 * Insert a tag-value combination from LINE (the equal sign is at POS)
 */
static int
conf_remove_now(char *section, char *tag)
{
	struct conf_binding *cb, *next;

	for (cb = LIST_FIRST (&conf_bindings[conf_hash (section)]); cb;
	     cb = next) {
		next = LIST_NEXT (cb, link);
		if (strcasecmp (cb->section, section) == 0
		    && strcasecmp (cb->tag, tag) == 0) {
			LIST_REMOVE (cb, link);
			free (cb->section);
			free (cb->tag);
			free (cb->value);
			free (cb);
			return (0);
		}
	}
	return (1);
}

static int
conf_remove_section_now (char *section)
{
	struct conf_binding *cb, *next;
	int unseen = 1;

	for (cb = LIST_FIRST (&conf_bindings[conf_hash (section)]); cb;
	     cb = next) {
		next = LIST_NEXT (cb, link);
		if (strcasecmp (cb->section, section) == 0) {
			unseen = 0;
			LIST_REMOVE (cb, link);
			free (cb->section);
			free (cb->tag);
			free (cb->value);
			free (cb);
		}
	}

	return (unseen);
}

/*
 * Insert a tag-value combination from LINE (the equal sign is at POS)
 * into SECTION of our configuration database.
 */
static int
conf_set_now (char *section, char *tag, char *value, int override,
	      int is_default)
{
	struct conf_binding *node = 0;

	if (override)
		conf_remove_now (section, tag);
	else if (conf_get_str (section, tag)) {
		if (!is_default)
			warnx("conf_set: duplicate tag [%s]:%s, ignoring...\n",
			      section, tag);
		return (1);
	}

	node = calloc (1, sizeof *node);
	if (!node) {
		warn("conf_set: calloc (1, %d) failed", sizeof *node);
		return (1);
	}
	node->section = strdup (section);
	node->tag = strdup (tag);
	node->value = strdup (value);
	node->is_default = is_default;

	LIST_INSERT_HEAD (&conf_bindings[conf_hash (section)], node, link);
	return (0);
}

/*
 * Parse the line LINE of SZ bytes.  Skip Comments, recognize section
 * headers and feed tag-value pairs into our configuration database.
 */
static void
conf_parse_line (int trans, char *line, size_t sz)
{
	char *cp = line;
	int i;
	static char *section = 0;
	static int ln = 0;

	ln++;

	/* Lines starting with '#' or ';' are comments.  */
	if (*line == '#' || *line == ';')
		return;

	/* '[section]' parsing...  */
	if (*line == '[') {
		for (i = 1; i < sz; i++)
			if (line[i] == ']')
				break;
		if (i == sz) {
			warnx("conf_parse_line: %d:"
			      "non-matched ']', ignoring until next section",
			      ln);
			section = 0;
			return;
		}
		if (section)
			free (section);
		section = malloc (i);
		strncpy (section, line + 1, i - 1);
		section[i - 1] = '\0';
		return;
	}

	/* Deal with assignments.  */
	for (i = 0; i < sz; i++)
		if (cp[i] == '=') {
			/* If no section, we are ignoring the lines.  */
			if (!section) {
				warnx("conf_parse_line: %d: ignoring line due to no section",
				      ln);
				return;
			}
			line[strcspn (line, " \t=")] = '\0';
			/* XXX Perhaps should we not ignore errors?  */
			conf_set (trans, section, line,
				  line + i + 1 + strspn (line + i + 1, " \t"), 0, 0);
			return;
		}

	/* Other non-empty lines are wierd.  */
	i = strspn (line, " \t");
	if (line[i])
		warnx("conf_parse_line: %d: syntax error", ln);

	return;
}

/* Parse the mapped configuration file.  */
static void
conf_parse (int trans, char *buf, size_t sz)
{
	char *cp = buf;
	char *bufend = buf + sz;
	char *line;

	line = cp;
	while (cp < bufend) {
		if (*cp == '\n') {
			/* Check for escaped newlines.  */
			if (cp > buf && *(cp - 1) == '\\')
				*(cp - 1) = *cp = ' ';
			else {
				*cp = '\0';
				conf_parse_line (trans, line, cp - line);
				line = cp + 1;
			}
		}
		cp++;
	}
	if (cp != line)
		warnx("conf_parse: last line non-terminated, ignored.");
}

void
conf_load_defaults (int tr)
{
	/* No defaults so far *
	conf_set (tr, "General", "Port", "80", 0, 1);
	conf_set (tr, "General", "IP-Address", "0.0.0.0", 0, 1);
	*/

	return;
}

void
conf_init (void)
{
	int i;

	for (i = 0; i < sizeof conf_bindings / sizeof conf_bindings[0]; i++)
		LIST_INIT (&conf_bindings[i]);
	TAILQ_INIT (&conf_trans_queue);
	conf_reinit ();
}

/* Open the config file and map it into our address space, then parse it.  */
void
conf_reinit (void)
{
	struct conf_binding *cb = 0;
	int fd, i, trans;
	off_t sz;
	char *new_conf_addr = 0;
	struct stat sb;

	if (stat (conf_path, &sb) == 0) {
		sz = sb.st_size;
		fd = open (conf_path, O_RDONLY);
		if (fd == -1) {
			warn("conf_reinit: open (\"%s\", O_RDONLY) failed",
			     conf_path);
			return;
		}

		new_conf_addr = malloc (sz);
		if (!new_conf_addr) {
			warn("conf_reinit: malloc (%d) failed", (int)sz);
			goto fail;
		}

		/* XXX I assume short reads won't happen here.  */
		if (read(fd, new_conf_addr, sz) != sz) {
			warn("conf_reinit: read (%d, %p, %d) failed",
			     fd, new_conf_addr, (int)sz);
			goto fail;
		}
		close (fd);

		trans = conf_begin ();

		/* XXX Should we not care about errors and rollback?  */
		conf_parse (trans, new_conf_addr, sz);
	}
	else
		trans = conf_begin ();

	/* Load default configuration values.  */
	conf_load_defaults (trans);

	/* Free potential existing configuration.  */
	if (conf_addr) {
		for (i = 0;
		     i < sizeof conf_bindings / sizeof conf_bindings[0]; i++)
			for (cb = LIST_FIRST (&conf_bindings[i]); cb;
			     cb = LIST_FIRST (&conf_bindings[i]))
				conf_remove_now (cb->section, cb->tag);
		free (conf_addr);
	}

	conf_end (trans, 1);
	conf_addr = new_conf_addr;
	return;

 fail:
	if (new_conf_addr)
		free (new_conf_addr);
	close (fd);
}

/*
 * Return the numeric value denoted by TAG in section SECTION or DEF
 * if that tag does not exist.
 */
int
conf_get_num (char *section, char *tag, int def)
{
	char *value = conf_get_str (section, tag);

	if (value)
		return (atoi (value));
	return (def);
}

/* Return the string value denoted by TAG in section SECTION.  */
char *
conf_get_str (char *section, char *tag)
{
	struct conf_binding *cb;

	for (cb = LIST_FIRST (&conf_bindings[conf_hash (section)]); cb;
	     cb = LIST_NEXT (cb, link))
		if (strcasecmp (section, cb->section) == 0
		    && strcasecmp (tag, cb->tag) == 0)
			return (cb->value);

	return (0);
}

int
conf_begin (void)
{
	static int seq = 0;

	return (++seq);
}

static struct conf_trans *
conf_trans_node (int transaction, enum conf_op op)
{
	struct conf_trans *node;

	node = calloc (1, sizeof *node);
	if (!node) {
		warn("conf_trans_node: calloc (1, %d) failed", sizeof *node);
		return (0);
	}
	node->trans = transaction;
	node->op = op;
	TAILQ_INSERT_TAIL (&conf_trans_queue, node, link);

	return (node);
}

/* Queue a set operation.  */
int
conf_set (int transaction, char *section, char *tag, char *value, int override,
	  int is_default)
{
	struct conf_trans *node;

	node = conf_trans_node (transaction, CONF_SET);
	if (!node)
		return (1);
	node->section = strdup (section);
	if (!node->section) {
		warn("conf_set: strdup (\"%s\") failed", section);
		goto fail;
	}
	node->tag = strdup (tag);
	if (!node->tag) {
		warn("conf_set: strdup (\"%s\") failed", tag);
		goto fail;
	}
	node->value = strdup (value);
	if (!node->value) {
		warn("conf_set: strdup (\"%s\") failed", value);
		goto fail;
	}
	node->override = override;
	node->is_default = is_default;
	return (0);

 fail:
	if (node->tag)
		free (node->tag);
	if (node->section)
		free (node->section);
	if (node)
		free (node);
	return (1);
}

/* Execute all queued operations for this transaction.  Cleanup.  */
int
conf_end (int transaction, int commit)
{
	struct conf_trans *node, *next;

	for (node = TAILQ_FIRST (&conf_trans_queue); node; node = next) {
		next = TAILQ_NEXT (node, link);
		if (node->trans == transaction) {
			if (commit)
				switch (node->op) {
				case CONF_SET:
					conf_set_now (node->section, node->tag, node->value,
						      node->override, node->is_default);
					break;
				case CONF_REMOVE:
					conf_remove_now (node->section, node->tag);
					break;
				case CONF_REMOVE_SECTION:
					conf_remove_section_now (node->section);
					break;
				default:
					warnx("conf_end: unknown operation: %d", node->op);
				}
			TAILQ_REMOVE (&conf_trans_queue, node, link);
			if (node->section)
				free (node->section);
			if (node->tag)
				free (node->tag);
			if (node->value)
				free (node->value);
			free (node);
		}
	}
	return (0);
}

