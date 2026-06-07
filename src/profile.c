/* See LICENSE file for copyright and license details. */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <X11/extensions/randr.h>

#include "profile.h"
#include "utils.h"

static const char *rotation_name(const uint8_t rotation);
static const char *reflection_name (const uint8_t rotation);
static       int   make_directory(const char *path);
static       void  pl_grow(ProfileList *pl);
static       void  config_path_to(char *buf, const size_t bufsz, const char *suffix);
static       void  config_path(char *buf, const size_t bufsz);
static       int   parse_u8(const char *s, uint8_t *out);
static       int   parse_u16(const char *s, uint16_t *out);
static       int   parse_u64(const char *s, uint64_t *out);
static       int   parse_i32(const char *s, int32_t *out);
static       int   parse_double(const char *s, double *out);
static       int   report_malformed_line(const char *key, const char *val);
static       int   parse_monitor_line(const char *line, Monitor *m);

static const char *
rotation_name(const uint8_t rotation)
{
	switch (rotation & (RR_Rotate_0 | RR_Rotate_90 | RR_Rotate_180 | RR_Rotate_270)) {
	case RR_Rotate_0:
		return "normal";
	case RR_Rotate_90:
		return "left";
	case RR_Rotate_180:
		return "inverted";
	case RR_Rotate_270:
		return "right";
	}
	return "invalid rotation";
}

static const char *
reflection_name (const uint8_t rotation)
{
	switch (rotation & (RR_Reflect_X | RR_Reflect_Y)) {
	case 0:
		return "none";
	case RR_Reflect_X:
		return "X axis";
	case RR_Reflect_Y:
		return "Y axis";
	case RR_Reflect_X | RR_Reflect_Y:
		return "X and Y axis";
	}
	return "invalid reflection";
}

static int
make_directory(const char *path)
{
	char dir[PATH_MAX];
	char *slash;

	snprintf(dir, sizeof(dir), "%s", path);
	slash = strrchr(dir, '/');

	if (!slash)
		return 0;

	*slash = '\0';

	for (char *p = dir + 1; *p; p++) {
		if (*p != '/')
			continue;

		*p = '\0';

		if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
			warn("mkdir(%s) failed:", dir);
			return -1;
		}

		*p = '/';
	}

	if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
		warn("mkdir(%s) failed:", dir);
		return -1;
	}

	return 0;
}

static void
config_path_to(char *buf, const size_t bufsz, const char *suffix)
{
	const char *xdg  = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");

	if (xdg && xdg[0])
		snprintf(buf, bufsz, "%s/xrandr-profile%s", xdg, suffix);
	else if (home && home[0])
		snprintf(buf, bufsz, "%s/.config/xrandr-profile%s", home, suffix);
	else
		die("\"HOME\" is not set");
}

static void
config_path(char *buf, const size_t bufsz)
{
	config_path_to(buf, bufsz, "/profiles");
}

static int
parse_u8(const char *s, uint8_t *out)
{
	char *ep;
	unsigned long v;

	errno = 0;
	v = strtoul(s, &ep, 10);
	if (errno || ep == s || *ep || v > UINT8_MAX)
		return -1;
	*out = (uint8_t)v;
	return 0;
}

static int
parse_u16(const char *s, uint16_t *out)
{
	char *ep;
	unsigned long v;

	errno = 0;
	v = strtoul(s, &ep, 10);
	if (errno || ep == s || *ep || v > UINT16_MAX)
		return -1;
	*out = (uint16_t)v;
	return 0;
}

static int
parse_u64(const char *s, uint64_t *out)
{
	char *ep;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &ep, 10);
	if (errno || ep == s || *ep)
		return -1;
	*out = (uint64_t)v;
	return 0;
}

static int
parse_i32(const char *s, int32_t *out)
{
	char *ep;
	long v;

	errno = 0;
	v = strtol(s, &ep, 10);
	if (errno || ep == s || *ep || v < INT32_MIN || v > INT32_MAX)
		return -1;
	*out = (int32_t)v;
	return 0;
}

