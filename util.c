#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/render.h>
#include <xcb/xfixes.h>
#include <xcb/xcb_renderutil.h>
#include "xray.h"

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

xcb_render_picture_t get_alpha_picture(void)
{
	xcb_pixmap_t pid;
	xcb_render_picture_t pict;
	xcb_render_pictformat_t fmt;
	uint32_t mask;
	uint32_t vals[1];
	xcb_void_cookie_t ck;

	pid = xcb_generate_id(X);
	ck = xcb_create_pixmap_checked(X, 32, pid, root->id, 1, 1);
	if (check_cookie(ck)) {
		debug("get_alpha_picture: can't create pixmap\n");
	}
	pict = xcb_generate_id(X);
	fmt = xcb_render_util_find_standard_format(
					xcb_render_util_query_formats(X),
					XCB_PICT_STANDARD_ARGB_32)->id;
	mask = XCB_RENDER_CP_REPEAT;
	vals[0] = 1;
	ck = xcb_render_create_picture_checked(X, pict, pid, fmt, mask, vals);
	if (check_cookie(ck)) {
		debug("get_alpha_picture: can't create picture\n");
	}
	xcb_free_pixmap(X, pid);
	{
		uint8_t op;
		xcb_render_color_t c;
		xcb_rectangle_t r[1];

		op = XCB_RENDER_PICT_OP_SRC;
		c.red = c.green = c.blue = 0x0000;
		c.alpha = 0xcccc;
		r[0].x = r[0].y = 0;
		r[0].width = r[0].height = 1;
		xcb_render_fill_rectangles(X, op, pict, c, 1, r);
	}
	return pict;
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

