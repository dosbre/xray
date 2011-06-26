#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/damage.h>
#include <xcb/composite.h>
#include "xray.h"

struct window *find_win(struct window *list, xcb_window_t wid)
{
	struct window *w = NULL;

	if (wid != XCB_NONE) {
		for (w = list; w; w = w->next)
			if (w->id == wid)
				break;;
		if (!w)
			fprintf(stderr, "find_window: window %u "
							"not found\n", wid);
	}
	return w;
}

struct window *add_win(struct window **list, xcb_window_t wid)
{
	struct window *w = NULL;
	uint32_t mask = XCB_CW_EVENT_MASK;
	const uint32_t vals[1] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
	uint8_t flag;

	if ((w = malloc(sizeof(struct window)))) {
		w->id = wid;
		w->damage = xcb_generate_id(X);
		flag = XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY;
		xcb_damage_create(X, w->damage, w->id, flag);
		w->region = xcb_generate_id(X);
		flag = XCB_SHAPE_SK_BOUNDING;
		xcb_xfixes_create_region_from_window(X, w->region, w->id, flag);
		w->pixmap = XCB_NONE;
		w->picture = XCB_NONE;
		w->opacity = get_window_opacity(w->id);
		w->alpha = XCB_NONE;
		w->prev = NULL;
		w->next = *list;
		*list = w;
		xcb_change_window_attributes(X, w->id, mask, vals);
	} else {
		fprintf(stderr, "add_window: can't alloc memory\n");
	}
	return w;
}

void remove_win(struct window **list, struct window *w)
{
	struct window **prev;

	for (prev = list; *prev; prev = &(*prev)->next)
		if (*prev == w)
			break;
	*prev = w->next;
	xcb_damage_destroy(X, w->damage);
	xcb_xfixes_destroy_region(X, w->region);
	if (w->pixmap)
		xcb_free_pixmap(X, w->pixmap);
	if (w->picture)
		xcb_render_free_picture(X, w->picture);
	if (w->alpha)
		xcb_render_free_picture(X, w->alpha);
	free(w);
}

void restack_win(struct window **list, struct window *w, xcb_window_t wid)
{
	struct window **prev;

	for (prev = list; *prev; prev = &(*prev)->next)
		if (*prev == w)
			break;
	*prev = w->next;
	for (prev = list; *prev; prev = &(*prev)->next)
		if ((*prev)->id == wid)
			break;
	w->next = *prev;
	*prev = w;
}

int add_winvec(struct window **list, xcb_window_t wid[], int len)
{
	xcb_get_window_attributes_cookie_t ack[len];
	xcb_get_geometry_cookie_t gck[len];
	struct window *w;
	int i, n = 0;

	for (i = 0; i < len; ++i) {
		ack[i] = xcb_get_window_attributes_unchecked(X, wid[i]);
		gck[i] = xcb_get_geometry_unchecked(X, wid[i]);
	}
	for (i = 0; i < len; ++i) {
		xcb_get_window_attributes_reply_t *ar;
		xcb_get_geometry_reply_t *gr;

		if ((w = add_win(list, wid[i])) != NULL) {
			ar = xcb_get_window_attributes_reply(X, ack[i], NULL);
			gr = xcb_get_geometry_reply(X, gck[i], NULL);
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
				++n;
			} else {
				if (ar)
					free(ar);
				if (gr)
					free(gr);
				remove_win(list, w);
				continue;
			}
		}
	}
	debugf("add_winvec: added %d of %d windows to stack\n", n, len);
	return n;
}

