/* See LICENSE file for copyright and license details.
 *
 * Tests for the pure logic inside xrandr.c that does not touch the X server:
 *   - compute_framebuffer(): bounding-box math (offsets, rotation, scaling
 *     transform, panning, disabled outputs)
 *   - bind_outputs(): output binding (exact hash+port match, identical-EDID
 *     disambiguation, port-moved fallback, EDID-less name-only matching)
 *
 * Both are file-static, so this test includes the translation unit directly.
 * It calls no Xlib functions, so it needs no display at run time (it links
 * the X libraries only to resolve symbols referenced elsewhere in the unit).
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xrandr.c"   /* pulls in static compute_framebuffer, bind_outputs, OutCache */

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

/* ------------------------------------------------------------------ helpers */

static void
add_mon(Profile *p, uint64_t hash, const char *output, uint8_t enabled,
        int32_t x, int32_t y, uint16_t w, uint16_t h, uint8_t rotation)
{
	profile_append(p);
	Monitor *m = &p->m[p->len - 1];

	m->edid.hash = hash;
	if (output)
		snprintf(m->output, sizeof(m->output), "%s", output);
	m->enabled  = enabled;
	m->x = x; m->y = y; m->w = w; m->h = h;
	m->rotation = rotation;
	m->rate = 60.0;
}

static XRROutputInfo *
fake_info(const char *name)
{
	XRROutputInfo *oi = calloc(1, sizeof(*oi));
	if (oi)
		oi->name = strdup(name);   /* bind_outputs reads only ->name */
	return oi;
}

static void
free_cache(OutCache *c, int n)
{
	for (int i = 0; i < n; i++) {
		free(c[i].info->name);
		free(c[i].info);
	}
}

/* ------------------------------------------------- compute_framebuffer tests */

static void
test_fb_single(void)
{
	Profile *p = profile_create("s");
	add_mon(p, 1, "DP-1", 1, 0, 0, 1920, 1080, RR_Rotate_0);
	int w, h;
	compute_framebuffer(p, &w, &h);
	CHECK(w == 1920 && h == 1080);
	profile_free(p);
}

static void
test_fb_horizontal(void)
{
	Profile *p = profile_create("h");
	add_mon(p, 1, "DP-1", 1, 0,    0, 1920, 1080, RR_Rotate_0);
	add_mon(p, 2, "DP-2", 1, 1920, 0, 2560, 1440, RR_Rotate_0);
	int w, h;
	compute_framebuffer(p, &w, &h);
	CHECK(w == 1920 + 2560);   /* widest right edge */
	CHECK(h == 1440);          /* tallest */
	profile_free(p);
}

static void
test_fb_vertical(void)
{
	Profile *p = profile_create("v");
	add_mon(p, 1, "DP-1", 1, 0, 0,    1920, 1080, RR_Rotate_0);
	add_mon(p, 2, "DP-2", 1, 0, 1080, 1920, 1200, RR_Rotate_0);
	int w, h;
	compute_framebuffer(p, &w, &h);
	CHECK(w == 1920);
	CHECK(h == 1080 + 1200);
	profile_free(p);
}

static void
test_fb_rotation(void)
{
	/* A 90-degree rotation swaps width and height in the footprint. */
	Profile *p = profile_create("r");
	add_mon(p, 1, "DP-1", 1, 0, 0, 1920, 1080, RR_Rotate_90);
	int w, h;
	compute_framebuffer(p, &w, &h);
	CHECK(w == 1080 && h == 1920);

	p->m[0].rotation = RR_Rotate_270;   /* also swaps */
	compute_framebuffer(p, &w, &h);
	CHECK(w == 1080 && h == 1920);

	p->m[0].rotation = RR_Rotate_180;   /* does NOT swap */
	compute_framebuffer(p, &w, &h);
	CHECK(w == 1920 && h == 1080);
	profile_free(p);
}

static void
test_fb_transform(void)
{
	/* A pure-scale transform enlarges the footprint (ceil of scaled size). */
	Profile *p = profile_create("t");
	add_mon(p, 1, "DP-1", 1, 0, 0, 1920, 1080, RR_Rotate_0);
	p->m[0].has_transform = 1;
	p->m[0].transform[0][0] = 1.5;
	p->m[0].transform[1][1] = 1.5;
	p->m[0].transform[2][2] = 1.0;
	int w, h;
	compute_framebuffer(p, &w, &h);
	CHECK(w == 2880 && h == 1620);   /* 1920*1.5, 1080*1.5 */
	profile_free(p);
}

static void
test_fb_panning(void)
{
	/* A panning rectangle larger than the mode extends the bounding box. */
	Profile *p = profile_create("pan");
	add_mon(p, 1, "DP-1", 1, 0, 0, 1920, 1080, RR_Rotate_0);
	p->m[0].pan_x = 0; p->m[0].pan_y = 0;
	p->m[0].pan_w = 2560; p->m[0].pan_h = 1440;
	int w, h;
	compute_framebuffer(p, &w, &h);
	CHECK(w == 2560 && h == 1440);
	profile_free(p);
}

static void
test_fb_disabled_and_empty(void)
{
	/* Disabled outputs contribute nothing; an all-disabled profile floors
	 * to 1x1 rather than 0x0. */
	Profile *p = profile_create("d");
	add_mon(p, 1, "DP-1", 1, 0, 0, 1920, 1080, RR_Rotate_0);
	add_mon(p, 2, "DP-2", 0, 5000, 5000, 9999, 9999, RR_Rotate_0); /* disabled */
	int w, h;
	compute_framebuffer(p, &w, &h);
	CHECK(w == 1920 && h == 1080);

	Profile *e = profile_create("empty");
	compute_framebuffer(e, &w, &h);
	CHECK(w == 1 && h == 1);

	p->m[0].enabled = 0;
	compute_framebuffer(p, &w, &h);
	CHECK(w == 1 && h == 1);

	profile_free(p);
	profile_free(e);
}

