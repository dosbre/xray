/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_renderutil.h>
#include <xcb/composite.h>
#include <xcb/xfixes.h>
#include <xcb/shape.h>
#include "xray.h"

xcb_connection_t *X;
struct root *root;
struct window *window_list;
xcb_generic_error_t *error;

uint8_t pict_rgb_24;
xcb_render_picture_t alpha_picture;

static uint8_t damage_event;

int check_cookie(xcb_void_cookie_t ck)
{
	uint8_t ret;
	xcb_generic_error_t *e;

	if ((e = xcb_request_check(X, ck)) == NULL)
		return 0;

	ret = e->error_code;
	debugf("debug: error_code=%hu\n", ret);
	free(e);
	return ret;
}

void xerror(const char *s)
{
	if (error == NULL)
		return;

	debugf("debug: error_code=%hu\n", error->error_code);
	fprintf(stderr, "%s\n", s);
	free(error);
}

static void init_extensions(void)
{
	xcb_render_query_version_cookie_t render_ck;
	xcb_xfixes_query_version_cookie_t xfixes_ck;
	xcb_damage_query_version_cookie_t damage_ck;
	xcb_composite_query_version_cookie_t composite_ck;

	xcb_render_query_version_reply_t *render_r;
	xcb_xfixes_query_version_reply_t *xfixes_r;
	xcb_damage_query_version_reply_t *damage_r;
	xcb_composite_query_version_reply_t *composite_r;

	const xcb_query_extension_reply_t *extr;

	render_ck = xcb_render_query_version_unchecked(X, 0, 11);
	xfixes_ck = xcb_xfixes_query_version_unchecked(X, 3, 0);
	damage_ck = xcb_damage_query_version_unchecked(X, 1, 1);
	composite_ck = xcb_composite_query_version_unchecked(X, 0, 4);

	render_r = xcb_render_query_version_reply(X, render_ck, NULL);
	extr = xcb_get_extension_data(X, &xcb_render_id);
	debugf("render: opcode=%hu, event=%hu, error=%hu\n",
		extr->major_opcode, extr->first_event, extr->first_error);
	xfixes_r = xcb_xfixes_query_version_reply(X, xfixes_ck, NULL);
	extr = xcb_get_extension_data(X, &xcb_xfixes_id);
	debugf("xfixes: opcode=%hu, event=%hu, error=%hu\n",
		extr->major_opcode, extr->first_event, extr->first_error);
	damage_r = xcb_damage_query_version_reply(X, damage_ck, NULL);
	extr = xcb_get_extension_data(X, &xcb_damage_id);
	damage_event = extr->first_event;
	debugf("damage: opcode=%hu, event=%hu, error=%hu\n",
		extr->major_opcode, extr->first_event, extr->first_error);
	composite_r = xcb_composite_query_version_reply(X, composite_ck, NULL);
	extr = xcb_get_extension_data(X, &xcb_composite_id);
	debugf("composite: opcode=%hu, event=%hu, error=%hu\n",
		extr->major_opcode, extr->first_event, extr->first_error);
}

static int redirect_subwindows(xcb_window_t wid)
{
	uint8_t up;
	xcb_void_cookie_t ck;

	/*
	up = XCB_COMPOSITE_REDIRECT_AUTOMATIC;
	*/
	up = XCB_COMPOSITE_REDIRECT_MANUAL;
	ck = xcb_composite_redirect_subwindows_checked(X, wid, up);
	if (check_cookie(ck)) {
		fprintf(stderr, "can't redirect subwindows\n");
		return -1;
	}
	return 0;
}

static int set_root_event_mask(xcb_window_t wid)
{
	uint32_t mask = XCB_CW_EVENT_MASK;
	uint32_t vals[1] = { XCB_EVENT_MASK_STRUCTURE_NOTIFY |
					XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY };

	xcb_change_window_attributes(X, wid, mask, vals);
	return 0;
}

