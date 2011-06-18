#include <stdio.h>
#include <stdlib.h>
#include <xcb/composite.h>
#include <xcb/xcb_renderutil.h>
#include "xray.h"

void create_notify(xcb_create_notify_event_t *e)
{
	struct window *win;

	debugf("create_noitfy: wid=%u\n", e->window);
	if ((win = add_window(&root->window_list, e->window))) {
		win->x = e->x;
		win->y = e->y;
		win->width = e->width;
		win->height = e->height;
		win->border_width = e->border_width;
		win->override_redirect = e->override_redirect;
		xcb_damage_create(X, win->damage, win->id,
					XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
		xcb_xfixes_create_region_from_window(X, win->region, win->id,
							XCB_SHAPE_SK_BOUNDING);
	}
}

void destroy_notify(xcb_destroy_notify_event_t *e)
{
	struct window *win;

	printf("destroy_notify: wid=%u\n", e->window);
	if ((win = find_window(root->window_list, e->window)) != NULL) {
		add_damaged_region(root, win->region);
		remove_window(&root->window_list, win);
	}
}

void unmap_notify(xcb_unmap_notify_event_t *e)
{
	struct window *win;

	debugf("unmap_notify: wid=%u\n", e->window);
	if ((win = find_window(root->window_list, e->window))) {
		xcb_free_pixmap(X, win->pixmap);
		xcb_render_free_picture(X, win->picture);
		win->map_state = XCB_MAP_STATE_UNMAPPED;
		win->pixmap = XCB_PIXMAP_NONE;
		win->picture = XCB_RENDER_PICTURE_NONE;
		add_damaged_region(root, win->region);
	}
}

void map_notify(xcb_map_notify_event_t *e)
{
	struct window *win;

	printf("map_notify: wid=%u\n", e->window);
	if ((win = find_window(root->window_list, e->window))) {
		win->map_state = XCB_MAP_STATE_VIEWABLE;
		win->pixmap = xcb_generate_id(X);
		win->picture = xcb_generate_id(X);
		xcb_composite_name_window_pixmap(X, win->id, win->pixmap);
		xcb_render_create_picture(X, win->picture, win->pixmap,
							pict_rgb_24, 0, NULL);
		add_damaged_region(root, win->region);
	}
}

void reparent_notify(xcb_reparent_notify_event_t *e)
{
	struct window *win;

	printf("reparent_notify:\n");
	printf("\tevent=%u\n\twindow=%u\n\tparent=%u\n"
				"\toverride_redirect=%u\n", e->event,
				e->window, e->parent, e->override_redirect);
	if (e->parent == root->id) {
		xcb_get_geometry_reply_t *r;

		win = add_window(&root->window_list, e->window);
		win->x = e->x;
		win->y = e->y;
		win->map_state = XCB_MAP_STATE_UNMAPPED;
		win->override_redirect = e->override_redirect;
		r = xcb_get_geometry_reply(X,
				xcb_get_geometry_unchecked(X, win->id), NULL);
		GEOMCPY(win, r);
	} else if ((win = find_window(root->window_list, e->window))) {
		remove_window(&root->window_list, win);
	}
}

void configure_notify(xcb_configure_notify_event_t *e)
{
	struct window *win;
	uint8_t dmg = 0;	/* "damaged" geometry */

	if ((win = find_window(root->window_list, e->window)) == NULL)
		return;
	debugf("configure_notify:\n\tevent=%u window=%u "
			"above_sibling=%u\n\tx=%hd y=%hd width=%hu height=%hu "
			"border_width=%hu\n\toverride_redirect=%hu\n",
			e->event, e->window, e->above_sibling, e->x,
			e->y, e->width, e->height, e->border_width,
			e->override_redirect);
	debugf("\t->next=%u\n", (win->next) ? win->next->id : 0);
	if (win->map_state != XCB_MAP_STATE_UNMAPPED) {
		if (win->x != e->x || win->y != e->y ||
			win->width != e->width || win->height != e->height ||
			win->border_width != e->border_width) {
			if (OFFSCREEN(e, root))
				win->map_state = XCB_MAP_STATE_UNVIEWABLE;
			else
				win->map_state = XCB_MAP_STATE_VIEWABLE;
		}
		dmg++;
	}
	if (win->width != e->width || win->height != win->height) {
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
	GEOMCPY(win, e);
	if (dmg) {
		xcb_xfixes_region_t reg;
		xcb_xfixes_region_t old;

		old = win->region;
		reg = xcb_generate_id(X);
		xcb_xfixes_create_region_from_window(X, reg, win->id,
							XCB_SHAPE_SK_BOUNDING);
		xcb_xfixes_union_region(X, old, reg, old);
		add_damaged_region(root, old);
		xcb_xfixes_destroy_region(X, old);
		win->region = reg;
	}
	if (e->above_sibling)
		restack_window(&root->window_list, win, e->above_sibling);
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

