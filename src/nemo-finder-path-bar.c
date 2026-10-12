/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Path Bar estilo Finder. Ver nemo-finder-path-bar.h.
 */

#include <config.h>

#include "nemo-finder-path-bar.h"
#include <string.h>

#include <glib/gi18n.h>
#include <libnemo-private/nemo-file-utilities.h>

#define MIN_WIDTH 60

typedef struct {
	GFile *file;
	GtkWidget *widget;
} Segment;

struct _NemoFinderPathBar {
	GtkContainer parent;

	GPtrArray *segments;    /* Segment * */
	GPtrArray *separators;  /* GtkWidget *, entre segmentos consecutivos */
	GtkWidget *ellipsis;
	GtkWidget *ellipsis_separator;
};

enum {
	LOCATION_CLICKED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (NemoFinderPathBar, nemo_finder_path_bar, GTK_TYPE_CONTAINER)

static void
segment_free (gpointer data)
{
	Segment *segment = data;

	g_object_unref (segment->file);
	g_free (segment);
}

static GtkWidget *
create_separator (void)
{
	GtkWidget *label = gtk_label_new ("›");

	gtk_style_context_add_class (gtk_widget_get_style_context (label),
				     "nemo-finder-pathbar-separator");
	gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_start (label, 5);
	gtk_widget_set_margin_end (label, 5);
	gtk_widget_show (label);

	return label;
}

static gboolean
file_is_user_dir (GFile *file,
                  GUserDirectory directory,
                  const gchar *fallback)
{
	const gchar *path = g_get_user_special_dir (directory);
	gchar *fallback_path = NULL;
	gchar *file_path = g_file_get_path (file);
	gboolean match;

	/* user-dirs.dirs puede apuntar la carpeta a $HOME */
	if (path == NULL || g_strcmp0 (path, g_get_home_dir ()) == 0) {
		fallback_path = g_build_filename (g_get_home_dir (), fallback, NULL);
		path = fallback_path;
	}

	match = file_path != NULL && g_strcmp0 (file_path, path) == 0;

	g_free (file_path);
	g_free (fallback_path);

	return match;
}

static const gchar *
segment_icon_name (GFile *file,
                   gboolean is_root)
{
	gchar *scheme;
	const gchar *icon = "folder-symbolic";

	if (!g_file_is_native (file)) {
		scheme = g_file_get_uri_scheme (file);

		if (g_strcmp0 (scheme, "recent") == 0)
			icon = "document-open-recent-symbolic";
		else if (g_strcmp0 (scheme, "trash") == 0)
			icon = "user-trash-symbolic";
		else if (g_strcmp0 (scheme, "search") == 0)
			icon = "edit-find-symbolic";
		else if (g_strcmp0 (scheme, "network") == 0)
			icon = "network-workgroup-symbolic";
		else if (is_root)
			icon = "network-server-symbolic";

		g_free (scheme);
		return icon;
	}

	if (is_root) {
		return "drive-harddisk";
	} else {
		gchar *path = g_file_get_path (file);

		if (g_strcmp0 (path, g_get_home_dir ()) == 0)
			icon = "user-home-symbolic";
		else if (file_is_user_dir (file, G_USER_DIRECTORY_DESKTOP, "Desktop"))
			icon = "user-desktop-symbolic";
		else if (file_is_user_dir (file, G_USER_DIRECTORY_DOCUMENTS, "Documents"))
			icon = "folder-documents-symbolic";
		else if (file_is_user_dir (file, G_USER_DIRECTORY_DOWNLOAD, "Downloads"))
			icon = "folder-download-symbolic";
		else if (file_is_user_dir (file, G_USER_DIRECTORY_MUSIC, "Music"))
			icon = "folder-music-symbolic";
		else if (file_is_user_dir (file, G_USER_DIRECTORY_PICTURES, "Pictures"))
			icon = "folder-pictures-symbolic";
		else if (file_is_user_dir (file, G_USER_DIRECTORY_VIDEOS, "Videos"))
			icon = "folder-videos-symbolic";

		g_free (path);
	}

	return icon;
}

static gchar *
segment_label (GFile *file,
               gboolean is_root)
{
	gchar *scheme = g_file_get_uri_scheme (file);
	gchar *label;

	if (is_root) {
		if (g_strcmp0 (scheme, "file") == 0)
			label = nemo_get_root_display_name ();
		else if (g_strcmp0 (scheme, "recent") == 0)
			label = g_strdup (_("Recent"));
		else if (g_strcmp0 (scheme, "trash") == 0)
			label = g_strdup (_("Trash"));
		else if (g_strcmp0 (scheme, "network") == 0)
			label = g_strdup (_("Network"));
		else if (g_strcmp0 (scheme, "search") == 0)
			label = g_strdup (_("Search"));
		else
			label = g_file_get_parse_name (file);
	} else {
		label = g_file_get_basename (file);
		if (label == NULL)
			label = g_file_get_parse_name (file);
	}

	g_free (scheme);

	return label;
}

static gboolean
segment_clicked_cb (GtkWidget *widget,
                    GdkEventButton *event,
                    gpointer user_data)
{
	NemoFinderPathBar *bar = NEMO_FINDER_PATH_BAR (user_data);
	GFile *file = g_object_get_data (G_OBJECT (widget), "nemo-finder-file");

	if (event->button != GDK_BUTTON_PRIMARY || file == NULL)
		return GDK_EVENT_PROPAGATE;

	g_signal_emit (bar, signals[LOCATION_CLICKED], 0, file);

	return GDK_EVENT_STOP;
}

/* El icono sale del tema (el mismo que el archivo en la vista y el lateral);
 * si no hay, queda el simbólico genérico. */
static GtkWidget *
segment_image_new (GFile *file,
                   gboolean is_root)
{
	GtkWidget *image = NULL;

	if (g_file_is_native (file)) {
		GFileInfo *info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_ICON,
		                                     G_FILE_QUERY_INFO_NONE, NULL, NULL);

		if (info != NULL) {
			GIcon *icon = g_file_info_get_icon (info);

			if (is_root)
				icon = NULL; /* la raíz muestra el disco, no la carpeta «/» */
			if (icon != NULL)
				image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_MENU);
			g_object_unref (info);
		}
	}

	if (image == NULL)
		image = gtk_image_new_from_icon_name (segment_icon_name (file, is_root),
		                                      GTK_ICON_SIZE_MENU);
	gtk_image_set_pixel_size (GTK_IMAGE (image), 16);
	return image;
}

