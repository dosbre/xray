#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/damage.h>
#include <xcb/xcb_renderutil.h>
#include "xray.h"

void create_notify(xcb_create_notify_event_t *e)
{
	struct window *w;
	xcb_get_window_attributes_cookie_t ack;
	xcb_get_window_attributes_reply_t *ar;

	debugf("CreateNotify: parent=%u window=%u\n\tx=%hd y=%hd "
			"width=%hu height=%hu border_width=%hu\n\t"
			"override_redirect=%#x\n", e->parent,
			e->window, e->x, e->y, e->width, e->height,
			e->border_width, e->override_redirect);
	ack = xcb_get_window_attributes_unchecked(X, e->window);
	ar = xcb_get_window_attributes_reply(X, ack, NULL);
	if (ar && (w = add_win(&root->window_list, e->window)) != NULL) {
		w->visual = ar->visual;
		w->_class = ar->_class;
		w->map_state = ar->map_state;
		free(ar);
		w->x = e->x;
		w->y = e->y;
		w->width = e->width;
		w->height = e->height;
		w->border_width = e->border_width;
	}
}

void destroy_notify(xcb_destroy_notify_event_t *e)
{
	struct window *w;

	debugf("DestroyNotify: window=%u\n", e->window);
	if ((w = find_win(root->window_list, e->window)) != NULL) {
		add_damaged_region(root, w->region);
		xcb_flush(X);
		remove_win(&root->window_list, w);
	}
}

void unmap_notify(xcb_unmap_notify_event_t *e)
{
	struct window *w;

	debugf("UnmapNotify: window=%u\n", e->window);
	if ((w = find_win(root->window_list, e->window))) {
		if (w->pixmap != XCB_NONE) {
			xcb_free_pixmap(X, w->pixmap);
			w->pixmap = XCB_NONE;
		}
		if (w->picture != XCB_NONE) {
			xcb_render_free_picture(X, w->picture);
			w->picture = XCB_NONE;
		}
		w->map_state = XCB_MAP_STATE_UNMAPPED;
		add_damaged_region(root, w->region); }
}

void map_notify(xcb_map_notify_event_t *e)
{
	struct window *w;

	debugf("MapNotify: window=%u\n", e->window);
	if ((w = find_win(root->window_list, e->window))) {
		w->map_state = XCB_MAP_STATE_VIEWABLE;
		if (w->_class == XCB_WINDOW_CLASS_INPUT_OUTPUT) {
			w->pixmap = xcb_generate_id(X);
			xcb_composite_name_window_pixmap(X, w->id, w->pixmap);
			{
			xcb_render_pictvisual_t *pv;
			pv = xcb_render_util_find_visual_format(
					xcb_render_util_query_formats(X),
					w->visual);
			w->picture = xcb_generate_id(X);
			xcb_render_create_picture(X, w->picture, w->pixmap,
							pv->format, 0, NULL);
			}
			add_damaged_region(root, w->region);
		}
	}
}

void reparent_notify(xcb_reparent_notify_event_t *e)
{
	struct window *w;
	xcb_get_window_attributes_cookie_t ack;
	xcb_get_geometry_cookie_t gck;

	debugf("ReparentNotify: event=%u window=%u parent=%u\n", e->event,
							e->window, e->parent);
	if (e->parent == root->id &&
			(w = add_win(&root->window_list, e->window))) {
		xcb_get_window_attributes_reply_t *ar;
		xcb_get_geometry_reply_t *gr;

		ack = xcb_get_window_attributes_unchecked(X, w->id);
		gck = xcb_get_geometry_unchecked(X, w->id);
		ar = xcb_get_window_attributes_reply(X, ack, NULL);
		gr = xcb_get_geometry_reply(X, gck, NULL);
		if (ar && gr) {
			w->visual = ar->visual;
			w->_class = ar->_class;
			w->map_state = ar->map_state;
			w->x = gr->x;
			w->y = gr->y;
			w->width = gr->width;
			w->height = gr->height;
			w->border_width = gr->border_width;
			free(ar);
			free(gr);
			printf("ATTRIBUTES\n");
		} else {
			printf("NOT ATTRIBUTES\n");
			remove_win(&root->window_list, w);
		}
	} else if ((w = find_win(root->window_list, e->window))) {
		printf("REMOVE WINDOW\n");
		remove_win(&root->window_list, w);
	}
}

