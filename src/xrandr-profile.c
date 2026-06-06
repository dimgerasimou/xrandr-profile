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
	OPT_FALLBACK,
	OPT_FORCE,
	OPT_VERBOSE,
	OPT_DRY_RUN,
	OPT_VERSION,
	OPT_HELP,
};

typedef struct {
	Action a;
	const char *name;
	unsigned int names_only;
	unsigned int force;
	unsigned int verbose;
	unsigned int dry_run;
	XrFallback fallback;
} Options;

static int  hook_name_cmp(const void *a, const void *b);
static void run_hook_dir(const char *dir, const char *profile, const char *phase);
static void run_hooks(const char *profile, const char *phase);
static int action_save(const Options *opt);
static int action_apply(const char *name, XrFallback fallback, const unsigned int force);
static int action_delete(const Options *opt);
static void action_list(const unsigned int names_only);
static void action_list_all(const unsigned int names_only);
static void action_list_current(void);
static int action_watch(XrFallback fallback, const unsigned int force);
static void usage(void);
static int  parse_fallback(const char *s, XrFallback *out);
static int  options_parse(Options *o, const int argc, char *argv[]);

static int
hook_name_cmp(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Run every executable regular file in `dir`, in sorted order, each with
 * XRANDR_PROFILE and XRANDR_HOOK set in its environment. Missing dir is a
 * no-op (hooks are opt-in).
 */
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

		/* Daemon shutting down: skip the rest (still free the names). */
		if (xr_stop_requested()) {
			free(names[i]);
			continue;
		}

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
				int status = 0;

				for (;;) {
					pid_t w = waitpid(pid, &status, 0);

					if (w == pid)
						break;                  /* reaped */
					if (w < 0 && errno == EINTR) {
						/* Stop signal: don't block on a slow
						 * hook during shutdown; leave the child
						 * to finish and be reaped by init. */
						if (xr_stop_requested())
							break;
						continue;               /* benign interrupt */
					}
					break;                          /* genuine error */
				}

				if (!xr_stop_requested()
				        && WIFEXITED(status) && WEXITSTATUS(status))
					warn("hook \"%s\" exited with %d",
					     names[i], WEXITSTATUS(status));
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
action_save(const Options *opt)
{
	ProfileList *pl;
	Profile     *cur;
	int          ret = 0;

	pl  = profile_list_read();
	cur = xr_active_profile();

	snprintf(cur->name, sizeof(cur->name), "%s", opt->name);

	profile_list_delete(pl, opt->name);
	profile_list_prepend(pl,cur);

	vinfo("saving profile \"%s\"%s", opt->name, dry_run ? " (dry-run)" : "");

	if (!dry_run && profile_list_write(pl))
		ret = -1;

	profile_list_free(pl);
	return ret;
}

static int
action_apply(const char *name, XrFallback fallback, const unsigned int force)
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

		if (!force && profile_layout_equal(sel, cur)) {
			vinfo("profile \"%s\" already applied; nothing to do", sel->name);
			goto out;
		}

		vinfo("applying profile \"%s\"%s", sel->name, dry_run ? " (dry-run)" : "");

		if (!dry_run)
			run_hooks(sel->name, "pre");

		xr_apply_profile(sel);

		if (!dry_run)
			run_hooks(sel->name, "post");

		if (i == 0)
			goto out;

		if (!dry_run) {
			memmove(&pl->p[1], &pl->p[0], i * sizeof(Profile *));
			pl->p[0] = sel;

			if (profile_list_write(pl))
				ret = -1;
		}
		goto out;
	}

	if (name) {
		warn("no matching profile named \"%s\"", name);
		ret = -1;
	} else if (fallback != XR_FALLBACK_OFF) {
		Profile *fb = xr_fallback_profile(fallback);

		if (fb) {
			warn("no saved profile matched; applying fallback layout");

			if (!dry_run)
				run_hooks(fb->name, "pre");

			xr_apply_profile(fb);

			if (!dry_run)
				run_hooks(fb->name, "post");

			profile_free(fb);
		}
	}

out:
	profile_list_free(pl);
	profile_free(cur);
	return ret;
}

