/*
 * jprint.h
 */

/* If in ZFS <sys/zfs_context.h> provides what is needed */

/* If standalone */
/* #include <stdbool.h> */
/* #define boolean_t bool */
/* #define B_TRUE true */
/* #define B_FALSE false */
/* #include <stddef.h> */
/* #include <stdint.h> */
/* #include <stdio.h> */
/* #include <stdarg.h> */
/* #include <string.h> */
/* #include <inttypes.h> */

/* maximum stack nesting */
#define JP_MAX_STACK 32

enum jp_type {
	JP_OBJECT = 1,
	JP_ARRAY
};

struct jp_stack {
	enum jp_type type;
	int nelem;
};

typedef struct jprint {
	char *buffer;    /* pointer to application's buffer */
	size_t buflen;   /* length of buffer */
	char *bufp;      /* current write position in buffer */
	char tmpbuf[32]; /* local buffer for conversions */
	int error;       /* error code */
	int ncall;       /* API call number on which error occurred */
	struct jp_stack  /* stack of array/object nodes */
	    stack[JP_MAX_STACK];
	int stackp;
} jprint_t;

/* error return codes */
#define JPRINT_OK          0 /* no error */
#define JPRINT_BUF_FULL    1 /* output buffer full */
#define JPRINT_NEST_ERROR  2 /* nesting error */
#define JPRINT_STACK_FULL  3 /* array/object nesting  */
#define JPRINT_STACK_EMPTY 4 /* stack underflow error */
#define JPRINT_OPEN        5 /* not all objects closed */
#define JPRINT_FMT         6 /* format error */
#define JPRINT_NO_DOUBLE   7 /* %g support not included */

const char *jp_errorstring(int err);
int jp_error(jprint_t *jp);
void jp_open(jprint_t *jp, char *buffer, size_t buflen);
int jp_close(jprint_t *jp);
int jp_errorpos(jprint_t *jp);
int jp_printf(jprint_t *jp, const char *fmt, ...);