static xcb_render_picture_t get_background(void)
{
	/* TODO: rewrite this instead of paint it gray */
	xcb_pixmap_t pid;
	xcb_render_picture_t pict;
	xcb_render_pictforminfo_t *form;
	uint32_t mask;
	uint32_t vals[1];
	uint8_t op;
	xcb_render_color_t color;
	xcb_rectangle_t rect[1];

	pid = xcb_generate_id(X);
	xcb_create_pixmap(X, root->depth, pid, root->id, 1, 1);
	pict = xcb_generate_id(X);
	form = xcb_render_util_find_standard_format(
					xcb_render_util_query_formats(X),
					XCB_PICT_STANDARD_RGB_24);
	mask = XCB_RENDER_CP_REPEAT;
	vals[0] = 1;
	xcb_render_create_picture(X, pict, pid, form->id, mask, vals);
	op = XCB_RENDER_PICT_OP_SRC;
	color.red = color.green = color.blue = 0x3f3f;
	color.alpha = 0xffff;
	rect[0].x = rect[0].y = 0;
	rect[0].width = rect[0].height = 1;
	xcb_render_fill_rectangles(X, op, pict, color, 1, rect);
	xcb_free_pixmap(X, pid);
	return pict;
}

int paint_background(struct root *r)
{
	uint8_t op;
	
	if (r->background == XCB_RENDER_PICTURE_NONE)
		r->background = get_background();
	op = XCB_RENDER_PICT_OP_SRC;
	xcb_render_composite(X, op, r->background, XCB_NONE, r->picture_buffer,
					0, 0, 0, 0, 0, 0, r->width, r->height);
	return 0;
}

static int setup_root(xcb_screen_t *s, struct root *r)
{
	r->id = s->root;
	r->depth = s->root_depth;
	r->width = s->width_in_pixels;
	r->height = s->height_in_pixels;

	if (redirect_subwindows(r->id) != 0)
		return -1;
	set_root_event_mask(r->id);
	r->picture = xcb_generate_id(X);
	xcb_render_create_picture(X, r->picture, r->id, pict_rgb_24, 0, NULL);
	{
		xcb_pixmap_t pid = xcb_generate_id(X);

		xcb_create_pixmap(X, r->depth, pid, r->id, r->width, r->height);
		r->picture_buffer = xcb_generate_id(X);
		xcb_render_create_picture(X, r->picture_buffer, pid,
							pict_rgb_24, 0, NULL);
		xcb_free_pixmap(X, pid);
	}
	root->background = get_background();
	paint_background(root);
	{
	xcb_render_composite(X, XCB_RENDER_PICT_OP_SRC, r->picture_buffer,
				XCB_RENDER_PICTURE_NONE, r->picture, 0, 0,
				0, 0, 0, 0, r->width, r->height);
	}
	root->damaged = 0;
	return 0;
}

struct window *add_window(xcb_window_t wid, uint8_t map,
							xcb_rectangle_t geom)
{
	struct window *win;
	uint8_t opt;

	win = malloc(sizeof(struct window));
	if (!win) {
		debugf("can't alloc window %u\n", wid);
		return NULL;
	}
	printf("added window %u to stack; map=%u\n", wid, map);
	{
		uint32_t val[1] = { XCB_EVENT_MASK_VISIBILITY_CHANGE };

		xcb_configure_window(X, wid, XCB_CW_EVENT_MASK, val);
	}
	win->id = wid;
	win->map_state = map;
	win->geometry = geom;
	if (map != XCB_MAP_STATE_UNMAPPED) {
		win->pixmap = xcb_generate_id(X);
		xcb_composite_name_window_pixmap(X, wid, win->pixmap);
		win->picture = xcb_generate_id(X);
		xcb_render_create_picture(X, win->picture, win->pixmap,
							pict_rgb_24, 0, NULL);
	} else {
		win->pixmap = XCB_PIXMAP_NONE;
		win->picture = XCB_RENDER_PICTURE_NONE;
	}
	win->damage = xcb_generate_id(X);
	opt = XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY;
	xcb_damage_create(X, win->damage, wid, opt);
	win->region = xcb_generate_id(X);
	opt = XCB_SHAPE_SK_BOUNDING;
	xcb_xfixes_create_region_from_window(X, win->region, wid, opt);
	win->next = window_list;
	if (window_list)
		window_list->prev = win;
	win->prev = NULL;
	window_list = win;
	xcb_flush(X);
	return win;
}

