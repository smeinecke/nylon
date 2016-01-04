include $(top_srcdir)/Makefile.am.inc

bin_PROGRAMS = nylon

nylon_SOURCES = nylon.c print.c cfg.c expanda.c net.c access.c atomicio.c \
                socks4.c socks5.c mirror.c cleanup.c misc.c

AM_CFLAGS = @EVENTINC@ -Wall -g
LDADD = @EVENTLIB@ @LIBOBJS@

EXTRA_DIST = strlcpy.c strlcat.c setproctitle.c strsep.c err.c daemon.c
