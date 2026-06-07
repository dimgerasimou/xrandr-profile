/* See LICENSE file for copyright and license details.
 *
 * Tests for the profile layer (profile.c): serialization round-trips,
 * parser validation, matching, and layout comparison. Uses a throwaway
 * XDG_CONFIG_HOME so it never touches real config.
 *
 * No X dependency: this links only profile.c + utils.c.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "profile.h"
#include "utils.h"

/* ------------------------------------------------------------------ harness */

static int checks_run;
static int checks_failed;

#define CHECK(cond) do {                                                  \
		checks_run++;                                             \
		if (!(cond)) {                                            \
			checks_failed++;                                  \
			fprintf(stderr, "  FAIL %s:%d: %s\n",             \
			        __FILE__, __LINE__, #cond);               \
		}                                                         \
	} while (0)

#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)

/* ------------------------------------------------------------------ helpers */

static Profile *
full_monitor_profile(const char *name, uint64_t hash)
{
	Profile *p = profile_create(name);

	profile_append(p);
	Monitor *m = &p->m[p->len - 1];

	m->edid.hash = hash;
	snprintf(m->output,      sizeof(m->output),      "DP-1");
	snprintf(m->edid.name,   sizeof(m->edid.name),   "DELL U2720Q");
	snprintf(m->edid.serial, sizeof(m->edid.serial), "ABC123");
	m->enabled = 1;
	m->primary = 1;
	m->w = 3840;
	m->h = 2160;
	m->rate = 60.0;             /* exact under %.2f round-trip */
	m->x = 100;
	m->y = 200;
	m->rotation = 1;            /* RR_Rotate_0 */
	m->pan_x = 10;
	m->pan_y = 20;
	m->pan_w = 3840;
	m->pan_h = 2160;
	m->has_transform = 1;
	m->transform[0][0] = 1.5;   /* exact under %a round-trip */
	m->transform[1][1] = 1.5;
	m->transform[2][2] = 1.0;

	return p;
}

/* A minimal enabled monitor with just an identity + geometry. */
static void
add_simple(Profile *p, uint64_t hash, const char *output,
           int32_t x, uint16_t w, uint16_t h)
{
	profile_append(p);
	Monitor *m = &p->m[p->len - 1];

	m->edid.hash = hash;
	if (output)
		snprintf(m->output, sizeof(m->output), "%s", output);
	m->enabled = 1;
	m->w = w;
	m->h = h;
	m->x = x;
	m->rate = 60.0;
	m->rotation = 1;
}

static void
write_config(const char *dir, const char *contents)
{
	char path[4096];

	snprintf(path, sizeof(path), "%s/xrandr-profile", dir);
	mkdir(path, 0755);   /* may already exist; ignore */

	snprintf(path, sizeof(path), "%s/xrandr-profile/profiles", dir);
	FILE *fp = fopen(path, "w");
	CHECK(fp != NULL);
	if (fp) {
		fputs(contents, fp);
		fclose(fp);
	}
}

static void
clear_config(const char *dir)
{
	char path[4096];

	snprintf(path, sizeof(path), "%s/xrandr-profile/profiles", dir);
	unlink(path);
}

/* --------------------------------------------------------------- test cases */

static void
test_roundtrip_full(void)
{
	ProfileList *pl = profile_list_create();
	profile_list_append(pl, full_monitor_profile("work", 0xDEADBEEFCAFEULL));

	CHECK(profile_list_write(pl) == 0);

	ProfileList *rd = profile_list_read();
	CHECK(rd->len == 1);
	if (rd->len != 1) { profile_list_free(pl); profile_list_free(rd); return; }

	Profile *q = rd->p[0];
	CHECK_STR(q->name, "work");
	CHECK(q->len == 1);

	Monitor *m = &q->m[0];
	CHECK(m->edid.hash == 0xDEADBEEFCAFEULL);
	CHECK_STR(m->output, "DP-1");
	CHECK_STR(m->edid.name, "DELL U2720Q");
	CHECK_STR(m->edid.serial, "ABC123");
	CHECK(m->enabled && m->primary);
	CHECK(m->w == 3840 && m->h == 2160);
	CHECK(m->x == 100 && m->y == 200);
	CHECK(m->rate == 60.0);
	CHECK(m->pan_x == 10 && m->pan_y == 20);
	CHECK(m->pan_w == 3840 && m->pan_h == 2160);
	CHECK(m->rotation == 1);
	CHECK(m->has_transform);
	CHECK(m->transform[0][0] == 1.5 && m->transform[1][1] == 1.5);

	profile_list_free(pl);
	profile_list_free(rd);
}

