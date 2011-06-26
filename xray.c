/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/damage.h>
#include <xcb/composite.h>
#include <xcb/xcb_renderutil.h>
#include "xray.h"

static uint8_t damage_event;

static void init_extensions(void)
{
	xcb_render_query_version_cookie_t rck;
	xcb_xfixes_query_version_cookie_t fck;
	xcb_damage_query_version_cookie_t dck;
	xcb_composite_query_version_cookie_t cck;
	xcb_render_query_version_reply_t *rr;
	xcb_xfixes_query_version_reply_t *fr;
	xcb_damage_query_version_reply_t *dr;
	xcb_composite_query_version_reply_t *cr;
	const xcb_query_extension_reply_t *extr;

	rck = xcb_render_query_version_unchecked(X, 0, 11);
	fck = xcb_xfixes_query_version_unchecked(X, 3, 0);
	dck = xcb_damage_query_version_unchecked(X, 1, 1);
	cck = xcb_composite_query_version_unchecked(X, 0, 4);
	rr = xcb_render_query_version_reply(X, rck, NULL);
	fr = xcb_xfixes_query_version_reply(X, fck, NULL);
	dr = xcb_damage_query_version_reply(X, dck, NULL);
	cr = xcb_composite_query_version_reply(X, cck, NULL);
	extr = xcb_get_extension_data(X, &xcb_render_id);
	printf("render %u.%u: opcode=%hu, event=%hu, error=%hu\n",
		rr->major_version, rr->minor_version, extr->major_opcode,
		extr->first_event, extr->first_error);
	free(rr);
	extr = xcb_get_extension_data(X, &xcb_xfixes_id);
	printf("xfixes %u.%u: opcode=%hu, event=%hu, error=%hu\n",
		fr->major_version, fr->minor_version, extr->major_opcode,
		extr->first_event, extr->first_error);
	free(fr);
	extr = xcb_get_extension_data(X, &xcb_damage_id);
	damage_event = extr->first_event;
	printf("damage %u.%u: opcode=%hu, event=%hu, error=%hu\n",
		dr->major_version, dr->minor_version, extr->major_opcode,
		extr->first_event, extr->first_error);
	free(dr);
	extr = xcb_get_extension_data(X, &xcb_composite_id);
	printf("composite %u.%u: opcode=%hu, event=%hu, error=%hu\n",
		cr->major_version, cr->minor_version, extr->major_opcode,
		extr->first_event, extr->first_error);
	free(cr);
}

xcb_atom_t _XROOTPMAP_ID;
xcb_atom_t _NET_WM_WINDOW_OPACITY;

static void init_atoms(void)
{
	struct {
		const char *s;
		xcb_atom_t *a;
	} atom[2] = {
		{ "_XROOTPMAP_ID", &_XROOTPMAP_ID },
		{ "_NET_WM_WINDOW_OPACITY", &_NET_WM_WINDOW_OPACITY }
	};
	xcb_intern_atom_cookie_t ck[LENGTH(atom)];
	xcb_intern_atom_reply_t *r;
	size_t i;

	for (i = 0; i < LENGTH(atom); ++i)
		ck[i] = xcb_intern_atom_unchecked(X, 0,
						strlen(atom[i].s), atom[i].s);
	for (i = 0; i < LENGTH(atom); ++i) {
		r = xcb_intern_atom_reply(X, ck[i], NULL);
		if (r) {
			*(atom[i].a) = r->atom;
			free(r);
		} else {
			fprintf(stderr, "init_atoms: can't get %s\n",
								atom[i].s);
		}
	}
}

static xcb_render_picture_t get_background(xcb_window_t wid)
{
	xcb_pixmap_t pid;
	xcb_render_picture_t pict;
	uint32_t val = 1;
	int fill = 0;
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *r;

	ck = xcb_get_property_unchecked(X, 0, wid, _XROOTPMAP_ID,
							XCB_ATOM_PIXMAP, 0, 4);
	r = xcb_get_property_reply(X, ck, NULL);
	if (r && r->type == XCB_ATOM_PIXMAP && r->value_len) {
		pid = *(uint32_t *) xcb_get_property_value(r);
		free(r);
	} else {
		debug("needs to fill\n");
		pid = xcb_generate_id(X);
		xcb_create_pixmap(X, root->depth, pid, root->id, 1, 1);
		++fill;
	}
	pict = xcb_generate_id(X);
	xcb_render_create_picture(X, pict, pid, pict_rgb_24,
						XCB_RENDER_CP_REPEAT, &val);
	if (fill) {
		xcb_render_color_t c = { 0x3f3f, 0x3f3f, 0x3f3f, 0xffff };
		xcb_rectangle_t r = { 0, 0, 1, 1 };
		xcb_render_fill_rectangles(X, XCB_RENDER_PICT_OP_SRC, pict, c,
									1, &r);
		xcb_free_pixmap(X, pid);
	}
	return pict;
}

