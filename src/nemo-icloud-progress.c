/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Avance de subidas y bajadas de iCloud Drive (rclone, API rc por socket Unix).
 */

#include <config.h>

#include "nemo-icloud-progress.h"

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>

#define POLL_ACTIVE_SECONDS 1
#define POLL_IDLE_SECONDS   3

typedef struct {
	char     *name;
	gint64    size;
	gint64    bytes;
	double    speed;
	gboolean  upload;
	gboolean  running;
} Item;

struct _NemoICloudProgress {
	GObject     parent;

	char       *socket_path;
	guint       timer;
	gboolean    busy;

	GPtrArray  *items;       /* Item*, estado actual */
	GHashTable *seen;        /* nombre -> tamaño, para lo que ya terminó */
	gint64      finished;    /* bytes de lo que terminó en esta tanda */
	double      fraction;

	GtkWidget  *window;
	GtkWidget  *list;
	GtkWidget  *empty_label;
};

enum { CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];

G_DEFINE_TYPE (NemoICloudProgress, nemo_icloud_progress, G_TYPE_OBJECT)

static void
item_free (Item *item)
{
	g_free (item->name);
	g_free (item);
}

/* ---- Cliente HTTP mínimo sobre el socket Unix (hilo aparte) ---- */

static char *
rc_request (const char *socket_path, const char *method, GError **error)
{
	GSocketClient *client = g_socket_client_new ();
	GSocketAddress *addr = g_unix_socket_address_new (socket_path);
	GSocketConnection *conn;
	GString *response;
	char *request, *body = NULL;
	const char *sep;

	g_socket_client_set_timeout (client, 3);
	conn = g_socket_client_connect (client, G_SOCKET_CONNECTABLE (addr), NULL, error);
	g_object_unref (addr);
	g_object_unref (client);
	if (conn == NULL)
		return NULL;

	request = g_strdup_printf ("POST /%s HTTP/1.0\r\nHost: rclone\r\n"
				   "Content-Length: 0\r\n\r\n", method);
	if (!g_output_stream_write_all (g_io_stream_get_output_stream (G_IO_STREAM (conn)),
					request, strlen (request), NULL, NULL, error)) {
		g_free (request);
		g_object_unref (conn);
		return NULL;
	}
	g_free (request);

	response = g_string_new (NULL);
	for (;;) {
		char buf[4096];
		gssize n = g_input_stream_read (g_io_stream_get_input_stream (G_IO_STREAM (conn)),
						buf, sizeof buf, NULL, error);
		if (n < 0) {
			g_string_free (response, TRUE);
			g_object_unref (conn);
			return NULL;
		}
		if (n == 0)
			break;
		g_string_append_len (response, buf, n);
	}
	g_object_unref (conn);

	sep = strstr (response->str, "\r\n\r\n");
	if (sep != NULL)
		body = g_strdup (sep + 4);
	g_string_free (response, TRUE);
	return body;
}

typedef struct {
	char *queue;
	char *stats;
} RcResult;

static void
rc_result_free (RcResult *r)
{
	g_free (r->queue);
	g_free (r->stats);
	g_free (r);
}

static void
poll_thread (GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
	NemoICloudProgress *self = source;
	RcResult *r = g_new0 (RcResult, 1);
	GError *error = NULL;

	r->queue = rc_request (self->socket_path, "vfs/queue", &error);
	if (r->queue != NULL)
		r->stats = rc_request (self->socket_path, "core/stats", &error);
	if (r->queue == NULL || r->stats == NULL) {
		g_clear_error (&error);
		rc_result_free (r);
		g_task_return_pointer (task, NULL, NULL);
		return;
	}
	g_task_return_pointer (task, r, (GDestroyNotify) rc_result_free);
}

/* ---- Estado ---- */

