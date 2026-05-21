/* See LICENSE file for copyright and license details. */

#ifndef XRANDRSETUP_UTILS_H
#define XRANDRSETUP_UTILS_H

#include <string.h>

/* Sets program_name to alloced name ptr. */
void set_name(const char *name);


/* Gets program_name ptr */
const char *get_name(void);

/*
 * Prints formated message to stderr and exits.
 * If last char is ':', prints strerror with set errno.
 */
void die(const char *fmt, ...);

/*
 * Prints formated message to stderr and returns.
 * If last char is ':', prints strerror with set errno.
 */
void warn(const char *fmt, ...);

/* Prints formated message with prompt for '--help' */
void argerr(const char *fmt, ...);

/* Calls calloc and exits on failure. */
void *ecalloc(size_t nmemb, size_t size);

/* Calls malloc and exits on failure. */
void *emalloc(size_t size);

/* Calls realloc and exits on failure. */
void *erealloc(void *ptr, size_t size);

#endif /* XRANDRSETUP_UTILS_H */