static int
parse_double(const char *s, double *out)
{
	char *ep;
	double v;

	errno = 0;
	v = strtod(s, &ep);
	if (errno || ep == s || *ep)
		return -1;
	*out = v;
	return 0;
}

static int
report_malformed_line(const char *key, const char *val)
{
	warn("Malformed value for \"%s\": \"%s\"", key, val);
	return -1;
}

static int
parse_monitor_line(const char *line, Monitor *m)
{
	line += strlen("monitor ");

	while (*line) {
		while (*line == ' ' || *line == '\t')
			line++;

		if (!*line)
			break;

		char key[32];
		size_t ki = 0;

		while (*line && *line != '=' && ki < sizeof(key) - 1)
			key[ki++] = *line++;
		key[ki] = '\0';

		if (*line != '=') {
			if (ki == sizeof(key) - 1)
				warn("Key too long: \"%s...\"", key);
			else
				warn("Missing '=' after key \"%s\"", key);
			return -1;
		}
		line++;

		char val[512];
		size_t vi = 0;

		if (*line == '"') {
			line++;
			while (*line && *line != '"' && vi < sizeof(val) - 1)
				val[vi++] = *line++;

			if (*line == '"')
				line++;
		} else {
			while (*line && *line != ' ' && *line != '\t' && vi < sizeof(val) - 1)
				val[vi++] = *line++;
		}
		val[vi] = '\0';

		if (!strcmp(key, "hash")) {
			if (parse_u64(val, &m->edid.hash) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "name")) {
			snprintf(m->edid.name, sizeof(m->edid.name), "%.*s",
			         (int)sizeof(m->edid.name) - 1, val);
		} else if (!strcmp(key, "serial")) {
			snprintf(m->edid.serial, sizeof(m->edid.serial), "%.*s",
			         (int)sizeof(m->edid.serial) - 1, val);
		} else if (!strcmp(key, "primary")) {
			if (parse_u8(val, &m->primary) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "enabled")) {
			if (parse_u8(val, &m->enabled) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "w")) {
			if (parse_u16(val, &m->w) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "h")) {
			if (parse_u16(val, &m->h) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "rate")) {
			if (parse_double(val, &m->rate) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "x")) {
			if (parse_i32(val, &m->x) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "y")) {
			if (parse_i32(val, &m->y) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "pan_x")) {
			if (parse_i32(val, &m->pan_x) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "pan_y")) {
			if (parse_i32(val, &m->pan_y) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "pan_w")) {
			if (parse_u16(val, &m->pan_w) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "pan_h")) {
			if (parse_u16(val, &m->pan_h) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "rotation")) {
			if (parse_u8(val, &m->rotation) < 0)
				return report_malformed_line(key, val);
		} else if (!strcmp(key, "transform")) {
			if (sscanf(val, "[[%la,%la,%la],[%la,%la,%la],[%la,%la,%la]]",
			             &m->transform[0][0], &m->transform[0][1], &m->transform[0][2],
			             &m->transform[1][0], &m->transform[1][1], &m->transform[1][2],
			             &m->transform[2][0], &m->transform[2][1], &m->transform[2][2]) != 9)
				return report_malformed_line(key, val);
			m->has_transform = 1;
		} else {
			warn("Unknown field \"%s\"", key);
			return -1;
		}
	}

	return 0;
}

Profile *
profile_create(const char *name)
{
	Profile *p;

	p = ecalloc(1, sizeof(Profile));

	if (name)
		snprintf(p->name, sizeof(p->name), "%s", name);

	return p;
}

void
profile_free(Profile *p)
{
	if (!p)
		return;

	free(p->m);
	free(p);
}

void
profile_append(Profile *p)
{
	if (p->len == p->cap) {
		size_t newcap;
		Monitor *tmp;

		newcap = p->cap ? p->cap * 2 : 4;
		tmp = erealloc(p->m, newcap * sizeof(Monitor));
		p->m = tmp;
		p->cap = newcap;
	}

	p->m[p->len] = (Monitor){0};
	p->len++;
}

