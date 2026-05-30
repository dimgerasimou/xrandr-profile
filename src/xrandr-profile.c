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
#include <getopt.h>

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

enum {
	OPT_SAVE = 1000,
	OPT_LOAD,
	OPT_DELETE,
	OPT_LIST,
	OPT_LIST_ALL,
	OPT_LIST_CURRENT,
	OPT_NAMES,
	OPT_VERSION,
	OPT_HELP,
};

typedef struct {
	Action a;
	const char *name;
	unsigned int names_only;
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

		xr_apply_profile(sel);

		if (i == 0)
			goto out;

		memmove(&pl->p[1], &pl->p[0], i * sizeof(Profile *));
		pl->p[0] = sel;

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

	snprintf(cur->name, sizeof(cur->name), "%s", opt->name);

	profile_list_delete(pl, opt->name);
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

	profile_list_delete(pl, opt->name);

	if (profile_list_write(pl))
		ret = -1;

	profile_list_free(pl);
	return ret;
}

static void
action_list(const unsigned int names_only)
{
	ProfileList *pl;
	Profile     *cur;

	pl  = profile_list_read();
	cur = xr_active_profile();

	for (size_t i = 0; i < pl->len; i++)
		if (profile_match(pl->p[i], cur))
			profile_print(pl->p[i], names_only);

	profile_list_free(pl);
	profile_free(cur);
}

static void
action_list_all(const unsigned int names_only)
{
	ProfileList *pl;

	pl  = profile_list_read();

	for (size_t i = 0; i < pl->len; i++)
		profile_print(pl->p[i], names_only);

	profile_list_free(pl);
}

static void
action_list_current(void)
{
	Profile *cur;

	cur = xr_active_profile();

	profile_print(cur, 0);
	profile_free(cur);
}

static void
usage(void)
{
	printf("usage: %s [options]\n%s", get_name(),
	      "\tOptions:\n"
	      "\t\t[--help][--version]\n"
	      "\t\t[--load profile][--save profile][--delete profile]\n"
	      "\t\t[--list][--list-all][--list-current][--names]\n"
	      "\n"
	      "--help              Print this message and exit\n"
	      "--version           Print version and exit\n"
	      "--save              Save current profile\n"
	      "--load              Load selected profile\n"
	      "--delete            Delete selected profile\n"
	      "--list              List profiles matching configuration\n"
	      "--list-all          List all profiles\n"
	      "--list-current      List current profile properties\n"
	      "--names             Print only the profile names\n");
}

static int
options_parse(Options *o, const int argc, char *argv[])
{
	int nactions = 0;

	o->name = NULL;
	o->a = AUTO;
	o->names_only = 0;

	struct option longopts[] = {
		{ "save",         required_argument, 0, OPT_SAVE         },
		{ "load",         required_argument, 0, OPT_LOAD         },
		{ "delete",       required_argument, 0, OPT_DELETE       },
		{ "names",        no_argument,       0, OPT_NAMES        },
		{ "list",         no_argument,       0, OPT_LIST         },
		{ "list-all",     no_argument,       0, OPT_LIST_ALL     },
		{ "list-current", no_argument,       0, OPT_LIST_CURRENT },
		{ "help",         no_argument,       0, OPT_HELP         },
		{ "version",      no_argument,       0, OPT_VERSION      },
		{ 0,              0,                 0, 0                },
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
		switch (opt) {
		case OPT_SAVE:
			nactions++;
			o->a = SAVE;
			o->name=optarg;
			break;

		case OPT_LOAD:
			nactions++;
			o->a = LOAD;
			o->name=optarg;
			break;

		case OPT_DELETE:
			nactions++;
			o->a = DELETE;
			o->name=optarg;
			break;

		case OPT_LIST:
			nactions++;
			o->a = LIST;
			break;

		case OPT_LIST_ALL:
			nactions++;
			o->a = LIST_ALL;
			break;

		case OPT_LIST_CURRENT:
			nactions++;
			o->a = LIST_CURRENT;
			break;

		case OPT_NAMES:
			o->names_only = 1;
			break;

		case OPT_HELP:
			usage();
			exit(0);

		case OPT_VERSION:
			printf("%s-"VERSION"\n", get_name());
			exit(0);

		default:
			fputc('\n', stderr);
			usage();
			exit(1);
		}
	}

	if (nactions > 1)
		argerr("only one action may be given.");

	return 0;
}

int
main (int argc, char *argv[])
{
	Options opt;
	int     ret = 0;

	set_name(argv[0]);
	options_parse(&opt, argc, argv);
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
		action_list(opt.names_only);
		break;
	
	case LIST_ALL:
		action_list_all(opt.names_only);
		break;
	
	case LIST_CURRENT:
		action_list_current();
		break;
	}

	xr_free();
	return ret;
}
