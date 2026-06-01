/* See LICENSE file for copyright and license details. */

#ifndef XRANDR_H
#define XRANDR_H

#include "profile.h"

typedef enum {
	XR_CHANGED,
	XR_INTERRUPTED,
} XrEvent;

void     xr_init(void);
void     xr_free(void);
Profile *xr_active_profile(void);
void     xr_apply_profile(const Profile *p);
void     xr_watch_init(void);
XrEvent  xr_wait_for_change(int debounce_ms);

#endif /* XRANDR_H */
