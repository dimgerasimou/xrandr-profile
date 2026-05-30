/* See LICENSE file for copyright and license details.
 *
 * Minimal round-trip and matching tests for the profile layer.
 * Uses a throwaway XDG_CONFIG_HOME so it never touches real config.
 */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "profile.h"
#include "utils.h"

static Profile *
one_monitor_profile(const char *name, uint64_t hash)
{
	Profile *p = profile_create(name);

	profile_append(p);
	Monitor *m = &p->m[p->len - 1];

	m->edid.hash = hash;
	snprintf(m->edid.name,   sizeof(m->edid.name),   "DELL U2720Q");
	snprintf(m->edid.serial, sizeof(m->edid.serial), "ABC123");
	m->enabled = 1;
	m->primary = 1;
	m->w = 3840;
	m->h = 2160;
	m->rate = 60.0;             /* exact under %.2f round-trip */
	m->rotation = 1;            /* RR_Rotate_0 */
	m->pan_w = 3840;
	m->pan_h = 2160;
	m->has_transform = 1;
	m->transform[0][0] = 1.5;   /* exact under %a round-trip */
	m->transform[1][1] = 1.5;
	m->transform[2][2] = 1.0;

	return p;
}

/* Write raw config text to $XDG_CONFIG_HOME/xrandr-profile/profiles. */
static void
write_config(const char *dir, const char *contents)
{
	char path[4096];

	snprintf(path, sizeof(path), "%s/xrandr-profile", dir);
	mkdir(path, 0755);   /* may already exist; ignore */

	snprintf(path, sizeof(path), "%s/xrandr-profile/profiles", dir);
	FILE *fp = fopen(path, "w");
	assert(fp);
	fputs(contents, fp);
	fclose(fp);
}

static void
cleanup(const char *dir)
{
	char path[4096];

	snprintf(path, sizeof(path), "%s/xrandr-profile/profiles", dir);
	unlink(path);
	snprintf(path, sizeof(path), "%s/xrandr-profile", dir);
	rmdir(path);
	rmdir(dir);
}

static void
test_roundtrip(void)
{
	ProfileList *pl = profile_list_create();
	profile_list_append(pl, one_monitor_profile("work", 0xDEADBEEFCAFEULL));

	assert(profile_list_write(pl) == 0);

	ProfileList *rd = profile_list_read();
	assert(rd->len == 1);

	Profile *q = rd->p[0];
	assert(strcmp(q->name, "work") == 0);
	assert(q->len == 1);

	Monitor *m = &q->m[0];
	assert(m->edid.hash == 0xDEADBEEFCAFEULL);
	assert(strcmp(m->edid.name, "DELL U2720Q") == 0);
	assert(strcmp(m->edid.serial, "ABC123") == 0);
	assert(m->enabled && m->primary);
	assert(m->w == 3840 && m->h == 2160);
	assert(m->rate == 60.0);
	assert(m->pan_w == 3840 && m->pan_h == 2160);
	assert(m->has_transform);
	assert(m->transform[0][0] == 1.5 && m->transform[1][1] == 1.5);

	profile_list_free(pl);
	profile_list_free(rd);
}

static void
test_match_multiplicity(void)
{
	/* Two panels with identical EDID hashes: matching must respect count. */
	Profile *a = profile_create("dual");
	profile_append(a);
	profile_append(a);
	a->m[0].edid.hash = 42;
	a->m[1].edid.hash = 42;

	Profile *b = profile_create("dual2");
	profile_append(b);
	profile_append(b);
	b->m[0].edid.hash = 42;
	b->m[1].edid.hash = 42;

	assert(profile_match(a, b) == 1);

	b->m[1].edid.hash = 99;
	assert(profile_match(a, b) == 0);   /* one hash now unmatched */

	Profile *c = profile_create("single");
	profile_append(c);
	c->m[0].edid.hash = 42;
	assert(profile_match(a, c) == 0);   /* different monitor count */

	profile_free(a);
	profile_free(b);
	profile_free(c);
}

static void
test_malformed(const char *dir)
{
	/* One well-formed monitor followed by several broken lines. The reader
	 * must keep the good line and drop each bad one rather than leaving a
	 * half-parsed monitor in the profile. */
	write_config(dir,
		"[Profile \"broken\"]\n"
		"monitor hash=111 name=\"Good\" serial=\"S1\" enabled=1 primary=1 "
			"w=1920 h=1080 rate=60.00 x=0 y=0 pan_x=0 pan_y=0 "
			"pan_w=0 pan_h=0 rotation=1\n"
		"monitor hash=222 name=\"BadW\" enabled=1 w=99999999 h=1080\n"   /* w > UINT16_MAX */
		"monitor hash=333 name=\"BadKey\" enabled=1 bogus=5\n"           /* unknown field   */
		"monitor hash=444 name=\"BadXform\" enabled=1 transform=[[1,0\n" /* truncated matrix */
	);

	ProfileList *pl = profile_list_read();
	assert(pl->len == 1);

	Profile *p = pl->p[0];
	assert(strcmp(p->name, "broken") == 0);
	assert(p->len == 1);                       /* only the good line survived */
	assert(p->m[0].edid.hash == 111);
	assert(p->m[0].w == 1920 && p->m[0].h == 1080);

	profile_list_free(pl);

	/* A monitor line with no preceding profile header is ignored entirely. */
	write_config(dir, "monitor hash=1 enabled=0\n");
	pl = profile_list_read();
	assert(pl->len == 0);
	profile_list_free(pl);
}

int
main(void)
{
	char tmpl[] = "/tmp/xrp_test_XXXXXX";
	char *dir   = mkdtemp(tmpl);

	set_name("test");

	if (!dir) {
		fprintf(stderr, "mkdtemp failed\n");
		return 1;
	}
	if (setenv("XDG_CONFIG_HOME", dir, 1) != 0) {
		fprintf(stderr, "setenv failed\n");
		return 1;
	}

	test_roundtrip();
	test_match_multiplicity();
	test_malformed(dir);

	cleanup(dir);

	printf("all tests passed\n");
	return 0;
}
