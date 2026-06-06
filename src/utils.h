/* See LICENSE file for copyright and license details. */

#ifndef XRANDRSETUP_UTILS_H
#define XRANDRSETUP_UTILS_H

#include <string.h>

extern int verbose;
extern int dry_run;

void set_name(const char *name);
const char *get_name(void);

/*
 * Prints formated message to stderr and exits.
 * If last char is ':', prints strerror with set errno.
 */
_Noreturn void die(const char *fmt, ...);

/*
 * Prints formated message to stderr and returns.
 * If last char is ':', prints strerror with set errno.
 */
void warn(const char *fmt, ...);

/* Prints verbose info. */
void vinfo(const char *fmt, ...);

/* Prints formated message with prompt for '--help' */
_Noreturn void argerr(const char *fmt, ...);

void *ecalloc(const size_t nmemb, const size_t size);
void *emalloc(const size_t size);
void *erealloc(void *ptr, size_t size);

#endif /* XRANDRSETUP_UTILS_H */
