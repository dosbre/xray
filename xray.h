#ifdef DEBUG
#define debug(msg) fprintf(stderr, msg)
#define debugf(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
#define debug(msg)
#define debugf(fmt, ...)
#endif

#define LENGTH(array) (sizeof(array) / sizeof(array[0]))

#define WIDTH(win) ((win)->width + ((win)->border_width * 2))
#define HEIGHT(win) ((win)->height + ((win)->border_width * 2))
#define HBOUND(win) ((win)->x + (int) WIDTH((win)))
#define VBOUND(win) ((win)->y + (int) HEIGHT((win)))
#define OFFSCREEN(win, root)	\
	((			\
		((win)->x > (int) (root)->width) ||	\
		(HBOUND(win) < 0)			\
	) || (						\
		((win)->y > (int) (root)->height) ||	\
		(VBOUND(win) < 0)			\
	))

#define OPACITY_PROPERTY_NAME "_NET_WM_WINDOW_OPACITY"
#define OPAQUE 0xffffffff

extern xcb_connection_t *X;

extern xcb_render_pictformat_t pict_a_8, pict_rgb_24, pict_argb_32;

extern xcb_atom_t _XROOTPMAP_ID;
extern xcb_atom_t _NET_WM_WINDOW_OPACITY;

extern struct root {
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
} *root;

struct window {
	xcb_window_t id;
	int16_t x, y;
	uint16_t width, height;
	uint16_t border_width;
	xcb_visualid_t visual;
	uint8_t map_state;
	xcb_damage_damage_t damage;
	xcb_xfixes_region_t region;
	xcb_pixmap_t pixmap;
	xcb_render_picture_t picture;
	uint32_t opacity;
	xcb_render_picture_t alpha;
	struct window *prev;
	struct window *next;
};

void add_damaged_region(struct root *root, xcb_xfixes_region_t region);
uint32_t get_opacity(xcb_window_t wid);

/* window.c */
struct window *find_win(struct window *list, xcb_window_t wid);
struct window *add_win(struct window **list, xcb_window_t wid);
int remove_win(struct window **list, struct window *win);
void restack_win(struct window **list, struct window *win, xcb_window_t sib);
void init_win(struct window *win, xcb_get_window_attributes_reply_t *ar,
						xcb_get_geometry_reply_t *gr);
int add_winvec(struct window **list, xcb_window_t wid[], int len);

/* event.c */
void create_notify(xcb_create_notify_event_t *e);
void destroy_notify(xcb_destroy_notify_event_t *e);
void unmap_notify(xcb_unmap_notify_event_t *e);
void map_notify(xcb_map_notify_event_t *e);
void reparent_notify(xcb_reparent_notify_event_t *e);
void configure_notify(xcb_configure_notify_event_t *e);
void circulate_notify(xcb_circulate_notify_event_t *e);
void property_notify(xcb_property_notify_event_t *e);
void damage_notify(xcb_damage_notify_event_t *e);

/* util.c */
extern xcb_generic_error_t *error;
unsigned check_error(const char *s);
unsigned check_cookie(xcb_void_cookie_t ck);