static Segment *
segment_new (NemoFinderPathBar *bar,
             GFile *file,
             gboolean is_root,
             gboolean is_current)
{
	Segment *segment = g_new0 (Segment, 1);
	GtkWidget *event_box = gtk_event_box_new ();
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget *image = segment_image_new (file, is_root);
	gchar *name = segment_label (file, is_root);
	GtkWidget *label = gtk_label_new (name);

	g_free (name);

	segment->file = g_object_ref (file);
	segment->widget = event_box;

	gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_NONE);
	gtk_container_add (GTK_CONTAINER (box), image);
	gtk_container_add (GTK_CONTAINER (box), label);
	gtk_container_add (GTK_CONTAINER (event_box), box);

	gtk_event_box_set_visible_window (GTK_EVENT_BOX (event_box), FALSE);
	gtk_style_context_add_class (gtk_widget_get_style_context (event_box),
				     "nemo-finder-pathbar-segment");
	if (is_current)
		gtk_style_context_add_class (gtk_widget_get_style_context (event_box), "current");

	g_object_set_data_full (G_OBJECT (event_box), "nemo-finder-file",
				g_object_ref (file), g_object_unref);
	g_signal_connect (event_box, "button-press-event",
			  G_CALLBACK (segment_clicked_cb), bar);

	gtk_widget_show_all (event_box);

	return segment;
}

static void
clear_segments (NemoFinderPathBar *bar)
{
	guint i;

	for (i = 0; i < bar->segments->len; i++) {
		Segment *segment = g_ptr_array_index (bar->segments, i);

		gtk_widget_unparent (segment->widget);
	}
	for (i = 0; i < bar->separators->len; i++)
		gtk_widget_unparent (g_ptr_array_index (bar->separators, i));

	g_ptr_array_set_size (bar->segments, 0);
	g_ptr_array_set_size (bar->separators, 0);
}

/* Dentro de un zip montado (archive://…) la ruta sigue por fuera: …/carpeta/Paquete.app
 * y luego lo de adentro, en vez de la URI escapada del montaje. */
