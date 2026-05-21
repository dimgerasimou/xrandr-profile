/* See LICENSE file for copyright and license details. */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xrender.h>

#include "xrandr.h"
#include "profile.h"
#include "utils.h"

static Display *dpy;
static XRRScreenResources *res;

static double
refresh_rate(const XRRModeInfo *m)
{
	if (!m->hTotal || !m->vTotal)
		return 0.0;

	double vtotal = m->vTotal;

	if (m->modeFlags & RR_DoubleScan)
		vtotal *= 2;
	if (m->modeFlags & RR_Interlace)
		vtotal /= 2;

	return m->dotClock / (m->hTotal * vtotal);
}

static uint64_t
fnv1a(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint64_t h = 14695981039346656037ULL;

	for (size_t i = 0; i < len; i++) {
		h ^= p[i];
		h *= 1099511628211ULL;
	}

	return h;
}

static int
get_edid(Display *dpy, const RROutput output, Monitor *m)
{
	Atom edid_atom;
	Atom actual_type;
	int actual_format, rc;
	unsigned long np, bytes_after;
	unsigned char *data = NULL;
	char serial[14] = {0}, name[14] = {0};

	edid_atom = XInternAtom(dpy, "EDID", False);
	rc = XRRGetOutputProperty(dpy, output, edid_atom, 0, 256, False, False, AnyPropertyType,
	                          &actual_type, &actual_format, &np, &bytes_after, &data);

	if (rc != Success || np < 128) {
		if (data)
			XFree(data);
		return -1;
	}

	/* Descriptor blocks: prefer serial (0xFF) over name (0xFC) */
	for (int i = 0; i < 4; i++) {
		char *dst = NULL;
		const unsigned char *d = data + 54 + i * 18;

		if (d[0] || d[1] || d[2])
			continue;

		if (d[3] == 0xFF && !serial[0])
			dst = serial;
		else if (d[3] == 0xFC && !name[0])
			dst = name;
		else
			continue;

		snprintf(dst, 14, "%.13s", (const char *)(d + 5));
		for (int j = strlen(dst) - 1; j >= 0 && dst[j] <= ' '; j--)
			dst[j] = '\0';
	}

	m->edid.hash = fnv1a(data, np);

	if (serial[0])
		snprintf(m->edid.serial, sizeof(m->edid.serial), "%s", serial);

	if (name[0])
		snprintf(m->edid.name, sizeof(m->edid.name), "%s", name);

	XFree(data);
	return 0;
}

static void
get_transform(Display *dpy, RRCrtc crtc, Monitor *m)
{
	XRRCrtcTransformAttributes *ta;

	if (!XRRGetCrtcTransform(dpy, crtc, &ta) || !ta)
		return;

	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			m->transform[i][j] = XFixedToDouble(ta->currentTransform.matrix[i][j]);

	m->has_transform = 0;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			double expected = (i == j) ? 1.0 : 0.0;
			if (fabs(m->transform[i][j] - expected) > 1e-6) {
				m->has_transform = 1;
				goto done;
			}
		}
	}
done:

	XFree(ta);
}

static RRMode
find_mode(XRRScreenResources *r, XRROutputInfo *info,
          uint16_t w, uint16_t h, double rate)
{
	RRMode best      = None;
	double best_diff = 1e9;
 
	for (int i = 0; i < info->nmode; i++) {
		for (int j = 0; j < r->nmode; j++) {
			const XRRModeInfo *mode = &r->modes[j];
 
			if (mode->id != info->modes[i])
				continue;
			if (mode->width != w || mode->height != h)
				continue;
 
			double diff = fabs(refresh_rate(mode) - rate);
			if (diff < best_diff) {
				best_diff = diff;
				best      = mode->id;
			}
		}
	}
 
	return best;
}
 
static RRCrtc
find_crtc(XRROutputInfo *info, const RRCrtc *used, int nused)
{
	for (int i = 0; i < info->ncrtc; i++) {
		RRCrtc crtc  = info->crtcs[i];
		int    taken = 0;
 
		for (int j = 0; j < nused; j++)
			if (used[j] == crtc) { taken = 1; break; }
 
		if (!taken)
			return crtc;
	}
 
	return None;
}

