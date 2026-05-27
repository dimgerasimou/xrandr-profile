/* xrandr-profile
 * Copyright (c) 2026 Dimitris Gerasimou
 * Licensed under the GNU General Public License v3.
 *
 * Simple XRandr profile manager, based on hardware
 * configuration.
 *
 * To understand everything, start reading main().
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "profile.h"
#include "xrandr.h"
#include "utils.h"

#ifndef VERSION
#define VERSION "unknown"
#endif

typedef enum {
	AUTO = 0,
	SAVE,
	LOAD,
	DELETE,
	LIST,
	LIST_ALL,
	LIST_CURRENT,
} Action;

typedef struct {
	Action a;
	const char *name;
} Options;

static int
action_apply(const char *name)
{
	ProfileList *pl;
	Profile     *cur;
	int          ret = 0;

	pl  = profile_list_read();
	cur = xr_active_profile();

	for (size_t i = 0; i < pl->len; i++) {
		if (!profile_match(pl->p[i], cur))
			continue;
		if (name && strcmp(pl->p[i]->name, name) != 0)
			continue;

		Profile *sel = pl->p[i];

		memmove(&pl->p[1], &pl->p[0], i * sizeof(Profile *));
		pl->p[0] = sel;

		xr_apply_profile(sel);

		if (profile_list_write(pl))
			ret = -1;
		goto out;
	}

	if (name) {
		warn("no matching profile named \"%s\"", name);
		ret = -1;
	}

out:
	profile_list_free(pl);
	profile_free(cur);
	return ret;
}

static int
action_save(Options *opt)
{
	ProfileList *pl;
	Profile     *cur;
	int          ret = 0;

	pl  = profile_list_read();
	cur = xr_active_profile();

	strncpy(cur->name, opt->name, sizeof(cur->name));
	cur->name[sizeof(cur->name)-1] = '\0';

	profile_list_delete(pl, (char*) opt->name);
	profile_list_prepend(pl,cur);

	if (profile_list_write(pl))
		ret = -1;

	profile_list_free(pl);
	return ret;
}

static int
action_delete(Options *opt)
{
	ProfileList *pl;
	int          ret = 0;

	pl  = profile_list_read();

	profile_list_delete(pl, (char*) opt->name);

	if (profile_list_write(pl))
		ret = -1;

	profile_list_free(pl);
	return ret;
}

static void
action_list(void)
{
	ProfileList *pl;
	Profile     *cur;

	pl  = profile_list_read();
	cur = xr_active_profile();

	for (size_t i = 0; i < pl->len; i++)
		if (profile_match(pl->p[i], cur))
			profile_print(pl->p[i]);

	profile_list_free(pl);
	profile_free(cur);
}

static void
action_list_all(void)
{
	ProfileList *pl;

	pl  = profile_list_read();

	for (size_t i = 0; i < pl->len; i++)
		profile_print(pl->p[i]);

	profile_list_free(pl);
}

static void
action_list_current(void)
{
	Profile *cur;

	cur = xr_active_profile();

	profile_print(cur);
	profile_free(cur);
}

static void
usage(void)
{
	printf("usage: %s [options]\n%s", get_name(),
	       "  where options are:\n"
	       "  --help\n"
	       "  --version\n"
	       "  --save <profile>\n"
	       "  --load <profile>\n"
	       "  --delete <profile>\n"
	       "  --list\n"
	       "  --list-all\n"
	       "  --list-current\n"
	);
}

static const char *
match_opt(const char *arg, const char *name)
{
	const char *p = arg;
	size_t n;

	if (*p++ != '-')
		return NULL;
	if (*p == '-')
		p++;

	n = strlen(name);
	if (strncmp(p, name, n) != 0)
		return NULL;
	if (p[n] == '\0')
		return "";
	if (p[n] == '=')
		return p + n + 1;
	return NULL;
}

static void
options_parse(Options *opt, const int argc, const char *argv[])
{
	const char *val;

	opt->name = NULL;
	opt->a = AUTO;

	for (int i = 1; i < argc; i++) {
		if (match_opt(argv[i], "help") || match_opt(argv[i], "h")) {
			usage();
			exit(0);
		}
		if (match_opt(argv[i], "version") || match_opt(argv[i], "v")) {
			printf("%s-"VERSION"\n", get_name());
			exit(0);
		}
		if ((val = match_opt(argv[i], "save")) != NULL) {
			if (val[0] == '\0') {
				if (++i >= argc)
					argerr("%s requires an argument", argv[i-1]);
				opt->name = argv[i];
			} else {
				opt->name = val;
			}
			opt->a = SAVE;
			break;
		}
		if ((val = match_opt(argv[i], "load")) != NULL) {
			if (val[0] == '\0') {
				if (++i >= argc)
					argerr("%s requires an argument", argv[i-1]);
				opt->name = argv[i];
			} else {
				opt->name = val;
			}
			opt->a = LOAD;
			break;
		}
		if ((val = match_opt(argv[i], "delete")) != NULL) {
			if (val[0] == '\0') {
				if (++i >= argc)
					argerr("%s requires an argument", argv[i-1]);
				opt->name = argv[i];
			} else {
				opt->name = val;
			}
			opt->a = DELETE;
			break;
		}
		if (match_opt(argv[i], "list")) {
			opt->a = LIST;
			break;
		}
		if (match_opt(argv[i], "list-all")) {
			opt->a = LIST_ALL;
			break;
		}
		if (match_opt(argv[i], "list-current")) {
			opt->a = LIST_CURRENT;
			break;
		}
		argerr("Invalid argument '%s'", argv[i]);
	}
}

int
main (int argc, char *argv[])
{
	Options opt;
	int     ret = 0;

	set_name(argv[0]);
	options_parse(&opt, argc, (const char**) argv);
	xr_init();

	switch (opt.a) {
	case AUTO:
		if (action_apply(NULL))
			ret = 1;
		break;

	case SAVE:
		if (action_save(&opt))
			ret = 1;
		break;

	case LOAD:
		if (action_apply(opt.name))
			ret = 1;
		break;
	
	case DELETE:
		if (action_delete(&opt))
			ret = 1;
		break;
	
	case LIST:
		action_list();
		break;
	
	case LIST_ALL:
		action_list_all();
		break;
	
	case LIST_CURRENT:
		action_list_current();
		break;
	}

	xr_free();
	return ret;
}