static GList *
replace_archive_root (GList *chain)
{
	GFile *root = chain != NULL ? chain->data : NULL;
	char *uri, *host_start, *slash, *host, *once, *real_uri;
	GFile *real;
	GList *real_chain = NULL, *rest;

	if (root == NULL || !g_file_has_uri_scheme (root, "archive"))
		return chain;

	uri = g_file_get_uri (root);
	host_start = uri + strlen ("archive://");
	slash = strchr (host_start, '/');
	host = slash != NULL ? g_strndup (host_start, slash - host_start) : g_strdup (host_start);
	once = g_uri_unescape_string (host, NULL);
	real_uri = once != NULL ? g_uri_unescape_string (once, NULL) : NULL;
	g_free (uri);
	g_free (host);
	g_free (once);
	if (real_uri == NULL)
		return chain;

	real = g_file_new_for_uri (real_uri);
	g_free (real_uri);
	for (GFile *f = g_object_ref (real); f != NULL; ) {
		GFile *parent = g_file_get_parent (f);

		real_chain = g_list_prepend (real_chain, f);
		f = parent;
	}
	g_object_unref (real);

	/* chain sin su primer elemento (la raíz del zip, que es el propio archivo) */
	rest = g_list_delete_link (chain, chain);
	g_object_unref (root);
	return g_list_concat (real_chain, rest);
}

void
nemo_finder_path_bar_set_location (NemoFinderPathBar *bar,
                                   GFile *location,
                                   GFile *selected)
{
	GList *chain = NULL, *l;
	GFile *file;

	g_return_if_fail (NEMO_IS_FINDER_PATH_BAR (bar));

	clear_segments (bar);

	if (location == NULL) {
		gtk_widget_queue_resize (GTK_WIDGET (bar));
		return;
	}

	/* Si hay algo seleccionado dentro de la carpeta, la ruta termina en él. */
	file = g_object_ref (selected != NULL && g_file_has_parent (selected, location) ? selected : location);
	while (file != NULL) {
		GFile *parent = g_file_get_parent (file);

		chain = g_list_prepend (chain, file); /* toma la referencia */
		file = parent;
	}

	chain = replace_archive_root (chain);

	for (l = chain; l != NULL; l = l->next) {
		gboolean is_root = (l == chain);
		gboolean is_current = (l->next == NULL);
		Segment *segment = segment_new (bar, l->data, is_root, is_current);

		if (!is_root) {
			GtkWidget *separator = create_separator ();

			gtk_widget_set_parent (separator, GTK_WIDGET (bar));
			g_ptr_array_add (bar->separators, separator);
		}

		gtk_widget_set_parent (segment->widget, GTK_WIDGET (bar));
		g_ptr_array_add (bar->segments, segment);
	}

	g_list_free_full (chain, g_object_unref);

	gtk_widget_queue_resize (GTK_WIDGET (bar));
}

/* Padding y borde del nodo CSS: el widget no tiene ventana propia, así que
 * los aplica él mismo al medir, colocar y dibujar. */
static void
get_css_extra (GtkWidget *widget,
               GtkBorder *extra)
{
	GtkStyleContext *context = gtk_widget_get_style_context (widget);
	GtkStateFlags state = gtk_widget_get_state_flags (widget);
	GtkBorder padding, border;

	gtk_style_context_get_padding (context, state, &padding);
	gtk_style_context_get_border (context, state, &border);

	extra->left = padding.left + border.left;
	extra->right = padding.right + border.right;
	extra->top = padding.top + border.top;
	extra->bottom = padding.bottom + border.bottom;
}

static gint
natural_width (GtkWidget *widget)
{
	gint nat = 0;

	gtk_widget_get_preferred_width (widget, NULL, &nat);

	return nat;
}

static void
nemo_finder_path_bar_get_preferred_width (GtkWidget *widget,
                                          gint *minimum,
                                          gint *natural)
{
	NemoFinderPathBar *bar = NEMO_FINDER_PATH_BAR (widget);
	gint total = 0;
	guint i;

	for (i = 0; i < bar->segments->len; i++)
		total += natural_width (((Segment *) g_ptr_array_index (bar->segments, i))->widget);
	for (i = 0; i < bar->separators->len; i++)
		total += natural_width (g_ptr_array_index (bar->separators, i));

	{
		GtkBorder extra;

		get_css_extra (widget, &extra);
		*minimum = MIN (total, MIN_WIDTH) + extra.left + extra.right;
		*natural = total + extra.left + extra.right;
	}
}