void
xr_init(void)
{
	Window root;

	dpy = XOpenDisplay(NULL);

	if (dpy == NULL)
		die("Can't open X display");

	root = DefaultRootWindow(dpy);
	res = XRRGetScreenResourcesCurrent(dpy, root);

	if (res == NULL)
		die("Can't get Current Screen Resources");
}

void
xr_free(void)
{
	XRRFreeScreenResources(res);
	XCloseDisplay(dpy);
}

Profile *
xr_active_profile(void)
{
	Profile *p;
	RROutput primary_output;

	p = profile_create("Active Profile");
	primary_output = XRRGetOutputPrimary(dpy, DefaultRootWindow(dpy));

	for (int i = 0; i < res->noutput; i++) {
		XRROutputInfo *info;

		info = XRRGetOutputInfo(dpy, res, res->outputs[i]);
		if (!info)
			continue;

		if (info->connection == RR_Connected) {
			Monitor *m;

			profile_append(p);
			m = &p->m[p->len - 1];

			snprintf(m->output, sizeof(m->output), "%s", info->name);
			get_edid(dpy, res->outputs[i], m);
			m->enabled = 0;

			m->primary = (res->outputs[i] == primary_output);

			if (info->crtc) {
				XRRCrtcInfo *crtc;
				XRRPanning *pan;

				m->enabled = 1;
				crtc = XRRGetCrtcInfo(dpy, res, info->crtc);
				pan = XRRGetPanning(dpy, res, info->crtc);
				get_transform(dpy, info->crtc, m);
				if (crtc) {
					m->x        = crtc->x;
					m->y        = crtc->y;
					m->rotation = crtc->rotation;


					for (int j = 0; j < res->nmode; j++) {
						const XRRModeInfo *mode = &res->modes[j];

						if (mode->id != crtc->mode)
							continue;

						m->w    = mode->width;
						m->h    = mode->height;
						m->rate = refresh_rate(mode);
						break;
					}

					XRRFreeCrtcInfo(crtc);
				}
				if (pan) {
					m->pan_x = pan->left;
					m->pan_y = pan->top;
					m->pan_w = pan->width;
					m->pan_h = pan->height;

					XRRFreePanning(pan);
				}
			}
		}

		XRRFreeOutputInfo(info);
	}
	return p;
}