static int
action_delete(const Options *opt)
{
	ProfileList *pl;
	int          ret = 0;

	pl  = profile_list_read();

	vinfo("deleting profile \"%s\"%s", opt->name, dry_run ? " (dry-run)" : "");

	profile_list_delete(pl, opt->name);

	if (!dry_run && profile_list_write(pl))
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

static int
action_watch(XrFallback fallback, const unsigned int force)
{
	enum { DEBOUNCE_MS = 300 };

	xr_watch_init();
	action_apply(NULL, fallback, force);

	while (xr_wait_for_change(DEBOUNCE_MS) == XR_CHANGED)
		action_apply(NULL, fallback, force);

	return 0;
}

static void
usage(void)
{
	printf("usage: %s [options]\n%s", get_name(),
	      "\tOptions:\n"
	      "\t\t[--help][--version]\n"
	      "\t\t[--load profile][--save profile][--delete profile]\n"
	      "\t\t[--list][--list-all][--list-current][--names]\n"
	      "\t\t[--watch][--fallback=horizontal|vertical|clone|off][--force][--verbose][--dry-run]\n"
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
	      "--names             Print only the profile names\n"
	      "--fallback=MODE     When no saved profile matches, arrange the\n"
	      "                    connected outputs automatically. MODE is one\n"
	      "                    of horizontal, vertical, clone, or off\n"
	      "                    (default: off)\n"
	      "--force             Re-apply even if the profile is already active\n"
	      "--verbose           Log what is being done to stderr\n"
	      "--dry-run           Show what would change without doing anything\n");
}

static int
parse_fallback(const char *s, XrFallback *out)
{
	if (!strcmp(s, "off"))
		*out = XR_FALLBACK_OFF;
	else if (!strcmp(s, "horizontal"))
		*out = XR_FALLBACK_HORIZONTAL;
	else if (!strcmp(s, "vertical"))
		*out = XR_FALLBACK_VERTICAL;
	else if (!strcmp(s, "clone"))
		*out = XR_FALLBACK_CLONE;
	else
		return -1;
	return 0;
}

static int
options_parse(Options *o, const int argc, char *argv[])
{
	int nactions = 0;

	o->name = NULL;
	o->a = AUTO;
	o->names_only = 0;
	o->force = 0;
	o->verbose = 0;
	o->dry_run = 0;
	o->fallback = XR_FALLBACK_OFF;

	struct option longopts[] = {
		{ "save",         required_argument, 0, OPT_SAVE         },
		{ "load",         required_argument, 0, OPT_LOAD         },
		{ "delete",       required_argument, 0, OPT_DELETE       },
		{ "names",        no_argument,       0, OPT_NAMES        },
		{ "list",         no_argument,       0, OPT_LIST         },
		{ "list-all",     no_argument,       0, OPT_LIST_ALL     },
		{ "list-current", no_argument,       0, OPT_LIST_CURRENT },
		{ "watch",        no_argument,       0, OPT_WATCH        },
		{ "fallback",     required_argument, 0, OPT_FALLBACK     },
		{ "force",        no_argument,       0, OPT_FORCE        },
		{ "verbose",      no_argument,       0, OPT_VERBOSE      },
		{ "dry-run",      no_argument,       0, OPT_DRY_RUN      },
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

		case OPT_FALLBACK:
			if (parse_fallback(optarg, &o->fallback) < 0)
				argerr("--fallback must be one of: horizontal, vertical, clone, off");
			break;

		case OPT_FORCE:
			o->force = 1;
			break;

		case OPT_VERBOSE:
			o->verbose = 1;
			break;

		case OPT_DRY_RUN:
			o->dry_run = 1;
			break;

		case OPT_HELP:
			usage();
			exit(0);

		case OPT_VERSION:
			puts("xrandr-profile-"VERSION);
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

	const char *prog = strrchr(argv[0], '/');
	set_name(prog ? prog + 1 : argv[0]);
	options_parse(&opt, argc, argv);

	verbose = (opt.verbose || opt.dry_run) ? 1 : 0;
	dry_run = (int)opt.dry_run;

	xr_init();

	switch (opt.a) {
	case AUTO:
		if (action_apply(NULL, opt.fallback, opt.force))
			ret = 1;
		break;

	case SAVE:
		if (action_save(&opt))
			ret = 1;
		break;

	case LOAD:
		if (action_apply(opt.name, opt.fallback, opt.force))
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
		if (action_watch(opt.fallback, opt.force))
			ret = 1;
		break;
	}

	xr_free();
	return ret;
}