/* -------------------------------------------------------- bind_outputs tests */

static void
test_bind_identical_panels(void)
{
	/* Two identical panels (same hash) saved on DP-1/DP-2. The live cache
	 * is in REVERSED order; binding must follow the port name, not order,
	 * so geometry does not get swapped between the two physical units. */
	const uint64_t H = 0xABCD;
	Profile *p = profile_create("dual");
	add_mon(p, H, "DP-1", 1, 0,    0, 1920, 1080, RR_Rotate_0);
	add_mon(p, H, "DP-2", 1, 1920, 0, 1920, 1080, RR_Rotate_0);

	OutCache c[2] = {0};
	c[0].hash = H; c[0].info = fake_info("DP-2");
	c[1].hash = H; c[1].info = fake_info("DP-1");

	int a[2];
	bind_outputs(p, c, 2, a);
	CHECK(a[0] == 1);   /* saved DP-1 -> cache entry named DP-1 */
	CHECK(a[1] == 0);   /* saved DP-2 -> cache entry named DP-2 */
	CHECK(c[0].used && c[1].used);

	free_cache(c, 2);
	profile_free(p);
}

static void
test_bind_port_moved(void)
{
	/* Saved DP-1 is gone; live has DP-2 and DP-9 (same model). The exact
	 * DP-2 match is reserved first, then DP-1 falls back to the leftover. */
	const uint64_t H = 0xABCD;
	Profile *p = profile_create("dual");
	add_mon(p, H, "DP-1", 1, 0,    0, 1920, 1080, RR_Rotate_0);
	add_mon(p, H, "DP-2", 1, 1920, 0, 1920, 1080, RR_Rotate_0);

	OutCache c[2] = {0};
	c[0].hash = H; c[0].info = fake_info("DP-9");
	c[1].hash = H; c[1].info = fake_info("DP-2");

	int a[2];
	bind_outputs(p, c, 2, a);
	CHECK(a[1] == 1);   /* DP-2 exact-binds first */
	CHECK(a[0] == 0);   /* DP-1 gone -> leftover DP-9 (fuzzy, same hash) */

	free_cache(c, 2);
	profile_free(p);
}

static void
test_bind_edidless(void)
{
	/* An EDID-less monitor (hash 0) binds only by exact port name; a
	 * wrong port must NOT be grabbed by the same-hash fallback. */
	Profile *p = profile_create("kvm");
	add_mon(p, 0, "HDMI-1", 1, 0, 0, 1920, 1080, RR_Rotate_0);

	OutCache c[1] = {0};
	c[0].hash = 0; c[0].info = fake_info("HDMI-2");   /* different port */

	int a[1];
	bind_outputs(p, c, 1, a);
	CHECK(a[0] == -1);          /* no match */
	CHECK(c[0].used == 0);

	/* Correct port: matches. */
	free(c[0].info->name);
	c[0].info->name = strdup("HDMI-1");
	bind_outputs(p, c, 1, a);
	CHECK(a[0] == 0);
	CHECK(c[0].used == 1);

	free_cache(c, 1);
	profile_free(p);
}

static void
test_bind_missing_output(void)
{
	/* A monitor whose hash is present but whose exact port is absent still
	 * binds via the same-hash fallback (single panel, replugged). */
	Profile *p = profile_create("one");
	add_mon(p, 42, "DP-1", 1, 0, 0, 1920, 1080, RR_Rotate_0);

	OutCache c[1] = {0};
	c[0].hash = 42; c[0].info = fake_info("HDMI-3");   /* same model, new port */

	int a[1];
	bind_outputs(p, c, 1, a);
	CHECK(a[0] == 0);          /* fuzzy same-hash bind */

	free_cache(c, 1);
	profile_free(p);
}

static void
test_bind_no_output_for_hash(void)
{
	/* No connected output has the saved hash: unmatched. */
	Profile *p = profile_create("gone");
	add_mon(p, 42, "DP-1", 1, 0, 0, 1920, 1080, RR_Rotate_0);

	OutCache c[1] = {0};
	c[0].hash = 7; c[0].info = fake_info("DP-1");   /* same port, wrong panel */

	int a[1];
	bind_outputs(p, c, 1, a);
	CHECK(a[0] == -1);

	free_cache(c, 1);
	profile_free(p);
}

static void
test_bind_disabled_skipped(void)
{
	/* Disabled monitors are never bound. */
	Profile *p = profile_create("dis");
	add_mon(p, 42, "DP-1", 0, 0, 0, 1920, 1080, RR_Rotate_0);  /* disabled */

	OutCache c[1] = {0};
	c[0].hash = 42; c[0].info = fake_info("DP-1");

	int a[1];
	bind_outputs(p, c, 1, a);
	CHECK(a[0] == -1);
	CHECK(c[0].used == 0);     /* output left free for an enabled monitor */

	free_cache(c, 1);
	profile_free(p);
}

int
main(void)
{
	set_name("test-xrandr");

	test_fb_single();
	test_fb_horizontal();
	test_fb_vertical();
	test_fb_rotation();
	test_fb_transform();
	test_fb_panning();
	test_fb_disabled_and_empty();

	test_bind_identical_panels();
	test_bind_port_moved();
	test_bind_edidless();
	test_bind_missing_output();
	test_bind_no_output_for_hash();
	test_bind_disabled_skipped();

	if (checks_failed) {
		fprintf(stderr, "%d/%d checks FAILED\n", checks_failed, checks_run);
		return 1;
	}
	printf("test-xrandr: %d checks passed\n", checks_run);
	return 0;
}
