/*
 * access.h
 *
 * Copyright (c) 2001, 2002 Marius Aamodt Eriksen <marius@monkey.org>
 *
 * $Id: access.h,v 1.2 2002/09/14 18:49:30 marius Exp $
 */


#ifndef ACCESS_H
#define ACCESS_H

void access_setup(char *, char *);
int  access_host(struct sockaddr_in *);

#endif /* ACCESS_H */