static JsonArray *
parse_array (const char *text, const char *member)
{
	JsonParser *parser = json_parser_new ();
	JsonArray *array = NULL;

	if (json_parser_load_from_data (parser, text, -1, NULL)) {
		JsonNode *root = json_parser_get_root (parser);
		if (root != NULL && JSON_NODE_HOLDS_OBJECT (root)) {
			JsonObject *o = json_node_get_object (root);
			if (json_object_has_member (o, member) &&
			    JSON_NODE_HOLDS_ARRAY (json_object_get_member (o, member)))
				array = json_array_ref (json_object_get_array_member (o, member));
		}
	}
	g_object_unref (parser);
	return array;
}

static Item *
find_item (GPtrArray *items, const char *name)
{
	for (guint i = 0; i < items->len; i++) {
		Item *item = g_ptr_array_index (items, i);
		if (g_strcmp0 (item->name, name) == 0)
			return item;
	}
	return NULL;
}

static void
apply_state (NemoICloudProgress *self, RcResult *r)
{
	GPtrArray *items = g_ptr_array_new_with_free_func ((GDestroyNotify) item_free);
	JsonArray *queue = parse_array (r->queue, "queue");
	JsonArray *transfers = parse_array (r->stats, "transferring");
	gint64 total = 0, done = 0;
	GHashTableIter it;
	gpointer key, value;

	if (queue != NULL) {
		for (guint i = 0; i < json_array_get_length (queue); i++) {
			JsonObject *o = json_array_get_object_element (queue, i);
			Item *item = g_new0 (Item, 1);
			item->name = g_strdup (json_object_get_string_member_with_default (o, "name", ""));
			item->size = json_object_get_int_member_with_default (o, "size", 0);
			item->upload = TRUE;
			item->running = json_object_get_boolean_member_with_default (o, "uploading", FALSE);
			g_ptr_array_add (items, item);
		}
	}
	if (transfers != NULL) {
		for (guint i = 0; i < json_array_get_length (transfers); i++) {
			JsonObject *o = json_array_get_object_element (transfers, i);
			const char *name = json_object_get_string_member_with_default (o, "name", "");
			Item *item = find_item (items, name);

			if (item == NULL) {
				item = g_new0 (Item, 1);
				item->name = g_strdup (name);
				g_ptr_array_add (items, item);
			}
			item->running = TRUE;
			item->size = json_object_get_int_member_with_default (o, "size", item->size);
			item->bytes = json_object_get_int_member_with_default (o, "bytes", 0);
			item->speed = json_object_get_double_member_with_default (o, "speed", 0);
		}
	}
	g_clear_pointer (&queue, json_array_unref);
	g_clear_pointer (&transfers, json_array_unref);

	/* Lo que estaba y ya no está, terminó: sigue contando en la tanda. */
	g_hash_table_iter_init (&it, self->seen);
	while (g_hash_table_iter_next (&it, &key, &value)) {
		if (find_item (items, key) == NULL)
			self->finished += GPOINTER_TO_SIZE (value);
	}
	g_hash_table_remove_all (self->seen);

	for (guint i = 0; i < items->len; i++) {
		Item *item = g_ptr_array_index (items, i);
		g_hash_table_insert (self->seen, g_strdup (item->name), GSIZE_TO_POINTER (item->size));
		total += item->size;
		done += MIN (item->bytes, item->size);
	}

	if (items->len == 0) {
		self->finished = 0;
		self->fraction = 0;
	} else if (total + self->finished > 0) {
		self->fraction = (double) (done + self->finished) / (double) (total + self->finished);
	} else {
		self->fraction = 0;
	}

	g_ptr_array_unref (self->items);
	self->items = items;
}

static gboolean poll_cb (gpointer data);

static void
poll_done (GObject *source, GAsyncResult *res, gpointer data)
{
	NemoICloudProgress *self = NEMO_ICLOUD_PROGRESS (source);
	RcResult *r = g_task_propagate_pointer (G_TASK (res), NULL);
	gboolean was_active = self->items->len > 0;

	self->busy = FALSE;
	if (r != NULL) {
		apply_state (self, r);
		rc_result_free (r);
	} else {
		g_ptr_array_set_size (self->items, 0);
		self->finished = 0;
		self->fraction = 0;
	}
	if (was_active || self->items->len > 0)
		g_signal_emit (self, signals[CHANGED], 0);

	/* ajusta la cadencia: rápido mientras haya actividad */
	if (self->timer != 0)
		g_source_remove (self->timer);
	self->timer = g_timeout_add_seconds (self->items->len > 0 ? POLL_ACTIVE_SECONDS : POLL_IDLE_SECONDS,
					     poll_cb, self);
}

