/* See LICENSE file for copyright and license details. */

#ifndef XRANDR_H
#define XRANDR_H

#include "profile.h"

void     xr_init(void);
void     xr_free(void);
Profile *xr_active_profile(void);
void     xr_apply_profile(const Profile *p);

#endif /* XRANDR_H */