struct window *find_window(xcb_window_t wid)
{
	struct window *win;

	for (win = window_list; win; win = win->next)
		if (win->id == wid)
			break;
	return win;
}

int remove_window(struct window *win)
{
	struct window **prev;

	if (win->pixmap != XCB_PIXMAP_NONE)
		xcb_free_pixmap(X, win->pixmap);
	if (win->picture != XCB_RENDER_PICTURE_NONE)
		xcb_render_free_picture(X, win->picture);
	xcb_damage_destroy(X, win->damage);
	xcb_xfixes_destroy_region(X, win->region);

	for (prev = &window_list; *prev != NULL; prev = &(*prev)->next)
		if ((*prev)->id == win->id)
			break;
	*prev = win->next;
	debugf("window %u removed from stack\n", win->id);
	free(win);
	return 0;
}

void restack_window(struct window *win, xcb_window_t wid)
{
	xcb_window_t old;
	struct window **prev;

	old = (win->next) ? win->next->id : XCB_WINDOW_NONE;
	if (old != wid) {
		for (prev = &window_list; *prev; prev = &(*prev)->next)
			if ((*prev) == win)
				break;
		*prev = win->next;
		for (prev = &window_list; *prev; prev = &(*prev)->next)
			if ((*prev)->id == wid)
				break;
		win->next = *prev;
		*prev = win;
	}
}

static int add_tree(xcb_window_t *wid, int len)
{
	xcb_get_window_attributes_cookie_t *ack;
	xcb_get_geometry_cookie_t *gck;
	int i;

	ack = malloc(sizeof(ack) * len);
	if (!ack)
		return -1;
	gck = malloc(sizeof(gck) * len);
	if (!gck) {
		free(ack);
		return -1;
	}
	for (i = 0; i < len; ++i) {
		ack[i] = xcb_get_window_attributes_unchecked(X, wid[i]);
		gck[i] = xcb_get_geometry_unchecked(X, wid[i]);
	}
	for (i = 0; i < len; ++i) {
		xcb_get_window_attributes_reply_t *ar;
		xcb_get_geometry_reply_t *gr;
		xcb_rectangle_t geom;

		ar = xcb_get_window_attributes_reply(X, ack[i], NULL);
		gr = xcb_get_geometry_reply(X, gck[i], NULL);
		geom.x = gr->x;
		geom.y = gr->y;
		geom.width = gr->width;
		geom.height = gr->height;
		/*
		if (ar->_class == XCB_WINDOW_CLASS_INPUT_OUTPUT)
		*/
			add_window(wid[i], ar->map_state, geom);
		free(ar);
		free(gr);
	}
	free(ack);
	free(gck);
	return 0;
}

void add_damaged_region(struct root *r, xcb_xfixes_region_t reg)
{
	xcb_xfixes_union_region(X, reg, r->damage, r->damage);
	r->damaged++;
}

static struct window *sanitize_window_list(struct window *win)
{
	struct window *tmp;

	for (tmp = NULL; win != NULL; win = win->next) {
		if (win->map_state != XCB_MAP_STATE_VIEWABLE)
			continue;
		if (win->pixmap == XCB_PIXMAP_NONE) {
			win->pixmap = xcb_generate_id(X);
			xcb_composite_name_window_pixmap(X, win->id,
								win->pixmap);
		}
		if (win->picture == XCB_RENDER_PICTURE_NONE) {
			win->picture = xcb_generate_id(X);
			xcb_render_create_picture(X, win->picture,
							win->pixmap,
							pict_rgb_24,
							0, NULL);
		}
		win->prev = tmp;
		tmp = win;
	}
	return tmp;
}

