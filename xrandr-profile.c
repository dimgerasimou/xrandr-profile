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
action_auto(void)
{
	ProfileList *pl;
	Profile     *cur;
	int          ret = 0;

	pl  = profile_list_read();
	cur = xr_active_profile();

	for (size_t i = 0; i < pl->len; i++) {
		if (profile_match(pl->p[i], cur)) {
			Profile *sel = pl->p[i];
			
			for (size_t j = i ; j > 0; j--)
				pl->p[j] = pl->p[j-1];

			pl->p[0] = sel;

			xr_apply_profile(sel);

			if (profile_list_write(pl))
				ret = -1;
			break;
		}
	}

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
action_load(Options *opt)
{
	ProfileList *pl;
	Profile     *cur;
	int          ret = 0;

	pl  = profile_list_read();
	cur = xr_active_profile();

	for (size_t i = 0; i < pl->len; i++) {
		if (profile_match(pl->p[i], cur) && !strcmp(pl->p[i]->name, opt->name)) {
			Profile *sel = pl->p[i];
			
			for (size_t j = i ; j > 0; j--)
				pl->p[j] = pl->p[j-1];

			pl->p[0] = sel;

			xr_apply_profile(sel);

			if (profile_list_write(pl))
				ret = -1;
			break;
		}
	}

	profile_list_free(pl);
	profile_free(cur);
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

static void
options_parse(Options *opt, const int argc, const char *argv[])
{
	opt->name = NULL;
	opt->a = AUTO;

	for (int i = 1; i < argc; i++) {
		if (!strcmp ("-help", argv[i]) || !strcmp ("--help", argv[i]) || !strcmp ("-h", argv[i])) {
			usage();
			exit(0);
		}
		if (!strcmp ("-version", argv[i]) || !strcmp ("--version", argv[i]) || !strcmp ("-v", argv[i])) {
			printf("%s-"VERSION"\n", get_name());
			exit(0);
		}
		if (!strcmp ("-save", argv[i]) || !strcmp ("--save", argv[i])) {
			if (++i >= argc)
				argerr("%s requires an argument", argv[i-1]);
			opt->name = argv[i];
			opt->a = SAVE;
			break;
		}
		if (!strcmp ("-load", argv[i]) || !strcmp ("--load", argv[i])) {
			if (++i >= argc)
				argerr("%s requires an argument", argv[i-1]);
			opt->name = argv[i];
			opt->a = LOAD;
			break;
		}
		if (!strcmp ("-delete", argv[i]) || !strcmp ("--delete", argv[i])) {
			if (++i >= argc)
				argerr("%s requires an argument", argv[i-1]);
			opt->name = argv[i];
			opt->a = DELETE;
			break;
		}
		if (!strcmp ("-list", argv[i]) || !strcmp ("--list", argv[i])) {
			opt->a = LIST;
			break;
		}
		if (!strcmp ("-list-all", argv[i]) || !strcmp ("--list-all", argv[i])) {
			opt->a = LIST_ALL;
			break;
		}
		if (!strcmp ("-list-current", argv[i]) || !strcmp ("--list-current", argv[i])) {
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
		if (action_auto())
			ret = 1;
		break;

	case SAVE:
		if (action_save(&opt))
			ret = 1;
		break;

	case LOAD:
		if (action_load(&opt))
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