static gboolean
poll_cb (gpointer data)
{
	NemoICloudProgress *self = data;
	GTask *task;

	self->timer = 0;
	if (self->busy)
		return G_SOURCE_REMOVE;

	if (!g_file_test (self->socket_path, G_FILE_TEST_EXISTS)) {
		if (self->items->len > 0) {
			g_ptr_array_set_size (self->items, 0);
			g_signal_emit (self, signals[CHANGED], 0);
		}
		self->timer = g_timeout_add_seconds (POLL_IDLE_SECONDS, poll_cb, self);
		return G_SOURCE_REMOVE;
	}

	self->busy = TRUE;
	task = g_task_new (self, NULL, poll_done, NULL);
	g_task_run_in_thread (task, poll_thread);
	g_object_unref (task);
	return G_SOURCE_REMOVE;
}

/* ---- GObject ---- */

static void
nemo_icloud_progress_init (NemoICloudProgress *self)
{
	const char *env = g_getenv ("FYNDER_ICLOUD_RC");

	if (env != NULL && env[0] != '\0')
		self->socket_path = g_strdup (env);
	else
		self->socket_path = g_build_filename (g_get_user_runtime_dir (), "rclone-icloud.sock", NULL);
	self->items = g_ptr_array_new_with_free_func ((GDestroyNotify) item_free);
	self->seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
nemo_icloud_progress_class_init (NemoICloudProgressClass *klass)
{
	signals[CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass),
					 G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
					 G_TYPE_NONE, 0);
}

NemoICloudProgress *
nemo_icloud_progress_get (void)
{
	static NemoICloudProgress *instance = NULL;

	if (instance == NULL) {
		instance = g_object_new (NEMO_TYPE_ICLOUD_PROGRESS, NULL);
		poll_cb (instance);
	}
	return instance;
}

gboolean
nemo_icloud_progress_is_active (NemoICloudProgress *self)
{
	return self->items->len > 0;
}

/* ---- Círculo ---- */

GdkPixbuf *
nemo_icloud_progress_get_pixbuf (NemoICloudProgress *self, int size)
{
	cairo_surface_t *surface;
	cairo_t *cr;
	GdkPixbuf *pixbuf;
	double c = size / 2.0, r = size / 2.0 - 1.5;

	if (self->items->len == 0)
		return NULL;

	surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
	cr = cairo_create (surface);

	cairo_set_source_rgba (cr, 0.55, 0.55, 0.58, 0.9);
	cairo_set_line_width (cr, 1.2);
	cairo_arc (cr, c, c, r, 0, 2 * G_PI);
	cairo_stroke (cr);

	if (self->fraction > 0.01) {
		cairo_set_source_rgba (cr, 0.0, 0.48, 1.0, 1.0);
		cairo_move_to (cr, c, c);
		cairo_arc (cr, c, c, r - 2.2, -G_PI / 2, -G_PI / 2 + 2 * G_PI * self->fraction);
		cairo_close_path (cr);
		cairo_fill (cr);
	}
	cairo_destroy (cr);
	pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, size, size);
	cairo_surface_destroy (surface);
	return pixbuf;
}

/* ---- Ventana ---- */

static char *
format_size (gint64 n)
{
	return g_format_size (n);
}