static void
nemo_finder_path_bar_get_preferred_height (GtkWidget *widget,
                                           gint *minimum,
                                           gint *natural)
{
	NemoFinderPathBar *bar = NEMO_FINDER_PATH_BAR (widget);
	gint height = 0;
	guint i;

	for (i = 0; i < bar->segments->len; i++) {
		gint min = 0, nat = 0;

		gtk_widget_get_preferred_height (((Segment *) g_ptr_array_index (bar->segments, i))->widget,
						 &min, &nat);
		height = MAX (height, nat);
	}

	{
		GtkBorder extra;

		get_css_extra (widget, &extra);
		*minimum = *natural = height + extra.top + extra.bottom;
	}
}

static gint
place_child (GtkWidget *child,
             GtkAllocation *alloc,
             gint x)
{
	GtkAllocation child_alloc;
	gint nat = natural_width (child);
	gint room = alloc->x + alloc->width - x;

	child_alloc.x = x;
	child_alloc.y = alloc->y;
	child_alloc.width = MAX (MIN (nat, room), 1);
	child_alloc.height = alloc->height;

	gtk_widget_set_child_visible (child, TRUE);
	gtk_widget_size_allocate (child, &child_alloc);

	return x + nat;
}

static void
nemo_finder_path_bar_size_allocate (GtkWidget *widget,
                                    GtkAllocation *alloc)
{
	NemoFinderPathBar *bar = NEMO_FINDER_PATH_BAR (widget);
	guint n = bar->segments->len;
	guint i, k = 1;
	gint total = 0, x;
	gboolean use_ellipsis = FALSE;

	GtkAllocation inner_alloc;
	GtkBorder extra;

	gtk_widget_set_allocation (widget, alloc);

	get_css_extra (widget, &extra);
	inner_alloc.x = alloc->x + extra.left;
	inner_alloc.y = alloc->y + extra.top;
	inner_alloc.width = MAX (alloc->width - extra.left - extra.right, 1);
	inner_alloc.height = MAX (alloc->height - extra.top - extra.bottom, 1);
	alloc = &inner_alloc;

	/* todo oculto salvo lo que se coloque abajo */
	for (i = 0; i < n; i++)
		gtk_widget_set_child_visible (((Segment *) g_ptr_array_index (bar->segments, i))->widget, FALSE);
	for (i = 0; i < bar->separators->len; i++)
		gtk_widget_set_child_visible (g_ptr_array_index (bar->separators, i), FALSE);
	gtk_widget_set_child_visible (bar->ellipsis, FALSE);
	gtk_widget_set_child_visible (bar->ellipsis_separator, FALSE);

	if (n == 0)
		return;

	for (i = 0; i < n; i++)
		total += natural_width (((Segment *) g_ptr_array_index (bar->segments, i))->widget);
	for (i = 0; i < bar->separators->len; i++)
		total += natural_width (g_ptr_array_index (bar->separators, i));

	if (total > alloc->width && n > 2) {
		gint head = natural_width (((Segment *) g_ptr_array_index (bar->segments, 0))->widget) +
		            natural_width (g_ptr_array_index (bar->separators, 0)) +
		            natural_width (bar->ellipsis) +
		            natural_width (bar->ellipsis_separator);

		/* conserva el primero y la cola más larga que quepa */
		for (k = 1; k < n - 1; k++) {
			gint tail = 0;
			guint j;

			for (j = k; j < n; j++) {
				tail += natural_width (((Segment *) g_ptr_array_index (bar->segments, j))->widget);
				if (j > k)
					tail += natural_width (g_ptr_array_index (bar->separators, j - 1));
			}

			if (head + tail <= alloc->width)
				break;
		}
		use_ellipsis = TRUE;
	}

	x = alloc->x;
	x = place_child (((Segment *) g_ptr_array_index (bar->segments, 0))->widget, alloc, x);

	if (n > 1) {
		if (use_ellipsis) {
			x = place_child (g_ptr_array_index (bar->separators, 0), alloc, x);
			x = place_child (bar->ellipsis, alloc, x);
			x = place_child (bar->ellipsis_separator, alloc, x);
		}

		for (i = 1; i < n; i++) {
			if (use_ellipsis && i < k)
				continue;
			if (!(use_ellipsis && i == k))
				x = place_child (g_ptr_array_index (bar->separators, i - 1), alloc, x);
			x = place_child (((Segment *) g_ptr_array_index (bar->segments, i))->widget, alloc, x);
		}
	}
}

