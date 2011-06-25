#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/damage.h>
#include <xcb/composite.h>
#include <xcb/xcb_renderutil.h>
#include "xray.h"

xcb_generic_error_t *error;

static const char *strerror[] = { "success", "request", "value", "window",
	"pixmap", "atom", "cursor", "font", "match", "drawable", "access",
	"alloc", "colormap", "gcontext", "idchoice", "name", "length",
	"implementation" };

unsigned check_error(const char *s)
{
	unsigned ret = 0;
	const char *se;

	if (error != NULL) {
		ret = error->error_code;
		se = (ret < LENGTH(strerror)) ? strerror[ret] : "unknow";
		fprintf(stderr, "Xerror %s: (%u) %s\n", s, ret, se);
		free(error);
	}
	return ret;
}

unsigned check_cookie(xcb_void_cookie_t ck)
{
	xcb_generic_error_t *e;
	unsigned ret = 0;

	if ((e = xcb_request_check(X, ck)) != NULL) {
		ret = e->error_code;
		debugf("check_cookie: %d %s\n", ret,
			(ret < LENGTH(strerror)) ? strerror[ret] : "unknow");
		free(e);
	}
	return ret;
}