void
profile_print(const Profile *p, const unsigned int names_only)
{
	if (names_only) {
		if (p->name[0])
			puts(p->name);
		return;
	}

	printf("Profile Name: %s\n", p->name[0] ? p->name : "(no profile name)");

	for (size_t i = 0; i < p->len; i++) {
		Monitor *m = &p->m[i];

		if (m->output[0])
			printf("  Output:     %s\n",    m->output);
		printf("  Name:       %s\n",    m->edid.name[0] ? m->edid.name : "(no name)");
		printf("  Serial:     %s\n",    m->edid.serial[0]  ? m->edid.serial  : "(no serial)");
		printf("  Primary:    %s\n",    m->primary ? "yes" : "no");
		printf("  Enabled:    %s\n",    m->enabled ? "yes" : "no");
		if (m->enabled) {
			printf("  Mode:       %ux%u @ %.0fHz\n", m->w, m->h, m->rate);
			printf("  Pos:        %d,%d\n", m->x, m->y);
			printf("  Pan:        pos:%d,%d size:%u,%u\n", m->pan_x, m->pan_y, m->pan_w, m->pan_h);
			printf("  Rotation:   %s\n",    rotation_name(m->rotation));
			printf("  Reflection: %s\n",    reflection_name(m->rotation));
			if (m->has_transform)
				printf("  Transform:  scale=%.4gx%.4g\n", m->transform[0][0], m->transform[1][1]);
		}
		printf("\n");
	}
}

ProfileList *
profile_list_create(void)
{
	return ecalloc(1, sizeof(ProfileList));
}

void
profile_list_free(ProfileList *pl)
{
	if (!pl)
		return;

	for (size_t i = 0; i < pl->len; i++)
		profile_free(pl->p[i]);

	free(pl->p);
	free(pl);
}

static void
pl_grow(ProfileList *pl)
{
	if (pl->len == pl->cap) {
		size_t newcap = pl->cap ? pl->cap * 2 : 4;
		pl->p = erealloc(pl->p, newcap * sizeof(Profile *));
		pl->cap = newcap;
	}
}

void
profile_list_append(ProfileList *pl, Profile *p)
{
	pl_grow(pl);
	pl->p[pl->len++] = p;
}

void
profile_list_prepend(ProfileList *pl, Profile *p)
{
	pl_grow(pl);
	memmove(pl->p + 1, pl->p, pl->len * sizeof(Profile *));
	pl->p[0] = p;
	pl->len++;
}

void
profile_list_delete(ProfileList *pl, const char *name)
{
	if (!pl || !name)
		return;

	for (size_t i = 0; i < pl->len; i++) {
		if (!strcmp(pl->p[i]->name, name)) {
			profile_free(pl->p[i]);

			for (size_t j = i + 1; j < pl->len; j++)
				pl->p[j-1] = pl->p[j];

			pl->len--;
			return;
		}
	}
}

ProfileList *
profile_list_read(void)
{
	char path[PATH_MAX];
	char line[1024];
	FILE *fp;
	ProfileList *pl;
	Profile *cur = NULL;

	config_path(path, sizeof(path));
	fp = fopen(path, "r");

	if (!fp) {
		if (errno == ENOENT)
			return profile_list_create();

		die("Can't open file \"%s\":", path);
	}

	pl = profile_list_create();

	while (fgets(line, sizeof(line), fp)) {
		size_t len = strlen(line);

		if (len && line[len - 1] == '\n')
			line[--len] = '\0';

		if (!len)
			continue;

		if (line[0] == '[') {
			char name[64] = {0};

			if (sscanf(line, "[Profile \"%63[^\"]\"", name) != 1) {
				warn("Malformed profile header: %s", line);
				continue;
			}

			cur = profile_create(name);
			profile_list_append(pl, cur);
		} else if (strncmp(line, "monitor ", 8) == 0) {
			if (!cur) {
				warn("Missing profile header");
				continue;
			}

			profile_append(cur);

			if (parse_monitor_line(line, &cur->m[cur->len - 1]) < 0)
				cur->len--;
		}
	}

	fclose(fp);
	return pl;
}

