/* xrandr-profile
 * Copyright (c) 2026 Dimitris Gerasimou
 * Licensed under the GNU General Public License v3.
 *
 * Simple XRandr profile manager, based on hardware
 * configuration.
 *
 * To understand everything, start reading main().
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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
	WATCH,
} Action;

enum {
	OPT_SAVE = 1000,
	OPT_LOAD,
	OPT_DELETE,
	OPT_LIST,
	OPT_LIST_ALL,
	OPT_LIST_CURRENT,
	OPT_WATCH,
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
hook_name_cmp(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Run every executable regular file in `dir`, in sorted order, each with
 * XRANDR_PROFILE and XRANDR_HOOK set in its environment. Missing dir is a
 * no-op (hooks are opt-in). */
static void
run_hook_dir(const char *dir, const char *profile, const char *phase)
{
	enum { MAXHOOKS = 256 };
	char  *names[MAXHOOKS];
	size_t n = 0;
	DIR   *d;
	struct dirent *de;

	if (!(d = opendir(dir)))
		return;

	while ((de = readdir(d)) && n < MAXHOOKS) {
		if (de->d_name[0] == '.')
			continue;
		if ((names[n] = strdup(de->d_name)))
			n++;
	}
	closedir(d);

	qsort(names, n, sizeof(names[0]), hook_name_cmp);

	for (size_t i = 0; i < n; i++) {
		char        path[PATH_MAX];
		struct stat st;

		snprintf(path, sizeof(path), "%s/%s", dir, names[i]);

		if (stat(path, &st) == 0 && S_ISREG(st.st_mode)
		        && (st.st_mode & S_IXUSR)) {
			pid_t pid = fork();

			if (pid == 0) {
				setenv("XRANDR_PROFILE", profile ? profile : "", 1);
				setenv("XRANDR_HOOK", phase, 1);
				execl(path, path, (char *)NULL);
				_exit(127);
			} else if (pid > 0) {
				int status;
				while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
					;
				if (WIFEXITED(status) && WEXITSTATUS(status))
					warn("hook \"%s\" exited with %d", names[i], WEXITSTATUS(status));
			} else {
				warn("fork:");
			}
		}

		free(names[i]);
	}
}

/* Global hooks/<phase>/ then per-profile hooks/<profile>/<phase>/. */
static void
run_hooks(const char *profile, const char *phase)
{
	char base[PATH_MAX];
	char dir[PATH_MAX + 16];
	int  n;

	profile_config_dir(base, sizeof(base));

	n = snprintf(dir, sizeof(dir), "%s/hooks/%s", base, phase);
	if (n > 0 && (size_t)n < sizeof(dir))
		run_hook_dir(dir, profile, phase);

	if (profile && profile[0]) {
		n = snprintf(dir, sizeof(dir), "%s/hooks/%s/%s", base, profile, phase);
		if (n > 0 && (size_t)n < sizeof(dir))
			run_hook_dir(dir, profile, phase);
	}
}

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

		run_hooks(sel->name, "pre");
		xr_apply_profile(sel);
		run_hooks(sel->name, "post");

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
action_watch(void)
{
	enum { DEBOUNCE_MS = 300 };

	xr_watch_init();
	action_apply(NULL);

	while (xr_wait_for_change(DEBOUNCE_MS) == XR_CHANGED)
		action_apply(NULL);

	return 0;
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
	      "\t\t[--watch]\n"
	      "\n"
	      "--help              Print this message and exit\n"
	      "--version           Print version and exit\n"
	      "--save              Save current profile\n"
	      "--load              Load selected profile\n"
	      "--delete            Delete selected profile\n"
	      "--list              List profiles matching configuration\n"
	      "--list-all          List all profiles\n"
	      "--list-current      List current profile properties\n"
	      "--watch             Watch for hotplug changes and auto-apply\n"
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
		{ "watch",        no_argument,       0, OPT_WATCH        },
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

		case OPT_WATCH:
			nactions++;
			o->a = WATCH;
			break;

		case OPT_NAMES:
			o->names_only = 1;
			break;

		case OPT_HELP:
			usage();
			exit(0);

		case OPT_VERSION:
			puts("xrandr-profile-"VERSION"\n");
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

	case WATCH:
		if (action_watch())
			ret = 1;
		break;
	}

	xr_free();
	return ret;
}