void configure_notify(xcb_configure_notify_event_t *e)
{
	struct window *w;
	uint8_t dmg = 0;	/* "damaged" geometry */

	debugf("ConfigureNotify:\n\tevent=%u window=%u above_sibling=%u\n\t"
			"x=%hd y=%hd width=%hu height=%hu border_width=%hu\n\t"
			"override_redirect=%hu\n", e->event, e->window,
			e->above_sibling, e->x, e->y, e->width,
			e->height, e->border_width,
			e->override_redirect);
	if ((w = find_win(root->window_list, e->window)) == NULL)
		return;
	debugf("\t->next=%u\n", (w->next) ? w->next->id : 0);
	if (w->width != e->width || w->height != e->height) {
		if (w->pixmap) {
			xcb_free_pixmap(X, w->pixmap);
			w->pixmap = XCB_NONE;
		}
		if (w->picture) {
			xcb_render_free_picture(X, w->picture);
			w->picture = XCB_NONE;
		}
		w->width = e->width;
		w->height = e->height;
		++dmg;
	}
	if (w->x != e->x || w->y != e->y) {
		w->x = e->x;
		w->y = e->y;
		++dmg;
	}
	if (w->border_width != e->border_width)
		w->border_width = e->border_width;
	if (dmg) {
		xcb_xfixes_region_t reg;
		xcb_xfixes_region_t old;

		old = w->region;
		reg = xcb_generate_id(X);
		xcb_xfixes_create_region_from_window(X, reg, w->id,
							XCB_SHAPE_SK_BOUNDING);
		xcb_xfixes_union_region(X, old, reg, old);
		add_damaged_region(root, old);
		xcb_xfixes_destroy_region(X, old);
		w->region = reg;
	}
	if (w->next == NULL || w->next->id != e->above_sibling)
		restack_win(&root->window_list, w, e->above_sibling);
}

void circulate_notify(xcb_circulate_notify_event_t *e)
{
	struct window *w;
	struct window **prev;

	debugf("CirculateNotify: window=%u\n", e->window);
	if ((w = find_win(root->window_list, e->window)) != NULL) {
		for (prev = &root->window_list; *prev; prev = &(*prev)->next)
			if (*prev == w)
				break;
		*prev = w->next;
		if (e->place == XCB_PLACE_ON_TOP) {
			prev = &root->window_list;
			w->next = *prev;
			*prev = w;
		} else if (e->place == XCB_PLACE_ON_BOTTOM) {
			while (*prev)
				prev = &(*prev)->next;
			*prev = w;
			w->next = NULL;
		}
	}
}

void property_notify(xcb_property_notify_event_t *e)
{
	struct window *w;

	debugf("PropertyNotify: window=%u atom=%u time=%u state=%#x\n",
					e->window, e->atom, e->time, e->state);
	if (e->atom == _NET_WM_WINDOW_OPACITY &&
			(w = find_win(root->window_list, e->window))) {
		w->opacity = (e->state == XCB_PROPERTY_NEW_VALUE) ?
					get_window_opacity(w->id) : OPAQUE;
		if (w->alpha != XCB_NONE) {
			xcb_render_free_picture(X, w->alpha);
			w->alpha = XCB_NONE;
		}
		add_damaged_region(root, w->region);
	} else if (e->atom == _XROOTPMAP_ID) {
		if (root->background != XCB_NONE) {
			xcb_render_free_picture(X, root->background);
			root->background = XCB_NONE;
		}
	}
}

void damage_notify(xcb_damage_notify_event_t *e)
{
	xcb_xfixes_region_t parts;
	/*struct window *win;*/

	/*debugf("DamageNotify: drawable=%u\n", e->drawable);
	if (!(win = find_win(root->window_list, e->drawable)))
		return;*/
	parts = xcb_generate_id(X);
	xcb_xfixes_create_region(X, parts, 0, NULL);
	xcb_damage_subtract(X, e->damage, XCB_NONE, parts);
	add_damaged_region(root, parts);
	xcb_damage_destroy(X, parts);
}