static gboolean
nemo_finder_path_bar_draw (GtkWidget *widget,
                           cairo_t *cr)
{
	GtkStyleContext *context = gtk_widget_get_style_context (widget);
	gint width = gtk_widget_get_allocated_width (widget);
	gint height = gtk_widget_get_allocated_height (widget);

	gtk_render_background (context, cr, 0, 0, width, height);
	gtk_render_frame (context, cr, 0, 0, width, height);

	return GTK_WIDGET_CLASS (nemo_finder_path_bar_parent_class)->draw (widget, cr);
}

static void
nemo_finder_path_bar_forall (GtkContainer *container,
                             gboolean include_internals,
                             GtkCallback callback,
                             gpointer callback_data)
{
	NemoFinderPathBar *bar = NEMO_FINDER_PATH_BAR (container);
	guint i;

	for (i = 0; i < bar->segments->len; i++)
		callback (((Segment *) g_ptr_array_index (bar->segments, i))->widget, callback_data);
	for (i = 0; i < bar->separators->len; i++)
		callback (g_ptr_array_index (bar->separators, i), callback_data);

	callback (bar->ellipsis, callback_data);
	callback (bar->ellipsis_separator, callback_data);
}

static void
nemo_finder_path_bar_remove (GtkContainer *container,
                             GtkWidget *widget)
{
	/* los hijos los gestiona set_location */
}

static void
nemo_finder_path_bar_finalize (GObject *object)
{
	NemoFinderPathBar *bar = NEMO_FINDER_PATH_BAR (object);

	g_ptr_array_free (bar->segments, TRUE);
	g_ptr_array_free (bar->separators, TRUE);

	G_OBJECT_CLASS (nemo_finder_path_bar_parent_class)->finalize (object);
}

static void
nemo_finder_path_bar_dispose (GObject *object)
{
	NemoFinderPathBar *bar = NEMO_FINDER_PATH_BAR (object);

	clear_segments (bar);

	if (bar->ellipsis != NULL) {
		gtk_widget_unparent (bar->ellipsis);
		bar->ellipsis = NULL;
	}
	if (bar->ellipsis_separator != NULL) {
		gtk_widget_unparent (bar->ellipsis_separator);
		bar->ellipsis_separator = NULL;
	}

	G_OBJECT_CLASS (nemo_finder_path_bar_parent_class)->dispose (object);
}

static void
nemo_finder_path_bar_init (NemoFinderPathBar *bar)
{
	gtk_widget_set_has_window (GTK_WIDGET (bar), FALSE);
	gtk_widget_set_redraw_on_allocate (GTK_WIDGET (bar), FALSE);

	bar->segments = g_ptr_array_new_with_free_func (segment_free);
	bar->separators = g_ptr_array_new ();

	gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (bar)),
				     "nemo-finder-pathbar");

	bar->ellipsis = gtk_label_new ("…");
	gtk_style_context_add_class (gtk_widget_get_style_context (bar->ellipsis),
				     "nemo-finder-pathbar-separator");
	gtk_widget_set_valign (bar->ellipsis, GTK_ALIGN_CENTER);
	gtk_widget_show (bar->ellipsis);
	gtk_widget_set_parent (bar->ellipsis, GTK_WIDGET (bar));

	bar->ellipsis_separator = create_separator ();
	gtk_widget_set_parent (bar->ellipsis_separator, GTK_WIDGET (bar));
}

static void
nemo_finder_path_bar_class_init (NemoFinderPathBarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

	object_class->dispose = nemo_finder_path_bar_dispose;
	object_class->finalize = nemo_finder_path_bar_finalize;

	widget_class->get_preferred_width = nemo_finder_path_bar_get_preferred_width;
	widget_class->get_preferred_height = nemo_finder_path_bar_get_preferred_height;
	widget_class->size_allocate = nemo_finder_path_bar_size_allocate;
	widget_class->draw = nemo_finder_path_bar_draw;

	container_class->forall = nemo_finder_path_bar_forall;
	container_class->remove = nemo_finder_path_bar_remove;

	signals[LOCATION_CLICKED] =
		g_signal_new ("location-clicked",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, NULL,
			      G_TYPE_NONE, 1, G_TYPE_FILE);

	gtk_widget_class_set_css_name (widget_class, "nemo-finder-path-bar");
}

GtkWidget *
nemo_finder_path_bar_new (void)
{
	return g_object_new (NEMO_TYPE_FINDER_PATH_BAR, NULL);
}
