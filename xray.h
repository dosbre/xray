#ifdef DEBUG
#define debug(msg) do { fprintf(stderr, msg); } while (0)
#define debugf(fmt, ...) do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define debug(msg) do { } while (0)
#define debugf(fmt, ...) do { } while (0)
#endif

#define HANDLE(name, event)						\
	do { name((xcb_ ## name ## _event_t *) event); } while (0)

#include <xcb/xcb.h>
#include <xcb/damage.h>
#include <xcb/render.h>

extern xcb_connection_t *X;
extern xcb_generic_error_t *error;

extern uint8_t pict_rgb_24;

int check_cookie(xcb_void_cookie_t ck);
void xerror(const char *s);

struct root {
	xcb_window_t id;
	uint32_t depth;
	uint16_t width;
	uint16_t height;
	xcb_render_picture_t picture;
	xcb_render_picture_t picture_buffer;
	xcb_render_picture_t background;
	xcb_xfixes_region_t damage;
	uint16_t damaged;
	struct window *window_list;
};

extern struct root *root;

void add_damaged_region(struct root *root, xcb_xfixes_region_t region);
int paint_background(struct root *root);
void paint(struct root *root);

/* window.c */
#define GEOMCPY(win1, win2)					\
	do {							\
		win1->x = win2->x;				\
		win1->y = win2->y;				\
		win1->width = win2->width;			\
		win1->height = win2->height;			\
		win1->border_width = win2->border_width;	\
	} while (0)
#define WIDTH(win) (win->width + (win->border_width * 2))
#define HEIGHT(win) (win->height + (win->border_width * 2))
#define HBOUND(win) ((win)->x + (int) WIDTH((win)))
#define VBOUND(win) ((win)->y + (int) HEIGHT((win)))
#define OFFSCREEN(win, root)	\
	((			\
		((win)->x > (int) root->width) ||	\
		(HBOUND(win) < 0)			\
	) || (						\
		((win)->y > (int) root->height) ||	\
		(VBOUND(win) < 0)			\
	))

struct window {
	xcb_window_t id;
	int16_t x, y;
	uint16_t width, height;
	uint16_t border_width;
	uint8_t map_state;
	uint8_t override_redirect;
	xcb_pixmap_t pixmap;
	xcb_render_picture_t picture;
	xcb_damage_damage_t damage;
	xcb_xfixes_region_t region;
	struct window *prev;	/* only used for transparency */
	struct window *next;
};
struct window *find_window(struct window *list, xcb_window_t wid);
struct window *add_window(struct window **list, xcb_window_t wid);
int add_winvec(struct window **list, xcb_window_t wid[], int len);
int remove_window(struct window **list, struct window *win);
void restack_window(struct window **list, struct window *win, xcb_window_t sib);

/* event.c */
void create_notify(xcb_create_notify_event_t *e);
void destroy_notify(xcb_destroy_notify_event_t *e);
void unmap_notify(xcb_unmap_notify_event_t *e);
void map_notify(xcb_map_notify_event_t *e);
void reparent_notify(xcb_reparent_notify_event_t *e);
void configure_notify(xcb_configure_notify_event_t *e);
void circulate_notify(xcb_circulate_notify_event_t *e);
void damage_notify(xcb_damage_notify_event_t *e);

/* util.c */
xcb_pixmap_t update_pixmap(struct window *win);
xcb_render_picture_t update_picture(struct window *win);
void debug_region(xcb_xfixes_region_t region);
xcb_render_picture_t get_alpha_picture(void);
