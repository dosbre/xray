#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/damage.h>
#include <xcb/composite.h>
#include <xcb/xcb_renderutil.h>
#include "xray.h"

xcb_generic_error_t *error;

static const char *strerror[] = { "success", "request", "value", "window",
	"pixmap", "atom", "cursor", "font", "match", "drawable", "access",
	"alloc", "colormap", "gcontext", "idchoice", "name", "length",
	"implementation" };

int check_error(const char *s)
{
	int ret = 0;
	const char *se;

	if (error != NULL) {
		ret = error->error_code;
		se = (ret < LENGTH(strerror)) ? strerror[ret] : "unknow";
		debugf("%s: (%d) %s\n", s, ret, se);
		free(error);
	}
	return ret;
}

int check_cookie(xcb_void_cookie_t ck)
{
	xcb_generic_error_t *e;
	const char *s;
	int ret = 0;

	if ((e = xcb_request_check(X, ck)) != NULL) {
		ret = e->error_code;
		s = (ret < LENGTH(strerror)) ? strerror[ret] : "unknow";
		debugf("check_cookie: %d %s\n", ret, s);
		free(e);
	}
	return ret;
}

xcb_pixmap_t update_pixmap(struct window *win)
{
	printf("update_pixmap: wid=%u\n", win->id);
	if (win->pixmap != XCB_PIXMAP_NONE)
		xcb_free_pixmap(X, win->pixmap);
	win->pixmap = xcb_generate_id(X);
	xcb_composite_name_window_pixmap(X, win->id, win->pixmap);
	return win->pixmap;
}

xcb_render_picture_t update_picture(struct window *win)
{
	printf("update_picture: wid=%u\n", win->id);
	if (win->picture != XCB_RENDER_PICTURE_NONE)
		xcb_render_free_picture(X, win->picture);
	win->picture = xcb_generate_id(X);
	xcb_render_create_picture(X, win->picture, win->pixmap,
							pict_rgb_24, 0, NULL);
	return win->picture;
}

xcb_render_picture_t get_alpha_picture(unsigned opacity)
{
	xcb_pixmap_t pid;
	xcb_render_picture_t pict;
	xcb_render_pictforminfo_t *pi;
	uint32_t mask;
	uint32_t vals[1];

	pid = xcb_generate_id(X);
	xcb_create_pixmap(X, 8, pid, root->id, 1, 1);
	pict = xcb_generate_id(X);
	pi = xcb_render_util_find_standard_format(
					xcb_render_util_query_formats(X),
							XCB_PICT_STANDARD_A_8);
	mask = XCB_RENDER_CP_REPEAT;
	vals[0] = 1;
	xcb_render_create_picture(X, pict, pid, pi->id, mask, vals);
	xcb_free_pixmap(X, pid);
	{
		xcb_render_color_t c;
		xcb_rectangle_t r = { 0, 0, 1, 1 };

		c.red = c.green = c.blue = 0x0000;
		opacity = (double) opacity / OPAQUE;
		c.alpha = /*opacity */ 0xcccc;
		xcb_render_fill_rectangles(X, XCB_RENDER_PICT_OP_SRC,
								pict, c, 1, &r);
	}
	return pict;
}

unsigned get_opacity_property(xcb_window_t wid)
{
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *r;
	unsigned val = OPAQUE;

	ck = xcb_get_property_unchecked(X, 0, wid, get_opacity_atom(),
						XCB_ATOM_CARDINAL, 0L, 1L);
	if ((r = xcb_get_property_reply(X, ck, NULL)) != NULL) {
		val = *((unsigned *) xcb_get_property_value(r));
		free(r);
	}
	debugf("get_opacity_property: wid=%u val=%u\n", wid, val);
	return val;
}

void debug_region(xcb_xfixes_region_t reg)
{
	xcb_xfixes_fetch_region_cookie_t ck;
	xcb_xfixes_fetch_region_reply_t *r;
	xcb_rectangle_t *rect;
	int i;

	ck = xcb_xfixes_fetch_region(X, reg);
	r = xcb_xfixes_fetch_region_reply(X, ck, &error);
	if (error) {
		debugf("debug_region: error %hu\n", error->error_code);
		free(error);
		return;
	}
	rect = xcb_xfixes_fetch_region_rectangles(r);
	for (i = 0; i < r->length; ++i) {
		debugf("region %d: %hu %hu %hd %hd\n", i, rect->x,
					rect->y, rect->width, rect->height);
	}
	*rect = r->extents;
	debugf("region %d: %hu %hu %hd %hd\n", i, rect->x,
				rect->y, rect->width, rect->height);
	free(r);
}