void
xr_apply_profile(const Profile *p)
{
	XRRScreenResources *r;
	Window              root;
	RROutput            primary = None;
	RRCrtc              used_crtcs[64];
	int                 nused = 0;
 
	root = DefaultRootWindow(dpy);
	r    = XRRGetScreenResources(dpy, root);
	if (!r)
		die("Can't get screen resources");
 
	/*
	 * Step 1: disable all active CRTCs.
	 * Must happen before resizing the screen, and prevents "CRTC too large"
	 * errors when the new layout is smaller than the current one.
	 */
	for (int i = 0; i < r->ncrtc; i++) {
		XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, r, r->crtcs[i]);
		if (!crtc)
			continue;
		if (crtc->mode != None)
			XRRSetCrtcConfig(dpy, r, r->crtcs[i], CurrentTime,
			                 0, 0, None, RR_Rotate_0, NULL, 0);
		XRRFreeCrtcInfo(crtc);
	}
 
	/*
	 * Step 2: compute the new screen bounding box.
	 * Rotation swaps w/h; transform (scale) divides the framebuffer size.
	 */
	int new_w = 0, new_h = 0;
 
	for (size_t i = 0; i < p->len; i++) {
		const Monitor *m = &p->m[i];
 
		if (!m->enabled)
			continue;
 
		int rot  = m->rotation & 0xf;
		int fb_w = (rot == RR_Rotate_90 || rot == RR_Rotate_270) ? m->h : m->w;
		int fb_h = (rot == RR_Rotate_90 || rot == RR_Rotate_270) ? m->w : m->h;
 
		if (m->has_transform
		        && m->transform[0][0] > 1e-9
		        && m->transform[1][1] > 1e-9) {
			fb_w = (int)ceil((double)fb_w / m->transform[0][0]);
			fb_h = (int)ceil((double)fb_h / m->transform[1][1]);
		}
 
		int right  = m->x + fb_w;
		int bottom = m->y + fb_h;
		if (right  > new_w) new_w = right;
		if (bottom > new_h) new_h = bottom;
	}
 
	if (new_w < 1) new_w = 1;
	if (new_h < 1) new_h = 1;
 
	/*
	 * Step 3: resize the screen.
	 * Derive mm dimensions from current DPI to keep physical size consistent.
	 */
	Screen *scr   = DefaultScreenOfDisplay(dpy);
	double  dpi_x = (double)scr->width  / scr->mwidth;
	double  dpi_y = (double)scr->height / scr->mheight;
	int     mm_w  = (int)((double)new_w / dpi_x);
	int     mm_h  = (int)((double)new_h / dpi_y);
	if (mm_w < 1) mm_w = 1;
	if (mm_h < 1) mm_h = 1;
 
	XRRSetScreenSize(dpy, root, new_w, new_h, mm_w, mm_h);
 
	/*
	 * Step 4: configure each monitor in the profile.
	 */
	for (size_t i = 0; i < p->len; i++) {
		const Monitor *m = &p->m[i];
 
		/* find output by EDID hash */
		RROutput       output = None;
		XRROutputInfo *info   = NULL;
 
		for (int j = 0; j < r->noutput; j++) {
			XRROutputInfo *oi = XRRGetOutputInfo(dpy, r, r->outputs[j]);
			if (!oi)
				continue;
			if (oi->connection != RR_Connected) {
				XRRFreeOutputInfo(oi);
				continue;
			}
 
			Monitor tmp = {0};
			get_edid(dpy, r->outputs[j], &tmp);
 
			if (tmp.edid.hash == m->edid.hash) {
				output = r->outputs[j];
				info   = oi;
				break;
			}
 
			XRRFreeOutputInfo(oi);
		}
 
		if (!info) {
			warn("No output found for monitor \"%s\" (hash=%" PRIu64 ")",
			     m->edid.name, m->edid.hash);
			continue;
		}
 
		if (!m->enabled) {
			XRRFreeOutputInfo(info);
			continue;
		}
 
		RRMode mode_id = find_mode(r, info, m->w, m->h, m->rate);
		if (mode_id == None) {
			warn("No mode %ux%u@%.2f for monitor \"%s\"",
			     m->w, m->h, m->rate, m->edid.name);
			XRRFreeOutputInfo(info);
			continue;
		}
 
		RRCrtc crtc = find_crtc(info, used_crtcs, nused);
		if (crtc == None) {
			warn("No CRTC available for monitor \"%s\"", m->edid.name);
			XRRFreeOutputInfo(info);
			continue;
		}
		used_crtcs[nused++] = crtc;
 
		Status st = XRRSetCrtcConfig(dpy, r, crtc, CurrentTime,
		                             m->x, m->y, mode_id, m->rotation,
		                             &output, 1);
		if (st != RRSetConfigSuccess) {
			warn("XRRSetCrtcConfig failed for monitor \"%s\"", m->edid.name);
			XRRFreeOutputInfo(info);
			continue;
		}
 
		/*
		 * Apply or reset transform.
		 * Always call XRRSetCrtcTransform so a previously applied transform
		 * from another profile doesn't persist.
		 */
		XTransform xf;
 
		if (m->has_transform) {
			for (int ri = 0; ri < 3; ri++)
				for (int ci = 0; ci < 3; ci++)
					xf.matrix[ri][ci] = XDoubleToFixed(m->transform[ri][ci]);
		} else {
			xf = (XTransform){{
				{ XDoubleToFixed(1), 0, 0 },
				{ 0, XDoubleToFixed(1), 0 },
				{ 0, 0, XDoubleToFixed(1) },
			}};
		}
 
		XRRSetCrtcTransform(dpy, crtc, &xf, "bilinear", NULL, 0);
 
		if (m->primary)
			primary = output;
 
		XRRFreeOutputInfo(info);
	}
 
	/* Step 5: set primary output */
	if (primary != None)
		XRRSetOutputPrimary(dpy, root, primary);
 
	XRRFreeScreenResources(r);
}