static void
test_roundtrip_order(void)
{
	/* Three profiles, two monitors each: order must be preserved on disk. */
	ProfileList *pl = profile_list_create();
	for (int i = 0; i < 3; i++) {
		char nm[16];
		snprintf(nm, sizeof(nm), "p%d", i);
		Profile *p = profile_create(nm);
		add_simple(p, 100u + (unsigned)i, "DP-1", 0, 1920, 1080);
		add_simple(p, 200u + (unsigned)i, "DP-2", 1920, 1920, 1080);
		profile_list_append(pl, p);
	}
	CHECK(profile_list_write(pl) == 0);

	ProfileList *rd = profile_list_read();
	CHECK(rd->len == 3);
	if (rd->len == 3) {
		CHECK_STR(rd->p[0]->name, "p0");
		CHECK_STR(rd->p[1]->name, "p1");
		CHECK_STR(rd->p[2]->name, "p2");
		CHECK(rd->p[2]->len == 2);
		CHECK(rd->p[2]->m[1].edid.hash == 202);
		CHECK_STR(rd->p[2]->m[1].output, "DP-2");
	}
	profile_list_free(pl);
	profile_list_free(rd);
}

static void
test_disabled_roundtrip(void)
{
	/* A disabled monitor writes only identity + enabled; geometry is
	 * omitted and comes back zeroed, but the output name survives. */
	Profile *p = profile_create("dock");
	profile_append(p);
	Monitor *m = &p->m[0];
	m->edid.hash = 7;
	snprintf(m->output, sizeof(m->output), "eDP-1");
	snprintf(m->edid.name, sizeof(m->edid.name), "Internal");
	m->enabled = 0;
	m->w = 1920; m->h = 1080;   /* should NOT survive (disabled) */

	ProfileList *pl = profile_list_create();
	profile_list_append(pl, p);
	CHECK(profile_list_write(pl) == 0);

	ProfileList *rd = profile_list_read();
	CHECK(rd->len == 1 && rd->p[0]->len == 1);
	if (rd->len == 1 && rd->p[0]->len == 1) {
		Monitor *r = &rd->p[0]->m[0];
		CHECK(r->enabled == 0);
		CHECK(r->edid.hash == 7);
		CHECK_STR(r->output, "eDP-1");
		CHECK(r->w == 0 && r->h == 0);   /* geometry not written */
	}
	profile_list_free(pl);
	profile_list_free(rd);
}

static void
test_no_transform_roundtrip(void)
{
	/* has_transform=0 must not emit a transform field. */
	Profile *p = profile_create("plain");
	add_simple(p, 5, "DP-1", 0, 1920, 1080);   /* has_transform stays 0 */

	ProfileList *pl = profile_list_create();
	profile_list_append(pl, p);
	CHECK(profile_list_write(pl) == 0);

	ProfileList *rd = profile_list_read();
	CHECK(rd->len == 1 && rd->p[0]->len == 1);
	if (rd->len == 1 && rd->p[0]->len == 1)
		CHECK(rd->p[0]->m[0].has_transform == 0);
	profile_list_free(pl);
	profile_list_free(rd);
}

static void
test_legacy_no_output_field(const char *dir)
{
	/* Profiles written by older versions have no output= field; they
	 * must still parse, leaving output empty. */
	write_config(dir,
		"[Profile \"legacy\"]\n"
		"monitor hash=999 name=\"Old\" serial=\"S\" enabled=1 primary=1 "
			"w=2560 h=1440 rate=60.00 x=0 y=0 pan_x=0 pan_y=0 "
			"pan_w=0 pan_h=0 rotation=1\n");

	ProfileList *pl = profile_list_read();
	CHECK(pl->len == 1 && pl->p[0]->len == 1);
	if (pl->len == 1 && pl->p[0]->len == 1) {
		Monitor *m = &pl->p[0]->m[0];
		CHECK(m->edid.hash == 999);
		CHECK(m->output[0] == '\0');   /* absent -> empty, no crash */
		CHECK(m->w == 2560 && m->h == 1440);
	}
	profile_list_free(pl);
	clear_config(dir);
}