int
profile_list_write(const ProfileList *pl)
{
	char path[PATH_MAX];
	char tmp[PATH_MAX + 4];
	FILE *fp;

	config_path(path, sizeof(path));
	if (make_directory(path) < 0)
		return -1;

	if (strlen(path) + 4 >= PATH_MAX) {
		warn("Path name too long: %s", path);
		return -1;
	}

	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	fp = fopen(tmp, "w");

	if (!fp) {
		warn("Can't write to \"%s\"", tmp);
		return -1;
	}

	for (size_t i = 0; i < pl->len; i++) {
		const Profile *p = pl->p[i];

		fprintf(fp, "[Profile \"%s\"]\n", p->name);

		for (size_t j = 0; j < p->len; j++) {
			const Monitor *m = &p->m[j];

			fprintf(fp, "monitor hash=%" PRIu64 " name=\"%s\" serial=\"%s\" enabled=%u", m->edid.hash,  m->edid.name,  m->edid.serial, m->enabled);
			if (m->enabled) {
				fprintf(fp, " primary=%u w=%u h=%u rate=%.2f x=%d y=%d pan_x=%d pan_y=%d pan_w=%u pan_h=%u rotation=%u",
				        m->primary, m->w, m->h, m->rate, m->x, m->y, m->pan_x, m->pan_y, m->pan_w, m->pan_h, m->rotation);
				if (m->has_transform) {
					fprintf(fp, " transform=[[%a,%a,%a],[%a,%a,%a],[%a,%a,%a]]", m->transform[0][0], m->transform[0][1], m->transform[0][2],
					        m->transform[1][0], m->transform[1][1], m->transform[1][2], m->transform[2][0], m->transform[2][1], m->transform[2][2]);
				}
			}
			fputc('\n', fp);
		}

		if (i != pl->len - 1)
			fputc('\n', fp);
	}

	fflush(fp);
	if (fsync(fileno(fp)) != 0)
		warn("fsync:");
	fclose(fp);

	if (rename(tmp, path) != 0) {
		warn("rename to \"%s\":", path);
		return -1;
	}
	return 0;
}

int
profile_match(const Profile *saved, const Profile *cur)
{
	uint8_t match[64] = {0};

	if (saved->len != cur->len)
		return 0;

	if (cur->len > sizeof(match))
		return 0;

	for (size_t i = 0; i < saved->len; i++) {
		int found = 0;

		for (size_t j = 0; j < cur->len; j++) {
			if (!match[j] && saved->m[i].edid.hash == cur->m[j].edid.hash) {
				match[j] = 1;
				found = 1;
				break;
			}
		}

		if (!found)
			return 0;
	}

	return 1;
}

int
profile_layout_equal(const Profile *saved, const Profile *cur)
{
	uint8_t matched[64] = {0};

	if (saved->len != cur->len || cur->len > sizeof(matched))
		return 0;

	for (size_t i = 0; i < saved->len; i++) {
		const Monitor *s = &saved->m[i];
		const Monitor *c = NULL;

		for (size_t j = 0; j < cur->len; j++) {
			if (!matched[j] && cur->m[j].edid.hash == s->edid.hash) {
				matched[j] = 1;
				c = &cur->m[j];
				break;
			}
		}
		if (!c)
			return 0;

		if (s->enabled != c->enabled)
			return 0;
		if (!s->enabled)
			continue;   /* both disabled: geometry does not matter */

		if (s->primary != c->primary
		        || s->x != c->x || s->y != c->y
		        || s->w != c->w || s->h != c->h
		        || s->rotation != c->rotation
		        || s->pan_x != c->pan_x || s->pan_y != c->pan_y
		        || s->pan_w != c->pan_w || s->pan_h != c->pan_h)
			return 0;

		if (fabs(s->rate - c->rate) >= 0.01)
			return 0;

		if (s->has_transform != c->has_transform)
			return 0;
		if (s->has_transform)
			for (int a = 0; a < 3; a++)
				for (int b = 0; b < 3; b++)
					if (fabs(s->transform[a][b] - c->transform[a][b]) >= 1e-6)
						return 0;
	}

	return 1;
}

void
profile_config_dir(char *buf, size_t bufsz)
{
	config_path_to(buf, bufsz, "");
}
