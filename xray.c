#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/damage.h>
#include <xcb/composite.h>
#include "xray.h"

xcb_connection_t *X;
xcb_generic_error_t *error;

int xerror(char *s)
{
	int ret = 0;

	if (error && ++ret) {
		fprintf(stderr, "%s: error code %hu\n", s, error->error_code);
		free(error);
	}

	return ret;
}

int usage(char *prog)
{
	fprintf(stderr, "Usage: %s <window_id>\n", prog);

	return 2;
}

int init_extensions(void)
{
	int ret = 0;
	xcb_damage_query_version_cookie_t damage_ck;
	xcb_composite_query_version_cookie_t composite_ck;

	xcb_damage_query_version_reply_t *damage_r;
	xcb_composite_query_version_reply_t *composite_r;

	damage_ck = xcb_damage_query_version(X, 1, 1);
	composite_ck = xcb_composite_query_version(X, 0, 4);

	damage_r = xcb_damage_query_version_reply(X, damage_ck, &error);
	if (error && xerror("init_extensions"))
		++ret;

	composite_r = xcb_composite_query_version_reply(X,
							composite_ck, &error);
	if (error && xerror("init_extensions"))
		++ret;

	return ret;
}

void setup(xcb_window_t win)
{
	xcb_damage_damage_t damage;
	uint8_t level;
	xcb_void_cookie_t ck;

	damage = xcb_generate_id(X);
	level = XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY;

	ck = xcb_damage_create_checked(X, damage, win, level);
	if ((error = xcb_request_check(X, ck)) != NULL)
		xerror("setup");
}

void damage_notify(xcb_damage_notify_event_t *e)
{
	xcb_rectangle_t area;
	xcb_rectangle_t geometry;

	area = e->area;
	geometry = e->geometry;

	printf("area: %hd %hd %hu %hu\n", area.x, area.y,
						area.width, area.height);

	printf("geometry: %hd %hd %hu %hu\n", geometry.x, geometry.y,
					geometry.width, geometry.height);
}

void handle_event(xcb_generic_event_t *e)
{
	uint8_t damage_notify_event;
	damage_notify_event = xcb_get_extension_data(X, &xcb_damage_id)->first_event;

	printf("response_type: %hu\n", e->response_type);
	if (e->response_type == damage_notify_event)
		damage_notify((xcb_damage_notify_event_t *) e);
	/*
	switch (e->response_type) {
	case (damage_event + XCB_DAMAGE_NOTIFY):
		printf("\t...damage notify\n");
		break;
	default:
		printf("\t...unknow\n");
		break;
	}
	*/
}

int loop(void)
{
	xcb_generic_event_t *e;

	while (1) {
		e = xcb_wait_for_event(X);
		if (!e)
			return 1;

		do {
			printf("An event has arrived!\n");
			handle_event(e);
			free(e);
		} while ((e = xcb_poll_for_event(X)));
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int ret = 0;

	if (argc < 2)
		return usage(argv[0]);

	X = xcb_connect(NULL, NULL);

	if ((ret = xcb_connection_has_error(X)) != 0)
		fprintf(stderr, "can't connect X\n");

	xcb_prefetch_extension_data(X, &xcb_damage_id);
	xcb_prefetch_extension_data(X, &xcb_composite_id);

	init_extensions();

	setup((xcb_window_t) atoi(argv[1]));

	ret = loop();

	return ret;
}

