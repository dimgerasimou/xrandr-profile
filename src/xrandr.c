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

typedef struct {
	RROutput       output;
	XRROutputInfo *info;
	uint64_t       hash;
	int            used;
} OutCache;

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

	return (double) m->dotClock / (m->hTotal * vtotal);
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
get_edid(const RROutput output, Monitor *m)
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
		for (int j = (int)strlen(dst) - 1; j >= 0 && dst[j] <= ' '; j--)
			dst[j] = '\0';
	}

	m->edid.hash = fnv1a(data, 128);

	if (serial[0])
		snprintf(m->edid.serial, sizeof(m->edid.serial), "%s", serial);

	if (name[0])
		snprintf(m->edid.name, sizeof(m->edid.name), "%s", name);

	XFree(data);
	return 0;
}

static void
get_transform(RRCrtc crtc, Monitor *m)
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

static int
build_output_cache(XRRScreenResources *r, OutCache *cache, int max)
{
	int n = 0;

	for (int j = 0; j < r->noutput && n < max; j++) {
		XRROutputInfo *oi = XRRGetOutputInfo(dpy, r, r->outputs[j]);
		if (!oi)
			continue;
		if (oi->connection != RR_Connected) {
			XRRFreeOutputInfo(oi);
			continue;
		}

		Monitor tmp = {0};
		get_edid(r->outputs[j], &tmp);

		cache[n].output = r->outputs[j];
		cache[n].info   = oi;
		cache[n].hash   = tmp.edid.hash;
		cache[n].used   = 0;
		n++;
	}

	return n;
}

static void
disable_all_crtcs(XRRScreenResources *r)
{
	for (int i = 0; i < r->ncrtc; i++) {
		XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, r, r->crtcs[i]);
		if (!crtc)
			continue;
		if (crtc->mode != None)
			XRRSetCrtcConfig(dpy, r, r->crtcs[i], CurrentTime,
			                 0, 0, None, RR_Rotate_0, NULL, 0);
		XRRFreeCrtcInfo(crtc);
	}
}

static void
compute_framebuffer(const Profile *p, int *out_w, int *out_h)
{
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
			fb_w = (int)ceil((double)fb_w * m->transform[0][0]);
			fb_h = (int)ceil((double)fb_h * m->transform[1][1]);
		}

		int right  = m->x + fb_w;
		int bottom = m->y + fb_h;

		if (m->pan_w && m->pan_h) {
			if (m->pan_x + (int)m->pan_w > right)
				right = m->pan_x + (int)m->pan_w;
			if (m->pan_y + (int)m->pan_h > bottom)
				bottom = m->pan_y + (int)m->pan_h;
		}

		if (right  > new_w) new_w = right;
		if (bottom > new_h) new_h = bottom;
	}

	*out_w = new_w < 1 ? 1 : new_w;
	*out_h = new_h < 1 ? 1 : new_h;
}

