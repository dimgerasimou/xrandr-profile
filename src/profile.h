/* See LICENSE file for copyright and license details. */

#ifndef PROFILE_H
#define PROFILE_H

#include <stdlib.h>
#include <stdint.h>

typedef struct {
	char name[64];
	char serial[64];
	uint64_t hash;
} Edid;

typedef struct {
	Edid     edid;
	char     output[64];
	int32_t  x, y;
	uint16_t w, h;
	double   rate;
	int32_t  pan_x, pan_y;
	uint16_t pan_w, pan_h;
	uint8_t  rotation;
	uint8_t  primary;
	uint8_t  enabled;
	double   transform[3][3];
	uint8_t  has_transform;
} Monitor;

typedef struct {
	char     name[64];
	Monitor *m;
	size_t   len;
	size_t   cap;
} Profile;

typedef struct {
	Profile **p;
	size_t    len;
	size_t    cap;
} ProfileList;

Profile     *profile_create(const char *name);
void         profile_free(Profile *p);
void         profile_append(Profile *p);
void         profile_print(const Profile *p, const unsigned int names_only);

ProfileList *profile_list_create(void);
void         profile_list_free(ProfileList *pl);
void         profile_list_append(ProfileList *pl, Profile *p);
void         profile_list_prepend(ProfileList *pl, Profile *p);
void         profile_list_delete(ProfileList *pl, const char *name);
ProfileList *profile_list_read(void);
int          profile_list_write(const ProfileList *pl);

int          profile_match(const Profile *saved, const Profile *cur);
int          profile_layout_equal(const Profile *saved, const Profile *cur);
void         profile_config_dir(char *buf, size_t bufsz);

#endif /* PROFILE_H */
