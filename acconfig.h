#undef socklen_t
#undef u_int16_t
#undef u_int32_t
#undef u_int64_t
#undef u_int8_t

#undef in_addr_t

#undef SYSCONFDIR

#undef SPT_TYPE
#undef HAVE___PROGNAME

/* Define if TAILQ_FOREACH is defined in <sys/queue.h> */
#undef HAVE_TAILQFOREACH
#ifndef HAVE_TAILQFOREACH
#define	TAILQ_FIRST(head)		((head)->tqh_first)
#define	TAILQ_END(head)			NULL
#define	TAILQ_NEXT(elm, field)		((elm)->field.tqe_next)
#define TAILQ_LAST(head, headname)					\
	(*(((struct headname *)((head)->tqh_last))->tqh_last))
#define TAILQ_FOREACH(var, head, field)					\
	for((var) = TAILQ_FIRST(head);					\
	    (var) != TAILQ_END(head);					\
	    (var) = TAILQ_NEXT(var, field))
#define	TAILQ_INSERT_BEFORE(listelm, elm, field) do {			\
	(elm)->field.tqe_prev = (listelm)->field.tqe_prev;		\
	(elm)->field.tqe_next = (listelm);				\
	*(listelm)->field.tqe_prev = (elm);				\
	(listelm)->field.tqe_prev = &(elm)->field.tqe_next;		\
} while (0)
#endif /* !HAVE_TAILQFOREACH */

/* Define if LIST_FIRST is defined in <sys/queue.h> */
#undef HAVE_LISTFIRST
#ifndef HAVE_LISTFIRST
#define	LIST_FIRST(head)		((head)->lh_first)
#define	LIST_END(head)			NULL
#define	LIST_EMPTY(head)		(LIST_FIRST(head) == LIST_END(head))
#define	LIST_NEXT(elm, field)		((elm)->field.le_next)
#endif /* !HAVE_LISTFIRST */

@BOTTOM@

/* XXX move err* warn* stuff here too */

/* Prototypes for missing functions */
#ifndef HAVE_STRLCAT
size_t	 strlcat(char *, const char *, size_t);
#endif

#ifndef HAVE_STRLCPY
size_t	 strlcpy(char *, const char *, size_t);
#endif

#ifndef HAVE_SETPROCTITLE
void     setproctitle(const char *fmt, ...);
#endif

#ifndef HAVE_STRSEP
char    *strsep(char **, const char *);
#endif

#ifndef HAVE_ERR
void     err(int, const char *, ...);
void     warn(const char *, ...);
void     errx(int , const char *, ...);
void     warnx(const char *, ...);
#endif

#ifndef HAVE_DAEMON
int      daemon(int, int);
#endif /* HAVE_DAEMON */
