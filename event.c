#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/damage.h>
#include "xray.h"

void create_notify(xcb_create_notify_event_t *e)
{
	struct window *win;

	debugf("CreateNotify: wid=%u\n", e->window);
	if ((win = add_window(&root->window_list, e->window))) {
		xcb_get_window_attributes_reply_t *ar;

		ar = xcb_get_window_attributes_reply(X,
				xcb_get_window_attributes_unchecked(X,
								win->id), NULL);
		init_window(win, NULL, ar);
		win->x = e->x;
		win->y = e->y;
		win->width = e->width;
		win->height = e->height;
		win->border_width = e->border_width;
		if (ar)
			free(ar);
	}
}

void destroy_notify(xcb_destroy_notify_event_t *e)
{
	struct window *win;

	debugf("DestroyNotify: wid=%u\n", e->window);
	if ((win = find_window(root->window_list, e->window))) {
		add_damaged_region(root, win->region);
		xcb_damage_destroy(X, win->damage);
		xcb_xfixes_destroy_region(X, win->region);
		remove_window(&root->window_list, win);
	}
}

void unmap_notify(xcb_unmap_notify_event_t *e)
{
	struct window *win;

	debugf("UnmapNotify: wid=%u\n", e->window);
	if ((win = find_window(root->window_list, e->window))) {
		if (win->pixmap != XCB_PIXMAP_NONE) {
			xcb_free_pixmap(X, win->pixmap);
			win->pixmap = XCB_PIXMAP_NONE;
		}
		if (win->picture != XCB_RENDER_PICTURE_NONE) {
			xcb_render_free_picture(X, win->picture);
			win->picture = XCB_RENDER_PICTURE_NONE;
		}
		win->map_state = XCB_MAP_STATE_UNMAPPED;
		add_damaged_region(root, win->region);
	}
}

void map_notify(xcb_map_notify_event_t *e)
{
	struct window *win;

	debugf("MapNotify: wid=%u\n", e->window);
	if ((win = find_window(root->window_list, e->window))) {
		win->map_state = XCB_MAP_STATE_VIEWABLE;
		win->pixmap = xcb_generate_id(X);
		xcb_composite_name_window_pixmap(X, win->id, win->pixmap);
		/*
		win->picture = xcb_generate_id(X);
		xcb_render_create_picture(X, win->picture, win->pixmap,
							pict_rgb_24, 0, NULL);
		*/
		win->picture = get_picture(win->pixmap, win->visual);
		add_damaged_region(root, win->region);
	}
}

void reparent_notify(xcb_reparent_notify_event_t *e)
{
	struct window *win;

	printf("REPARENT_NOTIFY:\n");
	printf("\tevent=%u\n\twindow=%u\n\tparent=%u\n"
				"\toverride_redirect=%u\n", e->event,
				e->window, e->parent, e->override_redirect);
	if (e->parent == root->id) {
		xcb_get_window_attributes_reply_t *ar;
		xcb_get_geometry_reply_t *gr;

		win = add_window(&root->window_list, e->window);
		win->x = e->x;
		win->y = e->y;
		win->map_state = XCB_MAP_STATE_UNMAPPED;
		win->override_redirect = e->override_redirect;
		ar = xcb_get_window_attributes_reply(X,
			xcb_get_window_attributes_unchecked(X, win->id), NULL);
		gr = xcb_get_geometry_reply(X,
				xcb_get_geometry_unchecked(X, win->id), NULL);
		if (gr) {
			GEOMCPY(win, gr);
			free(gr);
		}
		if (ar) {
			win->visual = ar->visual;
			free(ar);
		}
	} else if ((win = find_window(root->window_list, e->window))) {
		remove_window(&root->window_list, win);
	}
}

void configure_notify(xcb_configure_notify_event_t *e)
{
	struct window *win;
	uint8_t dmg = 0;	/* "damaged" geometry */

	debugf("ConfigureNotify:\n\tevent=%u window=%u above_sibling=%u\n\t"
			"x=%hd y=%hd width=%hu height=%hu border_width=%hu\n\t"
			"override_redirect=%hu\n", e->event, e->window,
			e->above_sibling, e->x, e->y, e->width,
			e->height, e->border_width,
			e->override_redirect);
	if ((win = find_window(root->window_list, e->window)) == NULL)
		return;
	debugf("\t->next=%u\n", (win->next) ? win->next->id : 0);
	if (win->width != e->width || win->height != e->height) {
		if (win->pixmap != XCB_PIXMAP_NONE) {
			xcb_free_pixmap(X, win->pixmap);
			win->pixmap = XCB_PIXMAP_NONE;
		}
		if (win->picture != XCB_RENDER_PICTURE_NONE) {
			xcb_render_free_picture(X, win->picture);
			win->picture = XCB_RENDER_PICTURE_NONE;
		}
		++dmg;
	} else if (win->x != e->x || win->y != e->y) {
		++dmg;
	}
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
		GEOMCPY(win, e);
	}
	if (win->next == NULL || win->next->id != e->above_sibling)
		restack_window(&root->window_list, win, e->above_sibling);
}

void circulate_notify(xcb_circulate_notify_event_t *e)
{
	printf("CirculateNotify: \n");
	printf("\tevent: %u\n", e->event);
	printf("\twindow: %u\n", e->window);
	printf("place: %hu\n", e->place);
}

void damage_notify(xcb_damage_notify_event_t *e)
{
	xcb_xfixes_region_t parts;

	/*debugf("DamageNotify: %u\n", e->drawable);*/
	parts = xcb_generate_id(X);
	xcb_xfixes_create_region(X, parts, 0, NULL);
	xcb_damage_subtract(X, e->damage, XCB_NONE, parts);
	add_damaged_region(root, parts);
}

