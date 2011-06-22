#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/damage.h>
#include <xcb/composite.h>
#include "xray.h"

struct window *find_win(struct window *list, xcb_window_t wid)
{
	struct window *win = NULL;

	if (wid != XCB_WINDOW_NONE) {
		for (win = list; win; win = win->next)
			if (win->id == wid)
				break;;
		if (win == NULL)
			debugf("find_window: window %u not found\n", wid);
	}
	return win;
}

struct window *add_win(struct window **list, xcb_window_t wid)
{
	struct window *win;

	if ((win = malloc(sizeof(struct window))) == NULL) {
		fprintf(stderr, "add_window: can't alloc memory\n");
		return NULL;
	}
	win->id = wid;
	win->damage = xcb_generate_id(X);
	xcb_damage_create(X, win->damage, win->id,
					XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
	win->region = xcb_generate_id(X);
	xcb_xfixes_create_region_from_window(X, win->region, win->id,
							XCB_SHAPE_SK_BOUNDING);
	win->next = *list;
	*list = win;
	return win;
}

int remove_win(struct window **list, struct window *win)
{
	struct window **prev;

	/* unhook */
	for (prev = list; *prev; prev = &(*prev)->next)
		if (*prev == win)
			break;
	*prev = win->next;
	xcb_damage_destroy(X, win->damage);
	xcb_xfixes_destroy_region(X, win->region);
	free(win);
	return 0;
}

void restack_win(struct window **list, struct window *win, xcb_window_t sid)
{
	struct window **prev;

	/* unhook */
	for (prev = list; *prev; prev = &(*prev)->next)
		if (*prev == win)
			break;
	*prev = win->next;
	/* rehook */
	for (prev = list; *prev; prev = &(*prev)->next)
		if (*prev && (*prev)->id == sid)
			break;
	win->next = *prev;
	*prev = win;
}

void init_win(struct window *win, xcb_get_window_attributes_reply_t *ar,
						xcb_get_geometry_reply_t *gr)
{
	if (ar != NULL) {
		win->visual = ar->visual;
		win->map_state = ar->map_state;
	}
	if (gr != NULL) {
		win->x = gr->x;
		win->y = gr->y;
		win->width = gr->width;
		win->height = gr->height;
		win->border_width = gr->border_width;
	}
	win->pixmap = XCB_PIXMAP_NONE;
	win->picture = XCB_RENDER_PICTURE_NONE;
}

int add_winvec(struct window **list, xcb_window_t wid[], int len)
{
	xcb_get_window_attributes_cookie_t *ack;
	xcb_get_geometry_cookie_t *gck;
	xcb_get_window_attributes_reply_t *ar;
	xcb_get_geometry_reply_t *gr;
	struct window *win;
	int i, n = 0;

	ack = malloc(sizeof(ack) * len);
	gck = malloc(sizeof(gck) * len);
	if (!ack || !gck) {
		if (ack)
			free(ack);
		if (gck)
			free(gck);
		fprintf(stderr, "add_tree: can't malloc\n");
		return -1;
	}
	for (i = 0; i < len; ++i) {
		ack[i] = xcb_get_window_attributes_unchecked(X, wid[i]);
		gck[i] = xcb_get_geometry_unchecked(X, wid[i]);
	}
	for (i = 0; i < len; ++i) {
		ar = xcb_get_window_attributes_reply(X, ack[i], NULL);
		gr = xcb_get_geometry_reply(X, gck[i], NULL);
		if (!ar || !gr) {
			fprintf(stderr, "add_winvec: can't get window "
								"attributes\n");
			if (ar)
				free(ar);
			if (gr)
				free(gr);
			continue;
		}
		if (ar->_class == XCB_WINDOW_CLASS_INPUT_OUTPUT &&
				(win = add_win(list, wid[i])) != NULL) {
			win->visual = ar->visual;
			win->map_state = ar->map_state;
			win->x = gr->x;
			win->y = gr->y;
			win->width = gr->width;
			win->height = gr->height;
			win->border_width = gr->border_width;
			debugf("add_winvec: window %u on stack\n", win->id);
			++n;
		}
		free(ar);
		free(gr);
	}
	free(ack);
	free(gck);
	debugf("add_winvec: added %d of %d windows to stack\n", n, len);
	return n;
}