static void
test_quoting(const char *dir)
{
	/* Quoted values may contain spaces. */
	write_config(dir,
		"[Profile \"q\"]\n"
		"monitor hash=1 output=\"DP-1\" name=\"Big Screen TV\" "
			"serial=\"S 123\" enabled=0\n");

	ProfileList *pl = profile_list_read();
	CHECK(pl->len == 1 && pl->p[0]->len == 1);
	if (pl->len == 1 && pl->p[0]->len == 1) {
		Monitor *m = &pl->p[0]->m[0];
		CHECK_STR(m->edid.name, "Big Screen TV");
		CHECK_STR(m->edid.serial, "S 123");
	}
	profile_list_free(pl);
	clear_config(dir);
}

static void
test_parse_bounds(const char *dir)
{
	/* One good monitor, then a battery of malformed lines that must each
	 * be dropped while the good one survives. */
	write_config(dir,
		"[Profile \"bad\"]\n"
		"monitor hash=1 output=\"DP-1\" name=\"Good\" enabled=1 primary=1 "
			"w=1920 h=1080 rate=60.00 x=0 y=0 pan_x=0 pan_y=0 "
			"pan_w=0 pan_h=0 rotation=1\n"
		"monitor hash=2 enabled=1 w=99999999 h=1080\n"   /* w > UINT16_MAX */
		"monitor hash=3 enabled=1 w=-5 h=1080\n"         /* negative u16    */
		"monitor hash=4 enabled=1 w=abc h=1080\n"        /* non-numeric     */
		"monitor hash=5 enabled=1 w=100xx h=1080\n"      /* trailing junk   */
		"monitor hash=6 enabled=1 rotation=999\n"        /* u8 overflow     */
		"monitor hash=7 enabled=1 x=9999999999\n"        /* i32 overflow    */
		"monitor hash=8 enabled=1 rate=notanumber\n"     /* bad double      */
		"monitor hash=9 enabled=1 bogus=5\n"             /* unknown field   */
		"monitor hash=10 enabled=1 transform=[[1,0\n");  /* truncated matrix*/

	ProfileList *pl = profile_list_read();
	CHECK(pl->len == 1);
	if (pl->len == 1) {
		CHECK(pl->p[0]->len == 1);             /* only the good line kept */
		if (pl->p[0]->len == 1) {
			CHECK(pl->p[0]->m[0].edid.hash == 1);
			CHECK(pl->p[0]->m[0].w == 1920);
		}
	}
	profile_list_free(pl);
	clear_config(dir);
}

static void
test_parse_transform_ok(const char *dir)
{
	write_config(dir,
		"[Profile \"x\"]\n"
		"monitor hash=1 enabled=1 w=1920 h=1080 rate=60.00 x=0 y=0 "
			"pan_x=0 pan_y=0 pan_w=0 pan_h=0 rotation=1 "
			"transform=[[0x1.8p+0,0x0p+0,0x0p+0],"
			"[0x0p+0,0x1.8p+0,0x0p+0],"
			"[0x0p+0,0x0p+0,0x1p+0]]\n");

	ProfileList *pl = profile_list_read();
	CHECK(pl->len == 1 && pl->p[0]->len == 1);
	if (pl->len == 1 && pl->p[0]->len == 1) {
		Monitor *m = &pl->p[0]->m[0];
		CHECK(m->has_transform == 1);
		CHECK(m->transform[0][0] == 1.5);   /* 0x1.8p+0 == 1.5 exactly */
		CHECK(m->transform[1][1] == 1.5);
		CHECK(m->transform[2][2] == 1.0);
	}
	profile_list_free(pl);
	clear_config(dir);
}

static void
test_missing_header(const char *dir)
{
	/* A monitor line with no preceding [Profile] header is ignored. */
	write_config(dir, "monitor hash=1 enabled=0\n");
	ProfileList *pl = profile_list_read();
	CHECK(pl->len == 0);
	profile_list_free(pl);
	clear_config(dir);
}

static void
test_bad_header(const char *dir)
{
	/* A malformed header is skipped, but a following valid one is kept. */
	write_config(dir,
		"[Profile no-quotes]\n"
		"monitor hash=1 enabled=0\n"
		"[Profile \"good\"]\n"
		"monitor hash=2 enabled=0\n");
	ProfileList *pl = profile_list_read();
	CHECK(pl->len == 1);
	if (pl->len == 1) {
		CHECK_STR(pl->p[0]->name, "good");
		CHECK(pl->p[0]->len == 1 && pl->p[0]->m[0].edid.hash == 2);
	}
	profile_list_free(pl);
	clear_config(dir);
}

