#include <stdio.h>
#include <stdlib.h>
#include "xray.h"

#define WIN_ITER(list, prev) for (prev = list; *prev; prev = &(*prev)->next)
#define WIN_PREV(list, win, tmp)		\
	do {					\
		WIN_ITER(list, tmp)		\
			if (*tmp == win)	\
				break;		\
	} while(0)
#define WIN_UNHOOK(list, win)			\
	do {					\
		struct window **tmp;		\
						\
		WIN_PREV(list, win, tmp);	\
		*tmp = (win)->next;		\
	} while (0)

struct window *find_window(struct window *list, xcb_window_t wid)
{
	struct window *win;

	if (wid != XCB_WINDOW_NONE)
		for (win = list; win != NULL; win = win->next)
			if (win->id == wid)
				break;
	debugf("find_window: window %u was %s\n", wid,
					(win != NULL) ? "found" : "not found");
	return win;
}

struct window *add_window(struct window **list, xcb_window_t wid)
{
	struct window *win;

	if ((win = malloc(sizeof(struct window))) == NULL) {
		fprintf(stderr, "add_window: can't alloc memory\n");
		return NULL;
	}
	win->id = wid;
	win->map_state = XCB_MAP_STATE_UNMAPPED;
	win->x = win->y = 0;
	win->width = win->height = win->border_width = 1;
	win->override_redirect = 0;
	win->pixmap = XCB_PIXMAP_NONE;
	win->picture = XCB_RENDER_PICTURE_NONE;
	win->damage = xcb_generate_id(X);
	xcb_damage_create(X, win->damage, win->id,
				XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
	win->region = xcb_generate_id(X);
	xcb_xfixes_create_region_from_window(X, win->region, win->id,
						XCB_SHAPE_SK_BOUNDING);
	win->prev = NULL;
	win->next = *list;
	*list = win;
	debugf("add_window: window %u added to stack\n", wid);
	return win;
}

int add_winvec(struct window **list, xcb_window_t wid[], int len)
{
	struct window *win;
	xcb_get_window_attributes_cookie_t *ack;
	xcb_get_geometry_cookie_t *gck;
	xcb_get_window_attributes_reply_t *ar;
	xcb_get_geometry_reply_t *gr;
	int i, n = 0;

	ack = malloc(sizeof(ack) * len);
	gck = malloc(sizeof(gck) * len);
	if (!ack || !gck) {
		if (ack)
			free(ack);
		fprintf(stderr, "add_tree: can't alloc memory\n");
		return -1;
	}
	for (i = 0; i < len; ++i) {
		ack[i] = xcb_get_window_attributes_unchecked(X, wid[i]);
		gck[i] = xcb_get_geometry_unchecked(X, wid[i]);
	}
	for (i = 0; i < len; ++i) {
		ar = xcb_get_window_attributes_reply(X, ack[i], NULL);
		gr = xcb_get_geometry_reply(X, gck[i], NULL);
		if (ar->_class == XCB_WINDOW_CLASS_INPUT_OUTPUT &&
				(win = add_window(list, wid[i])) != NULL) {
			GEOMCPY(win, gr);
			win->map_state = ar->map_state;
			win->override_redirect = ar->override_redirect;
			win->damage = xcb_generate_id(X);
			win->region = xcb_generate_id(X);
			win->prev = NULL;
			xcb_damage_create(X, win->damage, win->id,
					XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
			xcb_xfixes_create_region_from_window(X, win->region,
						win->id, XCB_SHAPE_SK_BOUNDING);
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

int remove_window(struct window **list, struct window *win)
{
	WIN_UNHOOK(list, win);
	/* check for pixmap and picture?
	 * pixmap and picutre are destroyed at unmap_notify
		xcb_free_pixmap(X, win->pixmap);
		xcb_render_free_picture(X, win->picture);
	*/
	xcb_damage_destroy(X, win->damage);
	xcb_xfixes_destroy_region(X, win->region);
	debugf("remove_window: window %u removed from stack\n", win->id);
	free(win);
	return 0;
}

void restack_window(struct window **list, struct window *win, xcb_window_t sid)
{
	struct window *sib;	/* sibling */

	debugf("restack_window: window=%u sibling=%u\n", win->id, sid);
	if ((win->next == NULL || win->next->id != sid) &&
			(sib = find_window(root->window_list, sid)) != NULL) {
		struct window **prev;

		WIN_UNHOOK(list, win);
		WIN_PREV(list, sib, prev);
		*prev = win;
		win->next = sib;
	}
}

