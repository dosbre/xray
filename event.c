#include <stdio.h>
#include <stdlib.h>
#include <xcb/composite.h>
#include <xcb/xcb_renderutil.h>
#include "xray.h"

void create_notify(xcb_create_notify_event_t *e)
{
	xcb_rectangle_t geom;

	printf("create_noitfy: wid=%u\n", e->window);
	geom.x = e->x;
	geom.y = e->y;
	geom.width = e->width;
	geom.height = e->height;
	add_window(e->window, XCB_MAP_STATE_UNMAPPED, geom);
}

void destroy_notify(xcb_destroy_notify_event_t *e)
{
	struct window *win;

	printf("destroy_notify: wid=%u\n", e->window);
	win = find_window(e->window);
	if (!win) {
		debug("not on stack\n");
		return;
	}
	add_damaged_region(root, win->region);
	remove_window(win);
}

void unmap_notify(xcb_unmap_notify_event_t *e)
{
	struct window *win;

	printf("unmap_notify: wid=%u\n", e->window);
	win = find_window(e->window);
	if (!win) {
		debug("\tnot on stack\n");
		return;
	}
	win->map_state = XCB_MAP_STATE_UNMAPPED;
	xcb_free_pixmap(X, win->pixmap);
	win->pixmap = XCB_PIXMAP_NONE;
	xcb_render_free_picture(X, win->picture);
	win->picture = XCB_RENDER_PICTURE_NONE;
	add_damaged_region(root, win->region);
}

void map_notify(xcb_map_notify_event_t *e)
{
	struct window *win;

	printf("map_notify: wid=%u\n", e->window);
	win = find_window(e->window);
	if (!win) {
		printf("\tnot on stack\n");
		return;
	}
	win->map_state = XCB_MAP_STATE_VIEWABLE;
	win->pixmap = xcb_generate_id(X);
	xcb_composite_name_window_pixmap(X, win->id, win->pixmap);
	win->picture = xcb_generate_id(X);
	xcb_render_create_picture(X, win->picture, win->pixmap,
							pict_rgb_24, 0, NULL);
	add_damaged_region(root, win->region);
}

void reparent_notify(xcb_reparent_notify_event_t *e)
{
	printf("reparent_notify:\n");
	printf("\tevent=%u\n\twindow=%u\n\tparent=%u\n"
				"\toverride_redirect=%u\n", e->event,
				e->window, e->parent, e->override_redirect);
	if (e->parent == root->id) {
		xcb_rectangle_t geom;

		geom.x = e->x;
		geom.y = e->y;
		add_window(e->window, XCB_MAP_STATE_UNMAPPED, geom);
	} else {
		struct window *win;

		win = find_window(e->window);
		if (win)
			remove_window(win);
	}
}

void configure_notify(xcb_configure_notify_event_t *e)
{
	struct window *win;
	xcb_rectangle_t rect;
	uint8_t dmg;		/* "damaged" geometry */

	printf("configure_notify: wid=%u\n", e->window);
	win = find_window(e->window);
	if (!win) {
		printf("\tnot on stack\n");
		return;
	}
	dmg = 0;
	rect = win->geometry;
	if (e->x != rect.x || e->y != rect.y) {
		if (win->map_state != XCB_MAP_STATE_UNMAPPED) {
			if (e->x >= root->width || e->y >= root->height)
				win->map_state = XCB_MAP_STATE_UNVIEWABLE;
			else
				win->map_state = XCB_MAP_STATE_VIEWABLE;
		}
		dmg++;
	}
	if (e->width != rect.width || e->height != rect.height) {
		if (win->pixmap != XCB_PIXMAP_NONE) {
			xcb_free_pixmap(X, win->pixmap);
			win->pixmap = XCB_PIXMAP_NONE;
		}
		if (win->picture != XCB_RENDER_PICTURE_NONE) {
			xcb_render_free_picture(X, win->picture);
			win->picture = XCB_RENDER_PICTURE_NONE;
		}
		dmg++;
	}
	rect.x = e->x;
	rect.y = e->y;
	rect.width = e->width;
	rect.height = e->height;
	win->geometry = rect;
	if (dmg) {
		xcb_xfixes_region_t reg;
		xcb_xfixes_region_t old;
		uint8_t knd;

		old = win->region;
		reg = xcb_generate_id(X);
		knd = XCB_SHAPE_SK_BOUNDING;
		xcb_xfixes_create_region_from_window(X, reg, win->id, knd);
		xcb_xfixes_union_region(X, old, reg, old);
		add_damaged_region(root, old);
		xcb_xfixes_destroy_region(X, old);
		win->region = reg;
	}
	restack_window(win, e->above_sibling);
	printf("\tabove=%u\n", e->above_sibling);
	printf("\tgeometry=%2d %2d %3u %3u\n", e->x, e->y, e->width, e->height);
	printf("\tnext=%u\n", (win->next) ? win->next->id : 0);
}

void circulate_notify(xcb_circulate_notify_event_t *e)
{
	printf("circulate_notify: \n");
	printf("\tevent: %u\n", e->event);
	printf("\twindow: %u\n", e->window);
	printf("place: %hu\n", e->place);
}

void damage_notify(xcb_damage_notify_event_t *e)
{
	xcb_xfixes_region_t parts;

	/*
	debugf("damage_notify: %u\n", e->drawable);
	*/
	parts = xcb_generate_id(X);
	xcb_xfixes_create_region(X, parts, 0, NULL);
	xcb_damage_subtract(X, e->damage, XCB_NONE, parts);
	add_damaged_region(root, parts);
}