static void
test_read_missing_file(const char *dir)
{
	/* No file at all yields an empty list, not an error. */
	clear_config(dir);
	ProfileList *pl = profile_list_read();
	CHECK(pl != NULL && pl->len == 0);
	profile_list_free(pl);
}

static void
test_match(void)
{
	/* Identical hash multiset matches regardless of order. */
	Profile *a = profile_create("a");
	add_simple(a, 42, "DP-1", 0, 1920, 1080);
	add_simple(a, 99, "DP-2", 1920, 1920, 1080);

	Profile *b = profile_create("b");
	add_simple(b, 99, "DP-9", 0, 1920, 1080);   /* reversed order, diff ports */
	add_simple(b, 42, "DP-8", 1920, 1920, 1080);
	CHECK(profile_match(a, b) == 1);

	/* Count mismatch never matches. */
	Profile *c = profile_create("c");
	add_simple(c, 42, "DP-1", 0, 1920, 1080);
	CHECK(profile_match(a, c) == 0);

	/* Duplicate hashes: matching must respect multiplicity. */
	Profile *d = profile_create("d");
	add_simple(d, 7, "DP-1", 0, 1920, 1080);
	add_simple(d, 7, "DP-2", 1920, 1920, 1080);
	Profile *e = profile_create("e");
	add_simple(e, 7, "DP-1", 0, 1920, 1080);
	add_simple(e, 7, "DP-2", 1920, 1920, 1080);
	CHECK(profile_match(d, e) == 1);
	e->m[1].edid.hash = 8;                       /* one hash now unmatched */
	CHECK(profile_match(d, e) == 0);

	profile_free(a);
	profile_free(b);
	profile_free(c);
	profile_free(d);
	profile_free(e);
}

static void
test_match_cap(void)
{
	/* The matcher caps at 64 outputs; a 65-monitor set cannot match. */
	Profile *a = profile_create("big");
	Profile *b = profile_create("big2");
	for (unsigned i = 0; i < 65; i++) {
		add_simple(a, 1000u + i, NULL, 0, 1920, 1080);
		add_simple(b, 1000u + i, NULL, 0, 1920, 1080);
	}
	CHECK(a->len == 65 && b->len == 65);
	CHECK(profile_match(a, b) == 0);   /* > 64: guarded out */

	/* Exactly 64 is allowed. */
	Profile *c = profile_create("c64");
	Profile *e = profile_create("e64");
	for (unsigned i = 0; i < 64; i++) {
		add_simple(c, 2000u + i, NULL, 0, 1920, 1080);
		add_simple(e, 2000u + i, NULL, 0, 1920, 1080);
	}
	CHECK(profile_match(c, e) == 1);

	profile_free(a); profile_free(b);
	profile_free(c); profile_free(e);
}

