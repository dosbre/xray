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

	debugf("CreateNotify: window=%u\n", e->window);
	if ((win = add_win(&root->window_list, e->window))) {
		xcb_get_window_attributes_reply_t *ar;

		ar = xcb_get_window_attributes_reply(X,
			xcb_get_window_attributes_unchecked(X, win->id), NULL);
		if (ar) {
			win->visual = ar->visual;
			win->map_state = ar->map_state;
			free(ar);
		}
		win->x = e->x;
		win->y = e->y;
		win->width = e->width;
		win->height = e->height;
		win->border_width = e->border_width;
	}
}

void destroy_notify(xcb_destroy_notify_event_t *e)
{
	struct window *win;

	debugf("DestroyNotify: window=%u\n", e->window);
	if ((win = find_win(root->window_list, e->window)) != NULL) {
		add_damaged_region(root, win->region);
		remove_win(&root->window_list, win);
	}
}

void unmap_notify(xcb_unmap_notify_event_t *e)
{
	struct window *win;

	debugf("UnmapNotify: window=%u\n", e->window);
	if ((win = find_win(root->window_list, e->window))) {
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

	debugf("MapNotify: window=%u\n", e->window);
	if ((win = find_win(root->window_list, e->window))) {
		win->map_state = XCB_MAP_STATE_VIEWABLE;
		win->pixmap = xcb_generate_id(X);
		xcb_composite_name_window_pixmap(X, win->id, win->pixmap);
		win->picture = xcb_generate_id(X);
		xcb_render_create_picture(X, win->picture, win->pixmap,
							pict_rgb_24, 0, NULL);
		add_damaged_region(root, win->region);
	}
}

void reparent_notify(xcb_reparent_notify_event_t *e)
{
	struct window *win;

	debugf("ReparentNotify: window=%u\n", e->window);
	if (e->parent == root->id &&
			(win = add_win(&root->window_list, e->window))) {
		xcb_get_window_attributes_reply_t *ar;
		xcb_get_geometry_reply_t *gr;

		ar = xcb_get_window_attributes_reply(X,
			xcb_get_window_attributes_unchecked(X, win->id), NULL);
		gr = xcb_get_geometry_reply(X,
				xcb_get_geometry_unchecked(X, win->id), NULL);
		if (ar) {
			win->visual = ar->visual;
			win->map_state = ar->map_state;
			free(ar);
		}
		if (gr) {
			win->x = gr->x;
			win->y = gr->y;
			win->width = gr->width;
			win->height = gr->height;
			win->border_width = gr->border_width;
			free(gr);
		}
	} else if ((win = find_win(root->window_list, e->window))) {
		remove_win(&root->window_list, win);
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
	if ((win = find_win(root->window_list, e->window)) == NULL)
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
		win->width = e->width;
		win->height = e->height;
		++dmg;
	}
	if (win->x != e->x || win->y != e->y) {
		win->x = e->x;
		win->y = e->y;
		++dmg;
	}
	if (win->border_width != e->border_width)
		win->border_width = e->border_width;
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
	if (win->next == NULL || win->next->id != e->above_sibling)
		restack_win(&root->window_list, win, e->above_sibling);
}

void circulate_notify(xcb_circulate_notify_event_t *e)
{
	struct window *win;
	struct window **prev;

	debugf("CirculateNotify: window=%u\n", e->window);
	if (!(win = find_win(root->window_list, e->window)))
		return;
	if ((e->place == XCB_PLACE_ON_TOP && win != root->window_list) ||
			(e->place == XCB_PLACE_ON_BOTTOM && win->next)) {
		for (prev = &root->window_list; *prev; prev = &(*prev)->next)
			if (*prev == win)
				break;
		*prev = win->next;
		if (e->place == XCB_PLACE_ON_TOP) {
			prev = &root->window_list;
			win->next = *prev;
			*prev = win;
		} else if (e->place == XCB_PLACE_ON_BOTTOM) {
			while (*prev)
				prev = &(*prev)->next;
			*prev = win;
			win->next = NULL;
		}
	}
}

void damage_notify(xcb_damage_notify_event_t *e)
{
	xcb_xfixes_region_t parts;

	/*debugf("DamageNotify: drawable=%u\n", e->drawable);*/
	parts = xcb_generate_id(X);
	xcb_xfixes_create_region(X, parts, 0, NULL);
	xcb_damage_subtract(X, e->damage, XCB_NONE, parts);
	add_damaged_region(root, parts);
}

