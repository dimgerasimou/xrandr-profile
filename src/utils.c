/* See LICENSE file for copyright and license details. */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"

static const char *program_name;

void
set_name(const char *name)
{
	program_name = name;
}

const char *
get_name(void)
{
	return program_name;
}

void
die(const char *fmt, ...)
{
	va_list ap;
	int saved_errno = errno;

	fprintf(stderr, "%s: ", program_name);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':')
		fprintf(stderr, " %s", strerror(saved_errno));
	fputc('\n', stderr);

	exit(1);
}

void
warn(const char *fmt, ...)
{
	va_list ap;
	int saved_errno = errno;

	fprintf(stderr, "%s: ", program_name);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':')
		fprintf(stderr, " %s", strerror(saved_errno));
	fputc('\n', stderr);
}

void
argerr(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", program_name);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fprintf (stderr, "\nTry '%s --help' for more information.\n", program_name);
	exit(1);
}

void *
ecalloc(const size_t nmemb, const size_t size)
{
	void *p;

	if ((p = calloc(nmemb, size)) == NULL)
		die("calloc:");
	return p;
}

void *
emalloc(const size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		die("malloc:");
	return p;
}

void *
erealloc(void *ptr, const size_t size)
{
	void *p;

	if ((p = realloc(ptr, size)) == NULL)
		die("realloc:");
	return p;
}