static void paint_background(struct root *r)
{
	if (r->background == XCB_NONE)
		r->background = get_background(r->id);
	xcb_xfixes_set_picture_clip_region(X, r->picture_buffer, r->damage,
									0, 0);
	xcb_render_composite(X, XCB_RENDER_PICT_OP_SRC, r->background,
					XCB_NONE, r->picture_buffer, 0, 0,
					0, 0, 0, 0, r->width, r->height);
}

static int scan_children(xcb_window_t wid, struct window **list)
{
	xcb_query_tree_reply_t *r;

	r = xcb_query_tree_reply(X, xcb_query_tree_unchecked(X, wid), NULL);
	if (r) {
		add_winvec(list, xcb_query_tree_children(r), r->children_len);
		free(r);
		return 0;
	}
	return -1;
}

static int setup_root(xcb_screen_t *s, struct root *r)
{
	uint32_t mask;
	uint32_t vals[1];
	xcb_void_cookie_t ck;

	r->id = s->root;
	mask = XCB_CW_EVENT_MASK;
	vals[0] = XCB_EVENT_MASK_STRUCTURE_NOTIFY |
					XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
					XCB_EVENT_MASK_PROPERTY_CHANGE;
	xcb_change_window_attributes(X, r->id, mask, vals);
	mask = XCB_COMPOSITE_REDIRECT_MANUAL;
	ck = xcb_composite_redirect_subwindows_checked(X, r->id, mask);
	if (check_cookie(ck) != 0) {
		fprintf(stderr, "another composite manager is running\n");
		return -1;
	}
	r->depth = s->root_depth;
	r->width = s->width_in_pixels;
	r->height = s->height_in_pixels;
	r->picture = xcb_generate_id(X);
	mask = XCB_RENDER_CP_SUBWINDOW_MODE;
	vals[0] = XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS;
	{
		xcb_render_pictvisual_t *pv;
		pv = xcb_render_util_find_visual_format(
				xcb_render_util_query_formats(X),
				s->root_visual);
	xcb_render_create_picture(X, r->picture, r->id, pv->format, mask, vals);
	}
	{
		xcb_pixmap_t pid = xcb_generate_id(X);
		xcb_render_pictvisual_t *pv;

		pv = xcb_render_util_find_visual_format(
					xcb_render_util_query_formats(X),
					s->root_visual);
		xcb_create_pixmap(X, r->depth, pid, r->id, r->width, r->height);
		r->picture_buffer = xcb_generate_id(X);
		xcb_render_create_picture(X, r->picture_buffer, pid,
						pv->format, mask, vals);
		xcb_free_pixmap(X, pid);
	}
	r->background = get_background(r->id);
	r->damaged = 0;
	r->window_list = NULL;
	return scan_children(r->id, &r->window_list);;
}

void add_damaged_region(struct root *r, xcb_xfixes_region_t reg)
{
	xcb_xfixes_union_region(X, r->damage, reg, r->damage);
	r->damaged++;
}

double get_window_opacity(xcb_window_t wid)
{
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *r;
	uint32_t val = OPAQUE;

	if (opacity != -1) {
		return opacity;
	}
	ck = xcb_get_property_unchecked(X, 0, wid, _NET_WM_WINDOW_OPACITY,
						XCB_ATOM_CARDINAL, 0L, 1L);
	r = xcb_get_property_reply(X, ck, NULL);
	if (r) {
		if (r->value_len && r->format == 32)
			val = *(uint32_t *) xcb_get_property_value(r);
		free(r);
	}
	return (double) val / OPAQUE;
}

xcb_render_picture_t get_alpha_picture(double o)
{
	xcb_pixmap_t pid;
	xcb_render_picture_t pict;
	xcb_render_color_t c = { 0x0, 0x0, 0x0, o * 0xffff };
	xcb_rectangle_t r = { 0, 0, 1, 1 };
	uint32_t vals = 1;

	pid = xcb_generate_id(X);
	xcb_create_pixmap(X, 8, pid, root->id, 1, 1);
	pict = xcb_generate_id(X);
	xcb_render_create_picture(X, pict, pid, pict_a_8,
						XCB_RENDER_CP_REPEAT, &vals);
	xcb_free_pixmap(X, pid);
	xcb_render_fill_rectangles(X, XCB_RENDER_PICT_OP_SRC, pict, c, 1, &r);
	return pict;
}

static struct window *sanitize_window_list(struct root *r, struct window *w)
{
	struct window *tmp;

	for (tmp = NULL; w != NULL; w = w->next) {
		if (w->_class == XCB_WINDOW_CLASS_INPUT_ONLY ||
				w->map_state == XCB_MAP_STATE_UNMAPPED ||
							OFFSCREEN(w, root))
			continue;
		if (!w->picture) {
			xcb_render_pictvisual_t *pv;
			uint32_t mask = XCB_RENDER_CP_SUBWINDOW_MODE;
			uint32_t vals = XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS;

			if (!w->pixmap) {
				w->pixmap = xcb_generate_id(X);
				xcb_composite_name_window_pixmap(X, w->id,
								w->pixmap);
			}
			w->picture = xcb_generate_id(X);
			pv = xcb_render_util_find_visual_format(
					xcb_render_util_query_formats(X),
					w->visual);
			xcb_render_create_picture(X, w->picture,
					w->pixmap, pv->format, mask, &vals);
		}
		if (w->opacity != OPAQUE) {
			if (!w->alpha)
				w->alpha = get_alpha_picture(w->opacity);
		} else {
			if (w->alpha) {
				xcb_render_free_picture(X, w->alpha);
				w->alpha = XCB_NONE;
			}
		}
		/* this is not realy needed... or is it? */
		xcb_xfixes_intersect_region(X, w->region, r->damage,
								w->region);
		w->prev = tmp;
		tmp = w;
	}
	return tmp;
}