void paint(struct root *r)
{
	struct window *win;
	xcb_rectangle_t geom;
	xcb_void_cookie_t ck;
	uint8_t op;
	xcb_render_picture_t msk;

	xcb_xfixes_set_picture_clip_region(X, r->picture_buffer, r->damage,
									0, 0);
	paint_background(r);
	win = sanitize_window_list(window_list);
	for (; win != NULL; win = win->prev) {
		op = XCB_RENDER_PICT_OP_OVER;
		msk = alpha_picture;
		geom = win->geometry;
		ck = xcb_render_composite_checked(X, op, win->picture, msk,
					r->picture_buffer, 0, 0, 0, 0, geom.x,
					geom.y, geom.width, geom.height);
		if (check_cookie(ck) != 0) {
			debugf("composite error: wid=%u\n", win->id);
		}
	}
	xcb_xfixes_set_picture_clip_region(X, r->picture_buffer, XCB_NONE,
									0, 0);
	xcb_render_composite(X, XCB_RENDER_PICT_OP_SRC, r->picture_buffer,
				XCB_RENDER_PICTURE_NONE, r->picture, 0, 0,
				0, 0, 0, 0, r->width, r->height);
	xcb_xfixes_set_region(X, r->damage, 0, NULL);
	r->damaged = 0;
}

static int init_windows(void)
{
	xcb_query_tree_cookie_t ck;
	xcb_query_tree_reply_t *r;
	xcb_window_t *wid;
	int len;
	int ret;

	ck = xcb_query_tree_unchecked(X, root->id);
	r = xcb_query_tree_reply(X, ck, NULL);
	wid = xcb_query_tree_children(r);
	len = xcb_query_tree_children_length(r);
	if ((ret = add_tree(wid, len)) == -1)
		fprintf(stderr, "can't init windows\n");
	free(r);
	return ret;
}

static void handle_event(xcb_generic_event_t *e)
{
	/*
	debugf("event: %hu\n", e->response_type);
	*/
	if (e->response_type == damage_event)
		HANDLE(damage_notify, e);
	else {
		switch (e->response_type) {
		case XCB_CREATE_NOTIFY:
			HANDLE(create_notify, e);
			break;
		case XCB_DESTROY_NOTIFY:
			HANDLE(destroy_notify, e);
			break;
		case XCB_UNMAP_NOTIFY:
			HANDLE(unmap_notify, e);
			break;
		case XCB_MAP_NOTIFY: 
			HANDLE(map_notify, e);
			break;
		case XCB_REPARENT_NOTIFY:
			HANDLE(reparent_notify, e);
			break;
		case XCB_CONFIGURE_NOTIFY:
			HANDLE(configure_notify, e);
			break;
		case XCB_CIRCULATE_NOTIFY:
			HANDLE(circulate_notify, e);
			break;
		default:
			if (e->response_type)
				printf("unhandled event: %hu\n",
							e->response_type);
			break;
		}
	}
}

static int loop(void)
{
	xcb_generic_event_t *e;

	while (1) {
		if (!(e = xcb_wait_for_event(X)))
			return 1;
		do {
			handle_event(e);
			free(e);
		} while ((e = xcb_poll_for_event(X)));
		xcb_flush(X);
		if (root->damaged)
			paint(root);
	}
	return 0;
}

int main(int argc, char *argv[])
{
	xcb_screen_t *s;

	X = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(X)) {
		fprintf(stderr, "can't connect X\n");
		return -1;
	}
	xcb_prefetch_extension_data(X, &xcb_render_id);
	xcb_prefetch_extension_data(X, &xcb_xfixes_id);
	xcb_prefetch_extension_data(X, &xcb_damage_id);
	xcb_prefetch_extension_data(X, &xcb_composite_id);
	init_extensions();
	pict_rgb_24 = xcb_render_util_find_standard_format(
					xcb_render_util_query_formats(X),
					XCB_PICT_STANDARD_RGB_24)->id;
	s = xcb_setup_roots_iterator(xcb_get_setup(X)).data;
	root = malloc(sizeof(struct root));
	if (root == NULL || setup_root(s, root) != 0)
		return -1;
	alpha_picture = get_alpha_picture();
	if (init_windows() != 0)
		return -1;
	loop();
	return 0;
}

