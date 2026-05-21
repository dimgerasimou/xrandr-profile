/* See LICENSE file for copyright and license details. */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <linux/limits.h>

#include "profile.h"
#include "utils.h"

static const char *direction[5] = { "normal", "left", "inverted", "right", "\n" };

static const char *
rotation_name(uint8_t rotation)
{
	if ((rotation & 0xf) == 0)
		return "normal";
	for (int i = 0; i < 4; i++)
		if (rotation & (1 << i))
			return direction[i];
	return "invalid rotation";
}

static const char *
reflection_name (uint8_t rotation)
{
	rotation &= (16|32);
	switch (rotation) {
	case 0:
		return "none";
	case 16:
		return "X axis";
	case 32:
		return "Y axis";
	case 16|32:
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

		if (mkdir(dir, 0755) != 0 && errno != EEXIST)
			die("mkdir(%s) failed:", dir);

		*p = '/';
	}

	if (mkdir(dir, 0755) != 0 && errno != EEXIST)
		die("mkdir(%s) failed:", dir);

	return 0;
}

static void
config_path(char *buf, const size_t bufsz)
{
	const char *xdg  = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");

	if (xdg && xdg[0])
		snprintf(buf, bufsz, "%s/xrandr-profile/profiles", xdg);
	else if (home && home[0])
		snprintf(buf, bufsz, "%s/.config/xrandr-profile/profiles", home);
	else
		die("\"HOME\" is not set");
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

		/* read key */
		char key[32];
		size_t ki = 0;

		while (*line && *line != '=' && ki < sizeof(key) - 1)
			key[ki++] = *line++;
		key[ki] = '\0';

		if (*line++ != '=')
			return -1;  /* skip '=' */

		/* read value: quoted or bare */
		char val[256];
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

		/* dispatch */
		char *ep;

		if (!strcmp(key, "hash")) {
			m->edid.hash = (uint64_t) strtoull(val, &ep, 10);
		} else if (!strcmp(key, "name")) {
			strncpy(m->edid.name,   val, sizeof(m->edid.name)   - 1);
		} else if (!strcmp(key, "serial")) {
			strncpy(m->edid.serial, val, sizeof(m->edid.serial) - 1);
		} else if (!strcmp(key, "primary")) {
			m->primary = (uint8_t) strtol(val, &ep, 10);
		} else if (!strcmp(key, "enabled")) {
			m->enabled = (uint8_t) strtol(val, &ep, 10);
		} else if (!strcmp(key, "w")) {
			m->w = (uint16_t) strtol(val, &ep, 10);
		} else if (!strcmp(key, "h")) {
			m->h = (uint16_t) strtol(val, &ep, 10);
		} else if (!strcmp(key, "rate")) {
			m->rate = strtod(val, &ep);
		} else if (!strcmp(key, "x")) {
			m->x = (int32_t)  strtol(val, &ep, 10);
		} else if (!strcmp(key, "y")) {
			m->y = (int32_t)  strtol(val, &ep, 10);
		} else if (!strcmp(key, "pan_x")) {
			m->pan_x = (int32_t)  strtol(val, &ep, 10);
		} else if (!strcmp(key, "pan_y")) {
			m->pan_y = (int32_t)  strtol(val, &ep, 10);
		} else if (!strcmp(key, "pan_w")) {
			m->pan_w = (uint16_t) strtol(val, &ep, 10);
		} else if (!strcmp(key, "pan_h")) {
			m->pan_h = (uint16_t) strtol(val, &ep, 10);
		} else if (!strcmp(key, "rotation")) {
			m->rotation = (uint8_t)  strtol(val, &ep, 10);
		} else if (!strcmp(key, "transform")) {
			sscanf(val, "[[%la,%la,%la],[%la,%la,%la],[%la,%la,%la]]",
			             &m->transform[0][0], &m->transform[0][1], &m->transform[0][2],
			             &m->transform[1][0], &m->transform[1][1], &m->transform[1][2],
			             &m->transform[2][0], &m->transform[2][1], &m->transform[2][2]);
			m->has_transform = 1;
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
profile_print(const Profile *p)
{
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

void
profile_list_append(ProfileList *pl, Profile *p)
{
	if (pl->len == pl->cap) {
		size_t newcap;
		Profile **tmp;

		newcap = pl->cap ? pl->cap * 2 : 4;
		tmp = erealloc(pl->p, newcap * sizeof(Profile *));
		pl->p = tmp;
		pl->cap = newcap;
	}

	pl->p[pl->len] = p;
	pl->len++;
}

void
profile_list_prepend(ProfileList *pl, Profile *p)
{
	if (pl->len == pl->cap) {
		size_t newcap;
		Profile **tmp;

		newcap = pl->cap ? pl->cap * 2 : 4;
		tmp = erealloc(pl->p, newcap * sizeof(Profile *));
		pl->p = tmp;
		pl->cap = newcap;
	}

	pl->len++;
	for (size_t i = pl->len - 1 ; i > 0; i--)
		pl->p[i] = pl->p[i-1];

	pl->p[0] = p;
}

void
profile_list_delete(ProfileList *pl, char *name)
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

			if (parse_monitor_line(line, &cur->m[cur->len - 1]) < 0) {
				warn("Malformed monitor line: %s", line);
				cur->len--;
			}
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
	make_directory(path);

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
			Monitor *m = &p->m[j];

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

	fclose(fp);
	rename(tmp, path);
	return 0;
}

int
profile_match(const Profile *saved, const Profile *cur)
{
	uint8_t *match;

	if (saved->len != cur->len)
		return 0;

	match = ecalloc(cur->len, sizeof(uint8_t));

	for (size_t i = 0; i < saved->len; i++) {
		int found = 0;

		for (size_t j = 0; j < cur->len; j++) {
			if (!match[j] && saved->m[i].edid.hash == cur->m[j].edid.hash) {
				match[j] = 1;
				found = 1;
				break;
			}
		}

		if (!found) {
			free(match);
			return 0;
		}
	}

	free(match);
	return 1;
}