static void paint(struct root *r)
{
	struct window *w;

	paint_background(r);
	w = sanitize_window_list(r, r->window_list);
	for (; w != NULL; w = w->prev) {
		xcb_xfixes_set_picture_clip_region(X, r->picture_buffer,
					w->region, w->x + w->border_width,
					w->y + w->border_width);
		xcb_render_composite(X, XCB_RENDER_PICT_OP_OVER, w->picture,
				w->alpha, r->picture_buffer, 0, 0, 0, 0,
				w->x, w->y, WIDTH(w), HEIGHT(w));
	}
	xcb_xfixes_set_picture_clip_region(X, r->picture_buffer, r->damage,
									0, 0);
	xcb_render_composite(X, XCB_RENDER_PICT_OP_SRC,
					r->picture_buffer, XCB_NONE, r->picture,
					0, 0, 0, 0, 0, 0, r->width, r->height);
	xcb_xfixes_set_region(X, r->damage, 0, NULL);
	r->damaged = 0;
}

#define HANDLE(func, e) func((xcb_ ## func ## _event_t *) e)

static void handle_event(xcb_generic_event_t *e)
{
	switch (e->response_type) {
	case XCB_CREATE_NOTIFY: HANDLE(create_notify, e); break;
	case XCB_DESTROY_NOTIFY: HANDLE(destroy_notify, e); break;
	case XCB_UNMAP_NOTIFY: HANDLE(unmap_notify, e); break;
	case XCB_MAP_NOTIFY: HANDLE(map_notify, e); break;
	case XCB_REPARENT_NOTIFY: HANDLE(reparent_notify, e); break;
	case XCB_CONFIGURE_NOTIFY: HANDLE(configure_notify, e); break;
	case XCB_CIRCULATE_NOTIFY: HANDLE(circulate_notify, e); break;
	case XCB_PROPERTY_NOTIFY: HANDLE(property_notify, e); break;
	default:
		if (e->response_type == damage_event) {
			HANDLE(damage_notify, e);
		} else if (e->response_type) {
			debugf("handle_event: unhandled event: %hu\n",
							e->response_type);
		}
		break;
	}
}

static int loop(void)
{
	xcb_generic_event_t *e;

	while (1) {
		if (!(e = xcb_wait_for_event(X)))
			return -1;
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

static void usage(const char *name)
{
	printf("Usage: %s [ -o opacity ]\n", name);
}

double opacity = -1;
xcb_render_picture_t alpha_picture;

int parse_args(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; ++i) {
		if (argv[i][0] != '-' || argv[i][2] != '\0')
			goto exit;
		switch (argv[i][1]) {
		case 'o':
			if (++i < argc) {
				opacity = atof(argv[i]);
			} else {
				fprintf(stderr, "-o requires an argument\n");
				return -1;
			}
			break;
		case 'v':
			printf("%s: \n", argv[0]);
			break;
		default:
			break;
		}
	}
	return 0;
exit:
	fprintf(stderr, "invalid argument '%s'\n", argv[i]);
	return -1;
}

xcb_connection_t *X;
struct root *root;
xcb_render_pictformat_t pict_a_8, pict_rgb_24, pict_argb_32;

int main(int argc, char *argv[])
{
	xcb_screen_t *s;

	if (parse_args(argc, argv) != 0) {
		usage(argv[0]);
		return 1;
	}
	X = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(X)) {
		fprintf(stderr, "can't connect X\n");
		return 1;
	}
	xcb_prefetch_extension_data(X, &xcb_render_id);
	xcb_prefetch_extension_data(X, &xcb_xfixes_id);
	xcb_prefetch_extension_data(X, &xcb_damage_id);
	xcb_prefetch_extension_data(X, &xcb_composite_id);
	init_extensions();
	pict_a_8 = xcb_render_util_find_standard_format(
					xcb_render_util_query_formats(X),
					XCB_PICT_STANDARD_A_8)->id;
	pict_rgb_24 = xcb_render_util_find_standard_format(
					xcb_render_util_query_formats(X),
					XCB_PICT_STANDARD_RGB_24)->id;
	pict_argb_32 = xcb_render_util_find_standard_format(
					xcb_render_util_query_formats(X),
					XCB_PICT_STANDARD_ARGB_32)->id;
	s = xcb_setup_roots_iterator(xcb_get_setup(X)).data;
	init_atoms();
	root = malloc(sizeof(struct root));
	if (root == NULL || setup_root(s, root) != 0)
		return 1;
	xcb_flush(X);
	loop();
	return 0;
}