static void
test_layout_equal(void)
{
	Profile *a = full_monitor_profile("a", 7);
	Profile *b = full_monitor_profile("b", 7);
	CHECK(profile_layout_equal(a, b) == 1);

	/* Each geometry field, in turn, must break equality. */
	b->m[0].x += 1;        CHECK(profile_layout_equal(a, b) == 0); b->m[0].x -= 1;
	b->m[0].y += 1;        CHECK(profile_layout_equal(a, b) == 0); b->m[0].y -= 1;
	b->m[0].w += 1;        CHECK(profile_layout_equal(a, b) == 0); b->m[0].w -= 1;
	b->m[0].h += 1;        CHECK(profile_layout_equal(a, b) == 0); b->m[0].h -= 1;
	b->m[0].primary = 0;   CHECK(profile_layout_equal(a, b) == 0); b->m[0].primary = 1;
	b->m[0].rotation = 2;  CHECK(profile_layout_equal(a, b) == 0); b->m[0].rotation = 1;
	b->m[0].pan_w += 1;    CHECK(profile_layout_equal(a, b) == 0); b->m[0].pan_w -= 1;
	CHECK(profile_layout_equal(a, b) == 1);

	/* Rate tolerance: < 0.01 equal, clearly beyond not. */
	b->m[0].rate += 0.005; CHECK(profile_layout_equal(a, b) == 1);
	b->m[0].rate += 0.05;  CHECK(profile_layout_equal(a, b) == 0);
	b->m[0].rate = a->m[0].rate;

	/* Transform tolerance: < 1e-6 equal, beyond not. */
	b->m[0].transform[0][0] += 1e-9; CHECK(profile_layout_equal(a, b) == 1);
	b->m[0].transform[0][0] += 1e-3; CHECK(profile_layout_equal(a, b) == 0);
	b->m[0].transform[0][0] = a->m[0].transform[0][0];

	/* has_transform mismatch. */
	b->m[0].has_transform = 0; CHECK(profile_layout_equal(a, b) == 0);
	b->m[0].has_transform = 1; CHECK(profile_layout_equal(a, b) == 1);

	/* Both disabled: geometry ignored. */
	a->m[0].enabled = 0; b->m[0].enabled = 0; b->m[0].x = 9999;
	CHECK(profile_layout_equal(a, b) == 1);
	b->m[0].enabled = 1;       /* enabled-state mismatch */
	CHECK(profile_layout_equal(a, b) == 0);
	a->m[0].enabled = 1; b->m[0].x = a->m[0].x;
	CHECK(profile_layout_equal(a, b) == 1);

	/* Different monitor set. */
	b->m[0].edid.hash = 8; CHECK(profile_layout_equal(a, b) == 0);

	/* Count mismatch. */
	b->m[0].edid.hash = 7;
	add_simple(b, 7, "DP-2", 0, 1920, 1080);
	CHECK(profile_layout_equal(a, b) == 0);

	profile_free(a);
	profile_free(b);
}

static void
test_list_ops(void)
{
	ProfileList *pl = profile_list_create();

	profile_list_append(pl, profile_create("a"));
	profile_list_append(pl, profile_create("b"));
	profile_list_prepend(pl, profile_create("z"));   /* z, a, b */
	CHECK(pl->len == 3);
	CHECK_STR(pl->p[0]->name, "z");
	CHECK_STR(pl->p[1]->name, "a");
	CHECK_STR(pl->p[2]->name, "b");

	profile_list_delete(pl, "a");                    /* z, b */
	CHECK(pl->len == 2);
	CHECK_STR(pl->p[0]->name, "z");
	CHECK_STR(pl->p[1]->name, "b");

	profile_list_delete(pl, "nope");                 /* no-op */
	CHECK(pl->len == 2);

	profile_list_delete(pl, "b");                    /* z */
	profile_list_delete(pl, "z");                    /* empty */
	CHECK(pl->len == 0);

	profile_list_free(pl);
}

static void
test_config_dir(void)
{
	char buf[4096];
	const char *xdg = getenv("XDG_CONFIG_HOME");
	char expect[4096];

	if (!xdg)
		return;
	snprintf(expect, sizeof(expect), "%s/xrandr-profile", xdg);
	profile_config_dir(buf, sizeof(buf));
	CHECK_STR(buf, expect);
}

int
main(void)
{
	char tmpl[] = "/tmp/xrp_test_XXXXXX";
	char *dir   = mkdtemp(tmpl);

	set_name("test-profile");

	if (!dir) {
		fprintf(stderr, "mkdtemp failed\n");
		return 1;
	}
	if (setenv("XDG_CONFIG_HOME", dir, 1) != 0) {
		fprintf(stderr, "setenv failed\n");
		return 1;
	}

	fprintf(stderr, "(parser tests below intentionally emit warnings)\n");

	test_roundtrip_full();
	test_roundtrip_order();
	test_disabled_roundtrip();
	test_no_transform_roundtrip();
	test_legacy_no_output_field(dir);
	test_quoting(dir);
	test_parse_bounds(dir);
	test_parse_transform_ok(dir);
	test_missing_header(dir);
	test_bad_header(dir);
	test_read_missing_file(dir);
	test_match();
	test_match_cap();
	test_layout_equal();
	test_list_ops();
	test_config_dir();

	/* tidy up the temp dir */
	clear_config(dir);
	char p[4096];
	snprintf(p, sizeof(p), "%s/xrandr-profile", dir); rmdir(p);
	rmdir(dir);

	if (checks_failed) {
		fprintf(stderr, "%d/%d checks FAILED\n", checks_failed, checks_run);
		return 1;
	}
	printf("test-profile: %d checks passed\n", checks_run);
	return 0;
}