static RROutput
apply_monitor(XRRScreenResources *r, const Monitor *m, OutCache *cache,
              int ncache, RRCrtc *used, int *nused, int maxused)
{
	XRROutputInfo *info   = NULL;
	RROutput       output = None;

	for (int j = 0; j < ncache; j++) {
		if (!cache[j].used && cache[j].hash == m->edid.hash) {
			output        = cache[j].output;
			info          = cache[j].info;
			cache[j].used = 1;
			break;
		}
	}

	if (!info) {
		warn("No output found for monitor \"%s\" (hash=%" PRIu64 ")",
		     m->edid.name, m->edid.hash);
		return None;
	}

	RRMode mode_id = find_mode(r, info, m->w, m->h, m->rate);
	if (mode_id == None) {
		warn("No mode %ux%u@%.2f for monitor \"%s\"",
		     m->w, m->h, m->rate, m->edid.name);
		return None;
	}

	if (*nused >= maxused) {
		warn("Too many active CRTCs, skipping monitor \"%s\"", m->edid.name);
		return None;
	}

	RRCrtc crtc = find_crtc(info, used, *nused);
	if (crtc == None) {
		warn("No CRTC available for monitor \"%s\"", m->edid.name);
		return None;
	}
	used[(*nused)++] = crtc;

	if (XRRSetCrtcConfig(dpy, r, crtc, CurrentTime, m->x, m->y,
	                     mode_id, m->rotation, &output, 1) != RRSetConfigSuccess) {
		warn("XRRSetCrtcConfig failed for monitor \"%s\"", m->edid.name);
		return None;
	}

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

	if (m->pan_w && m->pan_h) {
		XRRPanning pan = {0};
		pan.timestamp = CurrentTime;
		pan.left   = (unsigned int)m->pan_x;
		pan.top    = (unsigned int)m->pan_y;
		pan.width  = m->pan_w;
		pan.height = m->pan_h;
		if (XRRSetPanning(dpy, r, crtc, &pan) != RRSetConfigSuccess)
			warn("XRRSetPanning failed for monitor \"%s\"", m->edid.name);
	}

	return output;
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
			get_edid(res->outputs[i], m);
			m->enabled = 0;

			m->primary = (res->outputs[i] == primary_output);

			if (info->crtc) {
				XRRCrtcInfo *crtc;
				XRRPanning *pan;

				m->enabled = 1;
				crtc = XRRGetCrtcInfo(dpy, res, info->crtc);
				pan = XRRGetPanning(dpy, res, info->crtc);
				get_transform(info->crtc, m);
				if (crtc) {
					m->x        = crtc->x;
					m->y        = crtc->y;
					m->rotation = (uint8_t)crtc->rotation;


					for (int j = 0; j < res->nmode; j++) {
						const XRRModeInfo *mode = &res->modes[j];

						if (mode->id != crtc->mode)
							continue;

						m->w    = (uint16_t)mode->width;
						m->h    = (uint16_t)mode->height;
						m->rate = refresh_rate(mode);
						break;
					}

					XRRFreeCrtcInfo(crtc);
				}
				if (pan) {
					m->pan_x = (int32_t)pan->left;
					m->pan_y = (int32_t)pan->top;
					m->pan_w = (uint16_t)pan->width;
					m->pan_h = (uint16_t)pan->height;

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
	enum { MAXOUT = 64, MAXCRTC = 64 };
	XRRScreenResources *r;
	Window              root;
	RROutput            primary = None;
	RRCrtc              used_crtcs[MAXCRTC];
	int                 nused = 0;
	OutCache            cache[MAXOUT];
	int                 ncache;
	int                 new_w, new_h;

	root = DefaultRootWindow(dpy);

	/* Poll harware fresh */
	r = XRRGetScreenResources(dpy, root);
	if (!r)
		die("Can't get screen resources");

	ncache = build_output_cache(r, cache, MAXOUT);
	disable_all_crtcs(r);
	compute_framebuffer(p, &new_w, &new_h);

	Screen *scr   = DefaultScreenOfDisplay(dpy);
	double  dpi_x = (double)scr->width  / scr->mwidth;
	double  dpi_y = (double)scr->height / scr->mheight;
	int     mm_w  = (int)((double)new_w / dpi_x);
	int     mm_h  = (int)((double)new_h / dpi_y);
	if (mm_w < 1) mm_w = 1;
	if (mm_h < 1) mm_h = 1;

	XRRSetScreenSize(dpy, root, new_w, new_h, mm_w, mm_h);

	for (size_t i = 0; i < p->len; i++) {
		const Monitor *m = &p->m[i];

		if (!m->enabled)
			continue;

		RROutput out = apply_monitor(r, m, cache, ncache, used_crtcs, &nused, MAXCRTC);
		if (out != None && m->primary)
			primary = out;
	}

	if (primary != None)
		XRRSetOutputPrimary(dpy, root, primary);

	for (int j = 0; j < ncache; j++)
		XRRFreeOutputInfo(cache[j].info);

	XRRFreeScreenResources(r);
}