static void
rebuild_window (NemoICloudProgress *self)
{
	GList *children, *l;

	if (self->window == NULL)
		return;

	children = gtk_container_get_children (GTK_CONTAINER (self->list));
	for (l = children; l != NULL; l = l->next)
		gtk_widget_destroy (l->data);
	g_list_free (children);

	gtk_widget_set_visible (self->empty_label, self->items->len == 0);

	for (guint i = 0; i < self->items->len; i++) {
		Item *item = g_ptr_array_index (self->items, i);
		GtkWidget *row = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
		GtkWidget *top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
		GtkWidget *icon = gtk_image_new_from_icon_name (item->upload ? "go-up-symbolic" : "go-down-symbolic",
								GTK_ICON_SIZE_MENU);
		GtkWidget *name = gtk_label_new (item->name);
		GtkWidget *bar = gtk_progress_bar_new ();
		char *done_s, *size_s, *speed_s = NULL, *text;
		GtkWidget *detail;
		double frac = item->size > 0 ? (double) item->bytes / (double) item->size : 0;

		gtk_label_set_ellipsize (GTK_LABEL (name), PANGO_ELLIPSIZE_MIDDLE);
		gtk_label_set_xalign (GTK_LABEL (name), 0);
		gtk_widget_set_hexpand (name, TRUE);
		gtk_box_pack_start (GTK_BOX (top), icon, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (top), name, TRUE, TRUE, 0);
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (bar), CLAMP (frac, 0, 1));

		done_s = format_size (item->bytes);
		size_s = format_size (item->size);
		if (item->running && item->speed > 0) {
			char *sp = g_format_size ((guint64) item->speed);
			speed_s = g_strdup_printf (" · %s/s", sp);
			g_free (sp);
		}
		if (item->running)
			text = g_strdup_printf (item->upload ? _("Uploading: %s of %s%s") : _("Downloading: %s of %s%s"),
						done_s, size_s, speed_s ? speed_s : "");
		else
			text = g_strdup_printf (_("Waiting to upload · %s"), size_s);
		detail = gtk_label_new (text);
		gtk_label_set_xalign (GTK_LABEL (detail), 0);
		gtk_style_context_add_class (gtk_widget_get_style_context (detail), "dim-label");
		g_free (done_s); g_free (size_s); g_free (speed_s); g_free (text);

		gtk_box_pack_start (GTK_BOX (row), top, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (row), bar, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (row), detail, FALSE, FALSE, 0);
		gtk_container_add (GTK_CONTAINER (self->list), row);
	}
	gtk_widget_show_all (self->list);
}

static void
window_destroyed (GtkWidget *w, NemoICloudProgress *self)
{
	g_signal_handlers_disconnect_by_func (self, rebuild_window, NULL);
	self->window = NULL;
	self->list = NULL;
	self->empty_label = NULL;
}

void
nemo_icloud_progress_show_window (NemoICloudProgress *self, GtkWindow *parent)
{
	GtkWidget *box, *scroll;

	if (self->window != NULL) {
		gtk_window_present (GTK_WINDOW (self->window));
		return;
	}

	self->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (self->window), _("iCloud Drive"));
	gtk_window_set_default_size (GTK_WINDOW (self->window), 420, 320);
	gtk_window_set_type_hint (GTK_WINDOW (self->window), GDK_WINDOW_TYPE_HINT_DIALOG);
	if (parent != NULL)
		gtk_window_set_transient_for (GTK_WINDOW (self->window), parent);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width (GTK_CONTAINER (box), 12);
	self->empty_label = gtk_label_new (_("Everything is up to date"));
	gtk_style_context_add_class (gtk_widget_get_style_context (self->empty_label), "dim-label");
	gtk_widget_set_no_show_all (self->empty_label, TRUE);
	gtk_box_pack_start (GTK_BOX (box), self->empty_label, FALSE, FALSE, 12);

	self->list = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scroll), self->list);
	gtk_box_pack_start (GTK_BOX (box), scroll, TRUE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (self->window), box);

	g_signal_connect_swapped (self, "changed", G_CALLBACK (rebuild_window), self);
	g_signal_connect (self->window, "destroy", G_CALLBACK (window_destroyed), self);
	rebuild_window (self);
	gtk_widget_show_all (self->window);
	gtk_widget_set_visible (self->empty_label, self->items->len == 0);
}
